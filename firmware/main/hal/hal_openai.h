/*
 * SPDX-FileCopyrightText: 2026 StackChan-GPT Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <string>
#include <string_view>

struct OpenaiConfig_t {
    std::string apiKey;
    std::string baseUrl;
    std::string chatModel;
    std::string sttModel;
    std::string ttsModel;
    std::string ttsVoice;
    std::string systemPrompt;
    std::string wakeWord;
};

namespace hal_openai {

OpenaiConfig_t get_config();
// Raw NVS values without compiled-in defaults; unset fields are empty.
OpenaiConfig_t get_stored_config();
void set_config(const OpenaiConfig_t& config);
std::string get_wake_word();
void set_wake_word(std::string_view wake_word);
bool has_api_key();
std::string mask_api_key(std::string_view key);

void apply_boot_defaults();

void set_custom_led_active(bool active);
bool is_custom_led_active();

void set_thinking_led_active(bool active);
bool is_thinking_led_active();
void update_thinking_heartbeat_leds();

void notify_voice_playback_frame();
bool is_voice_playback_active();

bool start_web_config_server();
void stop_web_config_server();
bool is_web_config_server_running();
std::string get_web_config_url();
void set_last_web_search(const std::string& result);

}  // namespace hal_openai
