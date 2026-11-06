/*
 * SPDX-FileCopyrightText: 2026 StackChan-GPT Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal_sd_backup.h"
#include "board/hal_bridge.h"
#include <ssid_manager.h>
#include <mooncake_log.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>
#include <mutex>
#include <unistd.h>

static const std::string_view _tag = "HAL-SDBackup";

static constexpr const char* kFileName     = "stackchan_config.json";
static constexpr const char* kTmpFileName  = "stackchan_config.json.tmp";
static constexpr const char* kPlainKeyFile = "openai_key.txt";
static constexpr const char* kAltKeyFile   = "api_key.txt";
static constexpr int kSaveDebounceMs       = 1500;

static TaskHandle_t _save_task = nullptr;
static std::once_flag _save_task_once;

namespace hal_sd_backup {

namespace {

struct OpenaiField {
    const char* key;
    std::string OpenaiConfig_t::* member;
};

constexpr OpenaiField kOpenaiFields[] = {
    {"api_key", &OpenaiConfig_t::apiKey},       {"base_url", &OpenaiConfig_t::baseUrl},
    {"chat_model", &OpenaiConfig_t::chatModel}, {"stt_model", &OpenaiConfig_t::sttModel},
    {"tts_model", &OpenaiConfig_t::ttsModel},   {"tts_voice", &OpenaiConfig_t::ttsVoice},
    {"sys_prompt", &OpenaiConfig_t::systemPrompt}, {"wake_word", &OpenaiConfig_t::wakeWord},
};

std::string join_path(const char* mount_point, const char* name)
{
    return std::string(mount_point) + "/" + name;
}

bool read_file(const std::string& path, std::string& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    out.clear();
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    fclose(f);
    return true;
}

bool write_file_atomic(const char* mount_point, const std::string& content)
{
    const std::string path     = join_path(mount_point, kFileName);
    const std::string tmp_path = join_path(mount_point, kTmpFileName);

    FILE* f = fopen(tmp_path.c_str(), "wb");
    if (!f) {
        return false;
    }
    const bool written = fwrite(content.data(), 1, content.size(), f) == content.size();
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    if (!written) {
        unlink(tmp_path.c_str());
        return false;
    }
    // FATFS rename() fails when the target exists.
    unlink(path.c_str());
    return rename(tmp_path.c_str(), path.c_str()) == 0;
}

bool parse(const std::string& json, Backup_t& out)
{
    cJSON* root = cJSON_ParseWithLength(json.data(), json.size());
    if (!root) {
        return false;
    }

    cJSON* wifi = cJSON_GetObjectItem(root, "wifi");
    if (cJSON_IsArray(wifi)) {
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, wifi)
        {
            cJSON* ssid     = cJSON_GetObjectItem(item, "ssid");
            cJSON* password = cJSON_GetObjectItem(item, "password");
            if (cJSON_IsString(ssid) && ssid->valuestring[0] != '\0') {
                out.wifi.push_back({ssid->valuestring, cJSON_IsString(password) ? password->valuestring : ""});
            }
        }
    }

    cJSON* openai = cJSON_GetObjectItem(root, "openai");
    if (cJSON_IsObject(openai)) {
        for (const auto& field : kOpenaiFields) {
            cJSON* value = cJSON_GetObjectItem(openai, field.key);
            if (cJSON_IsString(value)) {
                out.openai.*field.member = value->valuestring;
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

std::string serialize(const Backup_t& backup)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);

    cJSON* wifi = cJSON_AddArrayToObject(root, "wifi");
    for (const auto& cred : backup.wifi) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", cred.ssid.c_str());
        cJSON_AddStringToObject(item, "password", cred.password.c_str());
        cJSON_AddItemToArray(wifi, item);
    }

    cJSON* openai = cJSON_AddObjectToObject(root, "openai");
    for (const auto& field : kOpenaiFields) {
        const std::string& value = backup.openai.*field.member;
        if (!value.empty()) {
            cJSON_AddStringToObject(openai, field.key, value.c_str());
        }
    }

    char* printed = cJSON_Print(root);
    std::string result = printed ? printed : "";
    cJSON_free(printed);
    cJSON_Delete(root);
    return result;
}

Backup_t snapshot()
{
    Backup_t backup;
    for (const auto& item : SsidManager::GetInstance().GetSsidList()) {
        backup.wifi.push_back({item.ssid, item.password});
    }
    backup.openai = hal_openai::get_stored_config();
    return backup;
}

bool is_empty(const Backup_t& backup)
{
    if (!backup.wifi.empty()) {
        return false;
    }
    for (const auto& field : kOpenaiFields) {
        if (!(backup.openai.*field.member).empty()) {
            return false;
        }
    }
    return true;
}

void save_now()
{
    const Backup_t backup = snapshot();
    // Never let an empty NVS (e.g. right after a full flash) clobber a good backup.
    if (is_empty(backup)) {
        mclog::tagInfo(_tag, "nothing to back up");
        return;
    }
    const std::string content = serialize(backup);

    const bool card_present = hal_bridge::board_sd_card_session([&](const char* mount_point) {
        std::string existing;
        if (read_file(join_path(mount_point, kFileName), existing) && existing == content) {
            mclog::tagInfo(_tag, "SD backup already up to date");
            return;
        }
        if (write_file_atomic(mount_point, content)) {
            if (!backup.openai.apiKey.empty()) {
                const std::string key_path = join_path(mount_point, kPlainKeyFile);
                FILE* kf                   = fopen(key_path.c_str(), "wb");
                if (kf) {
                    fwrite(backup.openai.apiKey.data(), 1, backup.openai.apiKey.size(), kf);
                    fwrite("\n", 1, 1, kf);
                    fflush(kf);
                    fsync(fileno(kf));
                    fclose(kf);
                }
            }
            mclog::tagInfo(_tag, "saved {} Wi-Fi network(s) and OpenAI key/settings to SD card", backup.wifi.size());
        } else {
            mclog::tagWarn(_tag, "failed to write SD backup");
        }
    });
    if (!card_present) {
        mclog::tagInfo(_tag, "no SD card, skipping backup");
    }
}

void save_task(void*)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // Coalesce bursts of changes (e.g. Wi-Fi + config saved together).
        vTaskDelay(pdMS_TO_TICKS(kSaveDebounceMs));
        ulTaskNotifyTake(pdTRUE, 0);
        save_now();
    }
}

}  // namespace

LoadResult_t load()
{
    LoadResult_t result;
    result.cardPresent = hal_bridge::board_sd_card_session([&](const char* mount_point) {
        std::string content;
        if (read_file(join_path(mount_point, kFileName), content) ||
            read_file(join_path(mount_point, kTmpFileName), content)) {
            result.hasBackup = parse(content, result.backup);
            if (!result.hasBackup) {
                mclog::tagWarn(_tag, "SD backup file is not valid JSON, ignoring");
            }
        } else {
            mclog::tagInfo(_tag, "no JSON backup file on SD card yet");
        }

        // Also support a plain-text key file (/sdcard/openai_key.txt or /sdcard/api_key.txt)
        if (result.backup.openai.apiKey.empty()) {
            std::string key_txt;
            if (read_file(join_path(mount_point, kPlainKeyFile), key_txt) ||
                read_file(join_path(mount_point, kAltKeyFile), key_txt)) {
                size_t first = key_txt.find_first_not_of(" \t\r\n\"'");
                size_t last  = key_txt.find_last_not_of(" \t\r\n\"'");
                if (first != std::string::npos && last != std::string::npos && last >= first) {
                    result.backup.openai.apiKey = key_txt.substr(first, last - first + 1);
                    result.hasBackup            = true;
                    mclog::tagInfo(_tag, "loaded OpenAI API key from SD text file");
                }
            }
        }
    });
    return result;
}

void request_save()
{
    std::call_once(_save_task_once,
                   [] { xTaskCreate(save_task, "sd_backup", 6144, nullptr, tskIDLE_PRIORITY + 1, &_save_task); });
    if (_save_task) {
        xTaskNotifyGive(_save_task);
    }
}

}  // namespace hal_sd_backup
