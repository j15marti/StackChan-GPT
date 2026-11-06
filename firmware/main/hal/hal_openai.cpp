/*
 * SPDX-FileCopyrightText: 2026 StackChan-GPT Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal_openai.h"
#include "hal.h"
#include "hal_sd_backup.h"
#include <openai_config.h>
#include <settings.h>
#include <ssid_manager.h>
#include <wifi_manager.h>
#include <mooncake_log.h>
#include <stackchan/stackchan.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include <atomic>
#include <cmath>
#include <mutex>
#include <algorithm>

static const std::string_view _tag    = "HAL-OpenAI";
static const std::string_view _nvs_ns = "openai";

static std::mutex _config_mutex;
static std::atomic<bool> _custom_led_active{false};
static std::atomic<bool> _thinking_led_active{false};
static std::atomic<uint32_t> _last_voice_playback_ms{0};
static httpd_handle_t _http_server = nullptr;

namespace hal_openai {

static std::string trim_trailing_slash(std::string url)
{
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

static std::string trim_whitespace(std::string s)
{
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

static std::string sanitize_api_key(std::string s)
{
    s = trim_whitespace(std::move(s));
    while (!s.empty() && (s.front() == '"' || s.front() == '\'')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == '"' || s.back() == '\'')) {
        s.pop_back();
    }
    s = trim_whitespace(std::move(s));
    // Strip accidental prefix (e.g. "Bearer " or a pasted terminal prompt/path before "sk-")
    size_t sk_pos = s.find("sk-");
    if (sk_pos != std::string::npos && sk_pos > 0) {
        s.erase(0, sk_pos);
    }
    size_t end_pos = s.find_first_of(" \t\r\n\"'");
    if (end_pos != std::string::npos) {
        s.erase(end_pos);
    }
    return s;
}

OpenaiConfig_t get_config()
{
    std::lock_guard<std::mutex> lock(_config_mutex);
    Settings settings(_nvs_ns.data(), false);

    OpenaiConfig_t cfg;
    cfg.apiKey       = settings.GetString("api_key", OPENAI_API_KEY_DEFAULT);
    cfg.baseUrl      = settings.GetString("base_url", OPENAI_BASE_URL_DEFAULT);
    cfg.chatModel    = settings.GetString("chat_model", OPENAI_CHAT_MODEL_DEFAULT);
    cfg.sttModel     = settings.GetString("stt_model", OPENAI_STT_MODEL_DEFAULT);
    cfg.ttsModel     = settings.GetString("tts_model", OPENAI_TTS_MODEL_DEFAULT);
    cfg.ttsVoice     = settings.GetString("tts_voice", OPENAI_TTS_VOICE_DEFAULT);
    cfg.systemPrompt = settings.GetString("sys_prompt", OPENAI_SYSTEM_PROMPT_DEFAULT);
    cfg.wakeWord     = settings.GetString("wake_word", "StackChan");

    if (cfg.apiKey.empty() && std::string_view(OPENAI_API_KEY_DEFAULT).size() > 0) {
        cfg.apiKey = OPENAI_API_KEY_DEFAULT;
    }
    if (cfg.baseUrl.empty()) {
        cfg.baseUrl = OPENAI_BASE_URL_DEFAULT;
    }
    if (cfg.chatModel.empty()) {
        cfg.chatModel = OPENAI_CHAT_MODEL_DEFAULT;
    }
    if (cfg.chatModel == "gpt5.5") {
        cfg.chatModel = "gpt-5.5";
    }
    if (cfg.sttModel.empty()) {
        cfg.sttModel = OPENAI_STT_MODEL_DEFAULT;
    }
    if (cfg.ttsModel.empty()) {
        cfg.ttsModel = OPENAI_TTS_MODEL_DEFAULT;
    }
    if (cfg.ttsVoice.empty()) {
        cfg.ttsVoice = OPENAI_TTS_VOICE_DEFAULT;
    }
    if (cfg.systemPrompt.empty()) {
        cfg.systemPrompt = OPENAI_SYSTEM_PROMPT_DEFAULT;
    }
    if (cfg.wakeWord.empty()) {
        cfg.wakeWord = "StackChan";
    }

    cfg.apiKey = sanitize_api_key(cfg.apiKey);
    cfg.baseUrl = trim_trailing_slash(cfg.baseUrl);
    return cfg;
}

OpenaiConfig_t get_stored_config()
{
    std::lock_guard<std::mutex> lock(_config_mutex);
    Settings settings(_nvs_ns.data(), false);

    OpenaiConfig_t cfg;
    cfg.apiKey       = sanitize_api_key(settings.GetString("api_key", ""));
    cfg.baseUrl      = settings.GetString("base_url", "");
    cfg.chatModel    = settings.GetString("chat_model", "");
    cfg.sttModel     = settings.GetString("stt_model", "");
    cfg.ttsModel     = settings.GetString("tts_model", "");
    cfg.ttsVoice     = settings.GetString("tts_voice", "");
    cfg.systemPrompt = settings.GetString("sys_prompt", "");
    cfg.wakeWord     = settings.GetString("wake_word", "");
    return cfg;
}

// Fills only the NVS keys that are currently unset, so newer local edits always win.
static bool restore_missing_config(const OpenaiConfig_t& backup)
{
    const OpenaiConfig_t stored = get_stored_config();
    std::lock_guard<std::mutex> lock(_config_mutex);
    Settings settings(_nvs_ns.data(), true);

    bool restored     = false;
    auto restore_once = [&](const char* key, const std::string& current, const std::string& saved) {
        if (current.empty() && !saved.empty()) {
            settings.SetString(key, saved);
            restored = true;
        }
    };
    const std::string clean_backup_key = sanitize_api_key(backup.apiKey);
    restore_once("api_key", stored.apiKey, clean_backup_key);
    restore_once("base_url", stored.baseUrl, backup.baseUrl);
    restore_once("chat_model", stored.chatModel, backup.chatModel);
    restore_once("stt_model", stored.sttModel, backup.sttModel);
    restore_once("tts_model", stored.ttsModel, backup.ttsModel);
    restore_once("tts_voice", stored.ttsVoice, backup.ttsVoice);
    restore_once("sys_prompt", stored.systemPrompt, backup.systemPrompt);
    if (stored.wakeWord.empty() && !backup.wakeWord.empty()) {
        settings.SetString("wake_word", backup.wakeWord);
        Settings sc_settings("stackchan", true);
        sc_settings.SetString("device_name", backup.wakeWord);
        restored = true;
    }
    return restored;
}

static void restore_from_sd_card()
{
    const auto result = hal_sd_backup::load();
    if (!result.cardPresent) {
        return;
    }

    if (result.hasBackup) {
        auto& ssid_mgr = SsidManager::GetInstance();
        // Only repopulate an empty list (post full-flash) so networks removed on
        // the device are not resurrected from a stale backup.
        if (ssid_mgr.GetSsidList().empty() && !result.backup.wifi.empty()) {
            // AddSsid prepends, so add in reverse to keep the saved priority order.
            for (auto it = result.backup.wifi.rbegin(); it != result.backup.wifi.rend(); ++it) {
                ssid_mgr.AddSsid(it->ssid, it->password);
            }
            mclog::tagInfo(_tag, "Restored {} Wi-Fi network(s) from SD card", result.backup.wifi.size());
        }
        if (restore_missing_config(result.backup.openai)) {
            mclog::tagInfo(_tag, "Restored OpenAI settings from SD card");
        }
    }

    // Create or refresh the backup now that a card is known to be present.
    hal_sd_backup::request_save();
}

void set_config(const OpenaiConfig_t& config)
{
    std::lock_guard<std::mutex> lock(_config_mutex);
    Settings settings(_nvs_ns.data(), true);

    std::string clean_key = sanitize_api_key(config.apiKey);
    if (!clean_key.empty()) {
        settings.SetString("api_key", clean_key);
    }
    if (!config.baseUrl.empty()) {
        settings.SetString("base_url", trim_trailing_slash(config.baseUrl));
    }
    if (!config.chatModel.empty()) {
        std::string model = (config.chatModel == "gpt5.5") ? std::string("gpt-5.5") : config.chatModel;
        settings.SetString("chat_model", model);
    }
    if (!config.sttModel.empty()) {
        settings.SetString("stt_model", config.sttModel);
    }
    if (!config.ttsModel.empty()) {
        settings.SetString("tts_model", config.ttsModel);
    }
    if (!config.ttsVoice.empty()) {
        settings.SetString("tts_voice", config.ttsVoice);
    }
    if (!config.systemPrompt.empty()) {
        settings.SetString("sys_prompt", config.systemPrompt);
    }
    std::string clean_wake = trim_whitespace(config.wakeWord);
    if (!clean_wake.empty()) {
        settings.SetString("wake_word", clean_wake);
    }

    mclog::tagInfo(_tag, "OpenAI config saved (name/wake={}, model={}, voice={}, base_url={})",
                   clean_wake.empty() ? "StackChan" : clean_wake, config.chatModel, config.ttsVoice, config.baseUrl);
    hal_sd_backup::request_save();
}

std::string get_wake_word()
{
    return get_config().wakeWord;
}

void set_wake_word(std::string_view wake_word)
{
    std::string clean = trim_whitespace(std::string(wake_word));
    if (clean.empty()) {
        clean = "StackChan";
    }
    {
        std::lock_guard<std::mutex> lock(_config_mutex);
        Settings settings(_nvs_ns.data(), true);
        settings.SetString("wake_word", clean);
    }
    // Also save as device_name in stackchan namespace for consistency
    {
        Settings sc_settings("stackchan", true);
        sc_settings.SetString("device_name", clean);
    }
    mclog::tagInfo(_tag, "Device wake word / name set to: {}", clean);
    hal_sd_backup::request_save();
}

bool has_api_key()
{
    auto cfg = get_config();
    return !cfg.apiKey.empty() && cfg.apiKey != "your_openai_api_key";
}

std::string mask_api_key(std::string_view key)
{
    if (key.empty()) {
        return "Not Configured";
    }
    if (key.size() <= 10) {
        return "Configured (***)";
    }
    return std::string(key.substr(0, 6)) + "..." + std::string(key.substr(key.size() - 4));
}

void apply_boot_defaults()
{
    {
        std::lock_guard<std::mutex> lock(_config_mutex);
        Settings ro_settings(_nvs_ns.data(), false);
        std::string raw_key   = ro_settings.GetString("api_key", "");
        std::string clean_key = sanitize_api_key(raw_key);
        if (!raw_key.empty() && clean_key != raw_key) {
            Settings rw_settings(_nvs_ns.data(), true);
            rw_settings.SetString("api_key", clean_key);
            mclog::tagInfo(_tag, "Sanitized stored OpenAI API key in NVS");
        }
    }
    restore_from_sd_card();

    std::string default_ssid(OPENAI_WIFI_SSID_DEFAULT);
    std::string default_pass(OPENAI_WIFI_PASSWORD_DEFAULT);
    if (!default_ssid.empty()) {
        auto& ssid_mgr = SsidManager::GetInstance();
        auto list      = ssid_mgr.GetSsidList();
        bool found     = false;
        for (const auto& item : list) {
            if (item.ssid == default_ssid) {
                found = true;
                break;
            }
        }
        if (!found) {
            mclog::tagInfo(_tag, "Adding default Wi-Fi SSID from openai_config.h: {}", default_ssid);
            ssid_mgr.AddSsid(default_ssid, default_pass);
        }
    }

    if (!SsidManager::GetInstance().GetSsidList().empty() || has_api_key()) {
        Settings app_settings("app_config", true);
        if (!app_settings.GetBool("is_configed", false)) {
            app_settings.SetBool("is_configed", true);
            mclog::tagInfo(_tag, "Marked device as configured (bypassing original server sign-in)");
        }
    }
}

void set_custom_led_active(bool active)
{
    _custom_led_active.store(active);
}

bool is_custom_led_active()
{
    return _custom_led_active.load();
}

void set_thinking_led_active(bool active)
{
    _thinking_led_active.store(active);
}

bool is_thinking_led_active()
{
    return _thinking_led_active.load();
}

void update_thinking_heartbeat_leds()
{
    if (!_thinking_led_active.load() || _custom_led_active.load()) {
        return;
    }

    // 20 BPM (3000 ms per cycle) smooth cosine breathing wave:
    //   0..1500 ms    : gradual cosine rise from soft blue (8) up to peak blue (160)
    //   1500..3000 ms : gradual cosine fall from peak blue (160) back to soft blue (8)
    constexpr uint32_t kCycleMs = 3000;
    const uint32_t phase        = GetHAL().millis() % kCycleMs;
    const float angle           = (static_cast<float>(phase) / static_cast<float>(kCycleMs)) * 6.2831853f;
    const float wave            = 0.5f * (1.0f - std::cos(angle));  // Smooth 0.0 -> 1.0 -> 0.0
    const uint8_t blue          = static_cast<uint8_t>(8.0f + wave * 152.0f);
    const uint8_t green         = static_cast<uint8_t>(1.0f + wave * 24.0f);

    GetHAL().showRgbColor(0, green, blue);
}

void notify_voice_playback_frame()
{
    _last_voice_playback_ms.store(GetHAL().millis());
}

bool is_voice_playback_active()
{
    const uint32_t last = _last_voice_playback_ms.load();
    if (last == 0) {
        return false;
    }
    return (GetHAL().millis() - last) <= 220;
}

static const char kWebPortalHtml[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>StackChan-GPT ? Agent.ai OpenAI Setup</title>
<style>
  :root { --bg:#0f172a; --card:#1e293b; --accent:#10b981; --text:#f8fafc; --muted:#94a3b8; --border:#334155; }
  * { box-sizing:border-box; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif; }
  body { margin:0; padding:20px; background:var(--bg); color:var(--text); }
  .wrap { max-width:540px; margin:0 auto; }
  .card { background:var(--card); border:1px solid var(--border); border-radius:16px; padding:22px; margin-bottom:16px; box-shadow:0 10px 25px rgba(0,0,0,0.3); }
  h1 { margin:0 0 6px; font-size:22px; color:#34d399; }
  p.sub { margin:0 0 18px; color:var(--muted); font-size:14px; }
  label { display:block; font-size:13px; font-weight:600; margin:12px 0 6px; color:#cbd5e1; }
  input, select, textarea { width:100%; padding:10px 12px; border-radius:10px; border:1px solid var(--border); background:#0f172a; color:var(--text); font-size:14px; }
  textarea { min-height:80px; resize:vertical; }
  .row { display:grid; grid-template-columns:1fr 1fr; gap:12px; }
  button { width:100%; padding:12px; border:none; border-radius:10px; font-size:15px; font-weight:600; cursor:pointer; margin-top:14px; }
  .btn-primary { background:var(--accent); color:#052e16; }
  .btn-secondary { background:#3b82f6; color:#fff; }
  .badge { display:inline-block; padding:4px 10px; border-radius:999px; font-size:12px; font-weight:600; background:#064e3b; color:#6ee7b7; margin-bottom:10px; }
  #status { margin-top:12px; font-size:14px; text-align:center; min-height:20px; }
</style>
</head>
<body>
<div class="wrap">
  <div class="card">
    <div class="badge" id="keyBadge">Loading...</div>
    <h1>StackChan-GPT (Agent.ai)</h1>
    <p class="sub">Direct OpenAI Voice, Responses, Servo &amp; RGB LED Control ? No original server sign-in required.</p>
    <form id="cfgForm">
      <label>Device Name / Wake Word ("What is my name?")</label>
      <input type="text" id="wakeWord" placeholder="StackChan">
      <label>OpenAI API Key</label>
      <input type="password" id="apiKey" placeholder="sk-proj-..." autocomplete="off">
      <label>OpenAI Base URL</label>
      <input type="text" id="baseUrl" placeholder="https://api.openai.com/v1">
      <div class="row">
        <div>
          <label>Chat Model</label>
          <select id="chatModel">
            <option value="gpt-live">gpt-live (Realtime Live WS)</option>
            <option value="gpt-5.4-nano">gpt-5.4-nano</option>
            <option value="gpt-5.5">gpt-5.5</option>
            <option value="gpt-5.6-terra">gpt-5.6-terra</option>
            <option value="gpt-4o-mini">gpt-4o-mini (Fast)</option>
            <option value="gpt-4o">gpt-4o</option>
            <option value="gpt-4.1-mini">gpt-4.1-mini</option>
            <option value="gpt-4.1">gpt-4.1</option>
          </select>
        </div>
        <div>
          <label>TTS Voice</label>
          <select id="ttsVoice">
            <option value="marin">marin (GPT-Live)</option>
            <option value="cedar">cedar (GPT-Live)</option>
            <option value="nova">nova</option>
            <option value="alloy">alloy</option>
            <option value="shimmer">shimmer</option>
            <option value="echo">echo</option>
            <option value="fable">fable</option>
            <option value="onyx">onyx</option>
            <option value="coral">coral</option>
            <option value="ash">ash</option>
            <option value="sage">sage</option>
          </select>
        </div>
      </div>
      <div class="row">
        <div>
          <label>STT Model</label>
          <input type="text" id="sttModel" placeholder="whisper-1">
        </div>
        <div>
          <label>TTS Model</label>
          <input type="text" id="ttsModel" placeholder="tts-1">
        </div>
      </div>
      <label>Robot Persona / System Prompt</label>
      <textarea id="systemPrompt"></textarea>
      <button type="submit" class="btn-primary">Save OpenAI Configuration</button>
    </form>
    <button type="button" id="testBtn" class="btn-secondary">Test Servos &amp; RGB LEDs</button>
    <div id="status"></div>
  </div>
</div>
<script>
async function loadCfg() {
  try {
    const r = await fetch('/api/config');
    const d = await r.json();
    document.getElementById('keyBadge').textContent = 'API Key: ' + d.maskedKey + ' | Wake Word: ' + (d.wakeWord || 'StackChan');
    document.getElementById('wakeWord').value = d.wakeWord || 'StackChan';
    document.getElementById('baseUrl').value = d.baseUrl || 'https://api.openai.com/v1';
    document.getElementById('chatModel').value = d.chatModel || 'gpt-4o-mini';
    document.getElementById('ttsVoice').value = d.ttsVoice || 'nova';
    document.getElementById('sttModel').value = d.sttModel || 'whisper-1';
    document.getElementById('ttsModel').value = d.ttsModel || 'tts-1';
    document.getElementById('systemPrompt').value = d.systemPrompt || '';
  } catch (e) {
    document.getElementById('status').textContent = 'Failed to load config';
  }
}
document.getElementById('cfgForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  const st = document.getElementById('status');
  st.textContent = 'Saving...';
  const payload = {
    wakeWord: document.getElementById('wakeWord').value.trim(),
    apiKey: document.getElementById('apiKey').value.trim(),
    baseUrl: document.getElementById('baseUrl').value.trim(),
    chatModel: document.getElementById('chatModel').value,
    ttsVoice: document.getElementById('ttsVoice').value,
    sttModel: document.getElementById('sttModel').value.trim(),
    ttsModel: document.getElementById('ttsModel').value.trim(),
    systemPrompt: document.getElementById('systemPrompt').value.trim()
  };
  const r = await fetch('/api/config', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify(payload)
  });
  const res = await r.json();
  st.textContent = res.message || 'Saved!';
  document.getElementById('apiKey').value = '';
  loadCfg();
});
document.getElementById('testBtn').addEventListener('click', async () => {
  const st = document.getElementById('status');
  st.textContent = 'Testing servos and RGB LEDs on StackChan...';
  await fetch('/api/test_hardware', {method:'POST'});
  st.textContent = 'Servo nod + RGB LED test triggered!';
});
loadCfg();
</script>
</body>
</html>
)rawliteral";

static esp_err_t handle_get_root(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kWebPortalHtml, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_get_config(httpd_req_t* req)
{
    auto cfg    = get_config();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "maskedKey", mask_api_key(cfg.apiKey).c_str());
    cJSON_AddBoolToObject(root, "hasKey", has_api_key());
    cJSON_AddStringToObject(root, "wakeWord", cfg.wakeWord.c_str());
    cJSON_AddStringToObject(root, "baseUrl", cfg.baseUrl.c_str());
    cJSON_AddStringToObject(root, "chatModel", cfg.chatModel.c_str());
    cJSON_AddStringToObject(root, "sttModel", cfg.sttModel.c_str());
    cJSON_AddStringToObject(root, "ttsModel", cfg.ttsModel.c_str());
    cJSON_AddStringToObject(root, "ttsVoice", cfg.ttsVoice.c_str());
    cJSON_AddStringToObject(root, "systemPrompt", cfg.systemPrompt.c_str());

    char* json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json_str ? json_str : "{}", HTTPD_RESP_USE_STRLEN);
    if (json_str) cJSON_free(json_str);
    cJSON_Delete(root);
    return err;
}

static esp_err_t handle_post_config(httpd_req_t* req)
{
    if (req->content_len <= 0 || req->content_len > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload size");
        return ESP_FAIL;
    }

    std::string body(req->content_len, '\0');
    int received = 0;
    while (received < static_cast<int>(req->content_len)) {
        int ret = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (ret <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read body");
            return ESP_FAIL;
        }
        received += ret;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    auto cfg = get_config();
    if (auto* item = cJSON_GetObjectItem(root, "wakeWord"); cJSON_IsString(item) && item->valuestring[0] != '\0') {
        cfg.wakeWord = item->valuestring;
        set_wake_word(cfg.wakeWord);
    }
    if (auto* item = cJSON_GetObjectItem(root, "apiKey"); cJSON_IsString(item) && item->valuestring[0] != '\0') {
        cfg.apiKey = item->valuestring;
    }
    if (auto* item = cJSON_GetObjectItem(root, "baseUrl"); cJSON_IsString(item) && item->valuestring[0] != '\0') {
        cfg.baseUrl = item->valuestring;
    }
    if (auto* item = cJSON_GetObjectItem(root, "chatModel"); cJSON_IsString(item) && item->valuestring[0] != '\0') {
        cfg.chatModel = item->valuestring;
    }
    if (auto* item = cJSON_GetObjectItem(root, "sttModel"); cJSON_IsString(item) && item->valuestring[0] != '\0') {
        cfg.sttModel = item->valuestring;
    }
    if (auto* item = cJSON_GetObjectItem(root, "ttsModel"); cJSON_IsString(item) && item->valuestring[0] != '\0') {
        cfg.ttsModel = item->valuestring;
    }
    if (auto* item = cJSON_GetObjectItem(root, "ttsVoice"); cJSON_IsString(item) && item->valuestring[0] != '\0') {
        cfg.ttsVoice = item->valuestring;
    }
    if (auto* item = cJSON_GetObjectItem(root, "systemPrompt"); cJSON_IsString(item) && item->valuestring[0] != '\0') {
        cfg.systemPrompt = item->valuestring;
    }

    set_config(cfg);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, R"({"ok":true,"message":"OpenAI settings saved to StackChan NVS!"})",
                           HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_post_test_hardware(httpd_req_t* req)
{
    {
        LvglLockGuard lock;
        auto& stackchan = GetStackChan();
        stackchan.leftNeonLight().setColor(0, 168, 120);
        stackchan.rightNeonLight().setColor(0, 120, 168);
        GetHAL().showRgbColor(0, 140, 100);
        stackchan.motion().pitchServo().moveWithSpeed(250, 300);
        stackchan.motion().yawServo().moveWithSpeed(250, 300);
    }
    GetHAL().delay(400);
    {
        LvglLockGuard lock;
        auto& stackchan = GetStackChan();
        stackchan.motion().yawServo().moveWithSpeed(-250, 300);
    }
    GetHAL().delay(400);
    {
        LvglLockGuard lock;
        auto& stackchan = GetStackChan();
        stackchan.motion().goHome(250);
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, R"({"ok":true})", HTTPD_RESP_USE_STRLEN);
}

bool start_web_config_server()
{
    if (_http_server != nullptr) {
        return true;
    }

    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 80;
    config.stack_size       = 8192;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;

    if (httpd_start(&_http_server, &config) != ESP_OK) {
        mclog::tagError(_tag, "Failed to start OpenAI web config server");
        _http_server = nullptr;
        return false;
    }

    static const httpd_uri_t uri_root = {
        .uri = "/", .method = HTTP_GET, .handler = handle_get_root, .user_ctx = nullptr};
    static const httpd_uri_t uri_get_cfg = {
        .uri = "/api/config", .method = HTTP_GET, .handler = handle_get_config, .user_ctx = nullptr};
    static const httpd_uri_t uri_post_cfg = {
        .uri = "/api/config", .method = HTTP_POST, .handler = handle_post_config, .user_ctx = nullptr};
    static const httpd_uri_t uri_post_test = {
        .uri = "/api/test_hardware", .method = HTTP_POST, .handler = handle_post_test_hardware, .user_ctx = nullptr};

    httpd_register_uri_handler(_http_server, &uri_root);
    httpd_register_uri_handler(_http_server, &uri_get_cfg);
    httpd_register_uri_handler(_http_server, &uri_post_cfg);
    httpd_register_uri_handler(_http_server, &uri_post_test);

    mclog::tagInfo(_tag, "OpenAI web config server started at {}", get_web_config_url());
    return true;
}

void stop_web_config_server()
{
    if (_http_server != nullptr) {
        httpd_stop(_http_server);
        _http_server = nullptr;
        mclog::tagInfo(_tag, "OpenAI web config server stopped");
    }
}

bool is_web_config_server_running()
{
    return _http_server != nullptr;
}

std::string get_web_config_url()
{
    auto ip = WifiManager::GetInstance().GetIpAddress();
    if (ip.empty()) {
        return "http://stackchan.local/";
    }
    return "http://" + ip + "/";
}

}  // namespace hal_openai
