/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <mooncake_log.h>
#include <hal/hal.h>
#include <hal/hal_openai.h>
#include <apps/common/loading_page/loading_page.h>
#include <array>
#include <vector>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

static std::string _tag = "Setup-AgentAI";

namespace {
static const std::array<const char*, 4> _idle_motion_level_labels = {{"Off", "Low", "Medium", "High"}};
static const std::array<const char*, 11> _openai_voices = {
    {"marin", "cedar", "nova", "alloy", "shimmer", "echo", "fable", "onyx", "coral", "ash", "sage"}};
static const std::array<const char*, 8> _openai_models = {
    {"gpt-live", "gpt-5.4-nano", "gpt-5.5", "gpt-5.6-terra", "gpt-4o-mini", "gpt-4o", "gpt-4.1-mini", "gpt-4.1"}};

}  // namespace

OpenaiSetupWorker::OpenaiSetupWorker()
{
    mclog::tagInfo(_tag, "OpenaiSetupWorker start");

    {
        auto loading_page = std::make_unique<view::LoadingPage>(0xEDF4FF, 0x26206A);
        GetHAL().lvglUnlock();

        GetHAL().startNetwork([&](std::string_view msg) {
            LvglLockGuard lock;
            loading_page->setMessage(msg);
        });

        hal_openai::start_web_config_server();
        GetHAL().lvglLock();
    }

    auto cfg = hal_openai::get_config();
    for (size_t i = 0; i < _openai_voices.size(); ++i) {
        if (cfg.ttsVoice == _openai_voices[i]) {
            _voice_index = i;
            break;
        }
    }
    for (size_t i = 0; i < _openai_models.size(); ++i) {
        if (cfg.chatModel == _openai_models[i]) {
            _model_index = i;
            break;
        }
    }

    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(0xEDF4FF));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);
    _panel->setPadding(0, 40, 12, 12);
    _panel->setScrollDir(LV_DIR_VER);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

    _card_info = std::make_unique<Container>(_panel->get());
    _card_info->setSize(296, 174);
    _card_info->align(LV_ALIGN_TOP_MID, 0, 16);
    _card_info->setBgColor(lv_color_hex(0xD2E3FF));
    _card_info->setBorderWidth(0);
    _card_info->setRadius(18);
    _card_info->setPadding(0, 0, 0, 0);
    _card_info->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_title = std::make_unique<Label>(_card_info->get());
    _label_title->setText("Agent.ai — OpenAI Config");
    _label_title->setTextFont(&lv_font_montserrat_16);
    _label_title->setTextColor(lv_color_hex(0x26206A));
    _label_title->align(LV_ALIGN_TOP_MID, 0, 10);

    _qrcode_portal = std::make_unique<Qrcode>(_card_info->get());
    _qrcode_portal->setSize(86);
    _qrcode_portal->setDarkColor(lv_color_hex(0x221C5B));
    _qrcode_portal->setLightColor(lv_color_hex(0xD2E3FF));
    _qrcode_portal->update(hal_openai::get_web_config_url());
    _qrcode_portal->align(LV_ALIGN_LEFT_MID, 14, 16);

    _label_key_status = std::make_unique<Label>(_card_info->get());
    _label_key_status->setTextFont(&lv_font_montserrat_14);
    _label_key_status->setTextColor(lv_color_hex(0x26206A));
    _label_key_status->setWidth(176);
    _label_key_status->align(LV_ALIGN_TOP_LEFT, 110, 42);

    _label_url = std::make_unique<Label>(_card_info->get());
    _label_url->setTextFont(&lv_font_montserrat_14);
    _label_url->setTextColor(lv_color_hex(0x134233));
    _label_url->setWidth(176);
    _label_url->align(LV_ALIGN_TOP_LEFT, 110, 96);

    _btn_voice = std::make_unique<Button>(_panel->get());
    apply_button_common_style(*_btn_voice);
    _btn_voice->align(LV_ALIGN_TOP_MID, 0, 202);
    _btn_voice->setSize(290, 44);
    _btn_voice->onClick().connect([this]() { _voice_clicked = true; });

    _btn_model = std::make_unique<Button>(_panel->get());
    apply_button_common_style(*_btn_model);
    _btn_model->align(LV_ALIGN_TOP_MID, 0, 256);
    _btn_model->setSize(290, 44);
    _btn_model->onClick().connect([this]() { _model_clicked = true; });

    _btn_back = std::make_unique<Button>(_panel->get());
    apply_button_common_style(*_btn_back);
    _btn_back->align(LV_ALIGN_TOP_MID, 0, 310);
    _btn_back->setSize(290, 46);
    _btn_back->setBgColor(lv_color_hex(0x615B9E));
    _btn_back->label().setText("Done");
    _btn_back->onClick().connect([this]() { _back_clicked = true; });

    refresh_labels();
}

OpenaiSetupWorker::~OpenaiSetupWorker()
{
}

void OpenaiSetupWorker::refresh_labels()
{
    auto cfg = hal_openai::get_config();
    _label_key_status->setText(
        fmt::format("Name: {}\nKey: {}", cfg.wakeWord, hal_openai::mask_api_key(cfg.apiKey)));
    _label_url->setText(fmt::format("Web Portal:\n{}", hal_openai::get_web_config_url()));
    _btn_voice->label().setText(fmt::format("Voice: {} (Tap)", _openai_voices[_voice_index]));
    _btn_model->label().setText(fmt::format("Model: {} (Tap)", _openai_models[_model_index]));
}

void OpenaiSetupWorker::update()
{
    if (_voice_clicked) {
        _voice_clicked = false;
        _voice_index   = (_voice_index + 1) % _openai_voices.size();
        auto cfg       = hal_openai::get_config();
        cfg.ttsVoice   = _openai_voices[_voice_index];
        hal_openai::set_config(cfg);
        refresh_labels();
    }

    if (_model_clicked) {
        _model_clicked = false;
        _model_index   = (_model_index + 1) % _openai_models.size();
        auto cfg       = hal_openai::get_config();
        cfg.chatModel  = _openai_models[_model_index];
        hal_openai::set_config(cfg);
        refresh_labels();
    }

    if (GetHAL().millis() - _last_poll > 2000) {
        _last_poll = GetHAL().millis();
        refresh_labels();
    }

    if (_back_clicked) {
        _back_clicked = false;
        _is_done      = true;
    }
}

XiaozhiPowerSavingWorker::XiaozhiPowerSavingWorker()
{
    mclog::info("XiaozhiPowerSavingWorker start");

    for (uint32_t seconds = 0; seconds <= 3600; seconds += 300) {
        _idle_shutdown_levels.push_back(seconds);
    }

    _config = GetHAL().getXiaozhiConfig();

    int current_index = static_cast<int>(_idle_shutdown_levels.size()) - 1;
    for (size_t i = 0; i < _idle_shutdown_levels.size(); ++i) {
        if (_idle_shutdown_levels[i] >= _config.idleShutdownTimeSeconds) {
            current_index = static_cast<int>(i);
            break;
        }
    }

    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(0xEDF4FF));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);
    _panel->setPadding(0, 50, 24, 18);
    _panel->setScrollDir(LV_DIR_VER);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

    _panel_idle_shutdown = std::make_unique<Container>(_panel->get());
    _panel_idle_shutdown->setSize(296, 148);
    _panel_idle_shutdown->align(LV_ALIGN_TOP_MID, 0, 20);
    _panel_idle_shutdown->setBgColor(lv_color_hex(0xD2E3FF));
    _panel_idle_shutdown->setBorderWidth(0);
    _panel_idle_shutdown->setRadius(18);
    _panel_idle_shutdown->setPadding(0, 0, 0, 0);
    _panel_idle_shutdown->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_idle_title = std::make_unique<Label>(_panel_idle_shutdown->get());
    _label_idle_title->setText("Automatically power off after being idle for:");
    _label_idle_title->setWidth(280);
    _label_idle_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_idle_title->setTextFont(&lv_font_montserrat_16);
    _label_idle_title->setTextColor(lv_color_hex(0x26206A));
    _label_idle_title->align(LV_ALIGN_TOP_MID, 0, 18);

    _label_idle_value = std::make_unique<Label>(_panel_idle_shutdown->get());
    _label_idle_value->setTextFont(&lv_font_montserrat_24);
    _label_idle_value->setTextColor(lv_color_hex(0x26206A));
    _label_idle_value->align(LV_ALIGN_TOP_MID, 0, 64);

    _slider_idle_shutdown = std::make_unique<Slider>(_panel_idle_shutdown->get());
    _slider_idle_shutdown->align(LV_ALIGN_TOP_MID, 0, 106);
    _slider_idle_shutdown->setRange(0, _idle_shutdown_levels.size() - 1);
    _slider_idle_shutdown->setSize(250, 18);
    _slider_idle_shutdown->setBgColor(lv_color_hex(0x615B9E), LV_PART_KNOB);
    _slider_idle_shutdown->setBgColor(lv_color_hex(0x615B9E), LV_PART_INDICATOR);
    _slider_idle_shutdown->setBgColor(lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    _slider_idle_shutdown->setBgOpa(255);
    _slider_idle_shutdown->setValue(current_index);
    _slider_idle_shutdown->onValueChanged().connect([this](int32_t value) { _pending_idle_index = value; });

    _panel_charging = std::make_unique<Container>(_panel->get());
    _panel_charging->setSize(296, 120);
    _panel_charging->align(LV_ALIGN_TOP_MID, 0, 188);
    _panel_charging->setBgColor(lv_color_hex(0xD2E3FF));
    _panel_charging->setBorderWidth(0);
    _panel_charging->setRadius(18);
    _panel_charging->setPadding(0, 0, 0, 0);
    _panel_charging->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_charging_title = std::make_unique<Label>(_panel_charging->get());
    _label_charging_title->setText("Allow auto power off while charging:");
    _label_charging_title->setTextFont(&lv_font_montserrat_16);
    _label_charging_title->setTextColor(lv_color_hex(0x26206A));
    _label_charging_title->setWidth(260);
    _label_charging_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_charging_title->align(LV_ALIGN_TOP_MID, 0, 18);

    _switch_charging = std::make_unique<Switch>(_panel_charging->get());
    _switch_charging->setSize(64, 36);
    _switch_charging->align(LV_ALIGN_TOP_MID, 0, 66);
    _switch_charging->setBgColor(lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    _switch_charging->setBgColor(lv_color_hex(0x615B9E),
                                 static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
    _switch_charging->setBgColor(lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    if (_config.allowShutdownWhenCharging) {
        _switch_charging->addState(LV_STATE_CHECKED);
    }

    _btn_confirm = std::make_unique<Button>(_panel->get());
    apply_button_common_style(*_btn_confirm);
    _btn_confirm->align(LV_ALIGN_TOP_MID, 0, 326);
    _btn_confirm->setSize(290, 50);
    _btn_confirm->label().setText("Confirm");
    _btn_confirm->onClick().connect([this]() { _confirm_flag = true; });

    update_idle_label();
}

void XiaozhiPowerSavingWorker::update()
{
    if (_pending_idle_index != -1) {
        _config.idleShutdownTimeSeconds = _idle_shutdown_levels[_pending_idle_index];
        _pending_idle_index             = -1;
        update_idle_label();
    }

    if (_confirm_flag) {
        _confirm_flag                     = false;
        _config.allowShutdownWhenCharging = _switch_charging->getValue();
        GetHAL().setXiaozhiConfig(_config);
        mclog::tagInfo(_tag, "xiaozhi config updated: idleShutdownTimeSeconds={}, allowShutdownWhenCharging={}",
                       _config.idleShutdownTimeSeconds, _config.allowShutdownWhenCharging);
        _is_done = true;
    }
}

void XiaozhiPowerSavingWorker::update_idle_label()
{
    if (_config.idleShutdownTimeSeconds == 0) {
        _label_idle_value->setText("Off");
        return;
    }

    auto total_minutes = _config.idleShutdownTimeSeconds / 60;
    _label_idle_value->setText(fmt::format("{} min", total_minutes));
}

XiaozhiGeneralWorker::XiaozhiGeneralWorker()
{
    mclog::info("XiaozhiGeneralWorker start");

    _config = GetHAL().getXiaozhiConfig();

    for (uint8_t level = 0; level < _idle_motion_level_labels.size(); ++level) {
        _idle_motion_levels.push_back(level);
    }

    int current_index = static_cast<int>(_idle_motion_levels.size()) - 1;
    for (size_t i = 0; i < _idle_motion_levels.size(); ++i) {
        if (_idle_motion_levels[i] >= _config.idleRandomMovementLevel) {
            current_index = static_cast<int>(i);
            break;
        }
    }

    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(0xEDF4FF));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);
    _panel->setPadding(0, 50, 24, 18);
    _panel->setScrollDir(LV_DIR_VER);
    _panel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

    _panel_general = std::make_unique<Container>(_panel->get());
    _panel_general->setSize(296, 156);
    _panel_general->align(LV_ALIGN_TOP_MID, 0, 20);
    _panel_general->setBgColor(lv_color_hex(0xD2E3FF));
    _panel_general->setBorderWidth(0);
    _panel_general->setRadius(18);
    _panel_general->setPadding(0, 0, 0, 0);
    _panel_general->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_idle_motion_title = std::make_unique<Label>(_panel_general->get());
    _label_idle_motion_title->setText("Idle movement frequency:");
    _label_idle_motion_title->setTextFont(&lv_font_montserrat_16);
    _label_idle_motion_title->setTextColor(lv_color_hex(0x26206A));
    _label_idle_motion_title->setWidth(260);
    _label_idle_motion_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_idle_motion_title->align(LV_ALIGN_TOP_MID, 0, 18);

    _label_idle_motion_value = std::make_unique<Label>(_panel_general->get());
    _label_idle_motion_value->setTextFont(&lv_font_montserrat_24);
    _label_idle_motion_value->setTextColor(lv_color_hex(0x26206A));
    _label_idle_motion_value->align(LV_ALIGN_TOP_MID, 0, 64);

    _slider_idle_motion = std::make_unique<Slider>(_panel_general->get());
    _slider_idle_motion->align(LV_ALIGN_TOP_MID, 0, 118);
    _slider_idle_motion->setRange(0, _idle_motion_levels.size() - 1);
    _slider_idle_motion->setSize(250, 18);
    _slider_idle_motion->setBgColor(lv_color_hex(0x615B9E), LV_PART_KNOB);
    _slider_idle_motion->setBgColor(lv_color_hex(0x615B9E), LV_PART_INDICATOR);
    _slider_idle_motion->setBgColor(lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    _slider_idle_motion->setBgOpa(255);
    _slider_idle_motion->setValue(current_index);
    _slider_idle_motion->onValueChanged().connect([this](int32_t value) { _pending_idle_motion_index = value; });

    _panel_startup = std::make_unique<Container>(_panel->get());
    _panel_startup->setSize(296, 120);
    _panel_startup->align(LV_ALIGN_TOP_MID, 0, 196);
    _panel_startup->setBgColor(lv_color_hex(0xD2E3FF));
    _panel_startup->setBorderWidth(0);
    _panel_startup->setRadius(18);
    _panel_startup->setPadding(0, 0, 0, 0);
    _panel_startup->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_startup_title = std::make_unique<Label>(_panel_startup->get());
    _label_startup_title->setText("Start Agent.ai on boot:");
    _label_startup_title->setTextFont(&lv_font_montserrat_16);
    _label_startup_title->setTextColor(lv_color_hex(0x26206A));
    _label_startup_title->setWidth(260);
    _label_startup_title->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _label_startup_title->align(LV_ALIGN_TOP_MID, 0, 18);

    _switch_start_ai_on_boot = std::make_unique<Switch>(_panel_startup->get());
    _switch_start_ai_on_boot->setSize(64, 36);
    _switch_start_ai_on_boot->align(LV_ALIGN_TOP_MID, 0, 66);
    _switch_start_ai_on_boot->setBgColor(lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    _switch_start_ai_on_boot->setBgColor(lv_color_hex(0x615B9E),
                                         static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
    _switch_start_ai_on_boot->setBgColor(lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    if (_config.startAiAgentOnBoot) {
        _switch_start_ai_on_boot->addState(LV_STATE_CHECKED);
    }

    _btn_confirm = std::make_unique<Button>(_panel->get());
    apply_button_common_style(*_btn_confirm);
    _btn_confirm->align(LV_ALIGN_TOP_MID, 0, 326);
    _btn_confirm->setSize(290, 50);
    _btn_confirm->label().setText("Confirm");
    _btn_confirm->onClick().connect([this]() { _confirm_flag = true; });

    update_idle_motion_label();
}

void XiaozhiGeneralWorker::update()
{
    if (_pending_idle_motion_index != -1) {
        _config.idleRandomMovementLevel = _idle_motion_levels[_pending_idle_motion_index];
        _pending_idle_motion_index      = -1;
        update_idle_motion_label();
    }

    if (_confirm_flag) {
        _confirm_flag              = false;
        _config.startAiAgentOnBoot = _switch_start_ai_on_boot->getValue();
        GetHAL().setXiaozhiConfig(_config);
        mclog::tagInfo(_tag, "xiaozhi config updated: idleRandomMovementLevel={} ({})", _config.idleRandomMovementLevel,
                       _idle_motion_level_labels[_config.idleRandomMovementLevel]);
        _is_done = true;
    }
}

void XiaozhiGeneralWorker::update_idle_motion_label()
{
    _label_idle_motion_value->setText(_idle_motion_level_labels[_config.idleRandomMovementLevel]);
}
