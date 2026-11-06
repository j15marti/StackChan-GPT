/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <src/misc/lv_area.h>
#include <src/misc/lv_text.h>
#include <stackchan/stackchan.h>
#include <ArduinoJson.hpp>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <hal/hal_openai.h>
#include <ssid_manager.h>
#include <memory>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;
using namespace stackchan;

static std::string _tag = "Setup-Connectivity";

WifiSetupWorker::WifiSetupWorker()
{
    _state       = State::AppDownload;
    _last_state  = State::None;
    _is_first_in = true;

    // Create default avatar
    auto avatar = std::make_unique<avatar::DefaultAvatar>();
    avatar->init(lv_screen_active(), &lv_font_montserrat_24);
    avatar->leftEye().setVisible(false);
    avatar->rightEye().setVisible(false);
    avatar->mouth().setVisible(false);
    GetStackChan().attachAvatar(std::move(avatar));
}

WifiSetupWorker::~WifiSetupWorker()
{
    cleanup_ui();
    _state_app_download_data.reset();
    _state_wait_app_connection_data.reset();
    _state_touch_keyboard_data.reset();
    _state_name_keyboard_data.reset();
    if (_app_config_signal_id >= 0) {
        GetHAL().onAppConfigEvent.disconnect(_app_config_signal_id);
    }
    GetStackChan().resetAvatar();
}

void WifiSetupWorker::update()
{
    cleanup_ui();
    update_state();
}

void WifiSetupWorker::update_state()
{
    switch (_state) {
        case State::AppDownload: {
            if (_is_first_in) {
                _is_first_in = false;

                auto& data = _state_app_download_data;

                data.panel = std::make_unique<Container>(lv_screen_active());
                data.panel->setBgColor(lv_color_hex(0xEDF4FF));
                data.panel->align(LV_ALIGN_CENTER, 0, 0);
                data.panel->setBorderWidth(0);
                data.panel->setSize(320, 240);
                data.panel->setRadius(0);

                data.title = std::make_unique<Label>(lv_screen_active());
                data.title->setTextFont(&lv_font_montserrat_20);
                data.title->setTextColor(lv_color_hex(0x7E7B9C));
                data.title->align(LV_ALIGN_TOP_MID, 0, 0);
                data.title->setText("WI-FI SETUP");

                data.info = std::make_unique<Label>(lv_screen_active());
                data.info->setTextFont(&lv_font_montserrat_14);
                data.info->setTextColor(lv_color_hex(0x26206A));
                data.info->align(LV_ALIGN_TOP_MID, 0, 25);
                data.info->setTextAlign(LV_TEXT_ALIGN_CENTER);
                data.info->setText("Type Wi-Fi on screen (Keyboard)\nor optional BLE App setup:");

                std::string qrcode_text = "https://apps.apple.com/us/app/stackchan-world/id6756086326";
                data.qrcode_ios         = std::make_unique<Qrcode>(lv_screen_active());
                data.qrcode_ios->setSize(74);
                data.qrcode_ios->setDarkColor(lv_color_hex(0x221C5B));
                data.qrcode_ios->setLightColor(lv_color_hex(0xEDF4FF));
                data.qrcode_ios->update(qrcode_text);
                data.qrcode_ios->align(LV_ALIGN_CENTER, -65, -14);

                qrcode_text         = "https://play.google.com/store/apps/details?id=com.m5stack.stackchan";
                data.qrcode_android = std::make_unique<Qrcode>(lv_screen_active());
                data.qrcode_android->setSize(74);
                data.qrcode_android->setDarkColor(lv_color_hex(0x221C5B));
                data.qrcode_android->setLightColor(lv_color_hex(0xEDF4FF));
                data.qrcode_android->update(qrcode_text);
                data.qrcode_android->align(LV_ALIGN_CENTER, 65, -14);

                data.label_ios = std::make_unique<Label>(lv_screen_active());
                data.label_ios->setTextFont(&lv_font_montserrat_14);
                data.label_ios->setTextColor(lv_color_hex(0x26206A));
                data.label_ios->align(LV_ALIGN_CENTER, -65, 42);
                data.label_ios->setText("App Store\n(iOS)");
                data.label_ios->setTextAlign(LV_TEXT_ALIGN_CENTER);

                data.label_android = std::make_unique<Label>(lv_screen_active());
                data.label_android->setTextFont(&lv_font_montserrat_14);
                data.label_android->setTextColor(lv_color_hex(0x26206A));
                data.label_android->align(LV_ALIGN_CENTER, 65, 42);
                data.label_android->setText("Play Store\n(Android)");
                data.label_android->setTextAlign(LV_TEXT_ALIGN_CENTER);

                data.btn_quit = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.btn_quit);
                data.btn_quit->align(LV_ALIGN_CENTER, -104, 92);
                data.btn_quit->setSize(86, 40);
                data.btn_quit->setBgColor(lv_color_hex(0xD4D9E0));
                data.btn_quit->label().setText("Skip");
                data.btn_quit->label().setTextFont(&lv_font_montserrat_16);
                data.btn_quit->label().setTextColor(lv_color_hex(0x525064));
                data.btn_quit->onClick().connect([this]() { _state_app_download_data.quit_clicked = true; });

                data.btn_keyboard = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.btn_keyboard);
                data.btn_keyboard->align(LV_ALIGN_CENTER, 0, 92);
                data.btn_keyboard->setSize(106, 40);
                data.btn_keyboard->setBgColor(lv_color_hex(0x10B981));
                data.btn_keyboard->label().setText("Keyboard");
                data.btn_keyboard->label().setTextFont(&lv_font_montserrat_16);
                data.btn_keyboard->label().setTextColor(lv_color_hex(0xFFFFFF));
                data.btn_keyboard->onClick().connect([this]() { _state_app_download_data.keyboard_clicked = true; });

                data.btn_next = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.btn_next);
                data.btn_next->align(LV_ALIGN_CENTER, 104, 92);
                data.btn_next->setSize(86, 40);
                data.btn_next->label().setText("BLE App");
                data.btn_next->label().setTextFont(&lv_font_montserrat_16);
                data.btn_next->onClick().connect([this]() { _state_app_download_data.next_clicked = true; });
            }

            if (_state_app_download_data.quit_clicked) {
                GetHAL().setAppConfiged(true);
                _is_done = true;
            }

            if (_state_app_download_data.keyboard_clicked) {
                switch_state(State::TouchKeyboard);
            }

            if (_state_app_download_data.next_clicked) {
                switch_state(State::WaitAppConnection);
            }

            if (_last_app_config_event != AppConfigEvent::None) {
                if (_last_app_config_event == AppConfigEvent::AppConnected) {
                    switch_state(State::AppConnected);
                }
                _last_app_config_event = AppConfigEvent::None;
            }

            break;
        }
        case State::WaitAppConnection: {
            if (_is_first_in) {
                _is_first_in = false;

                if (_app_config_signal_id < 0) {
                    _app_config_signal_id = GetHAL().onAppConfigEvent.connect(
                        [this](AppConfigEvent event) { _last_app_config_event = event; });
                }

                GetHAL().startAppConfigServer();

                auto& data = _state_wait_app_connection_data;

                data.panel = std::make_unique<Container>(lv_screen_active());
                data.panel->setBgColor(lv_color_hex(0xEDF4FF));
                data.panel->align(LV_ALIGN_CENTER, 0, 0);
                data.panel->setBorderWidth(0);
                data.panel->setSize(320, 240);
                data.panel->setRadius(0);

                data.btn_id = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.btn_id);
                data.btn_id->align(LV_ALIGN_TOP_MID, 0, 18);
                data.btn_id->setSize(262, 46);
                data.btn_id->onClick().connect([]() {
                    auto& avatar = GetStackChan().avatar();
                    avatar.clearDecorators();
                    avatar.addDecorator(std::make_unique<avatar::HeartDecorator>(lv_screen_active(), 3000));
                });
                data.btn_id->label().setText(fmt::format("ID: {}", GetHAL().getFactoryMacString()));

                data.info = std::make_unique<Label>(lv_screen_active());
                data.info->setTextFont(&lv_font_montserrat_20);
                data.info->setTextColor(lv_color_hex(0x26206A));
                data.info->align(LV_ALIGN_CENTER, 0, 8);
                data.info->setTextAlign(LV_TEXT_ALIGN_CENTER);
                data.info->setText("Look for me in the app\nor use touch keyboard:");

                data.btn_skip = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.btn_skip);
                data.btn_skip->align(LV_ALIGN_BOTTOM_MID, -74, -12);
                data.btn_skip->setSize(128, 40);
                data.btn_skip->setBgColor(lv_color_hex(0xD4D9E0));
                data.btn_skip->label().setText("Skip Pairing");
                data.btn_skip->label().setTextFont(&lv_font_montserrat_16);
                data.btn_skip->label().setTextColor(lv_color_hex(0x525064));
                data.btn_skip->onClick().connect([this]() { _state_wait_app_connection_data.skip_clicked = true; });

                data.btn_keyboard = std::make_unique<Button>(lv_screen_active());
                apply_button_common_style(*data.btn_keyboard);
                data.btn_keyboard->align(LV_ALIGN_BOTTOM_MID, 74, -12);
                data.btn_keyboard->setSize(128, 40);
                data.btn_keyboard->setBgColor(lv_color_hex(0x10B981));
                data.btn_keyboard->label().setText("Keyboard");
                data.btn_keyboard->label().setTextFont(&lv_font_montserrat_16);
                data.btn_keyboard->label().setTextColor(lv_color_hex(0xFFFFFF));
                data.btn_keyboard->onClick().connect(
                    [this]() { _state_wait_app_connection_data.keyboard_clicked = true; });

                auto& avatar = GetStackChan().avatar();
                avatar.clearDecorators();
                avatar.addDecorator(std::make_unique<avatar::HeartDecorator>(lv_screen_active(), 3000));
            }

            if (_state_wait_app_connection_data.skip_clicked) {
                GetHAL().setAppConfiged(true);
                _is_done = true;
                break;
            }

            if (_state_wait_app_connection_data.keyboard_clicked) {
                switch_state(State::TouchKeyboard);
                break;
            }

            if (_last_app_config_event != AppConfigEvent::None) {
                if (_last_app_config_event == AppConfigEvent::AppConnected) {
                    switch_state(State::AppConnected);
                }
                _last_app_config_event = AppConfigEvent::None;
            }

            break;
        }
        case State::TouchKeyboard: {
            if (_is_first_in) {
                _is_first_in = false;

                if (_app_config_signal_id < 0) {
                    _app_config_signal_id = GetHAL().onAppConfigEvent.connect(
                        [this](AppConfigEvent event) { _last_app_config_event = event; });
                }
                GetHAL().startAppConfigServer();

                auto& data = _state_touch_keyboard_data;

                data.panel = std::make_unique<Container>(lv_screen_active());
                data.panel->setBgColor(lv_color_hex(0xEDF4FF));
                data.panel->align(LV_ALIGN_CENTER, 0, 0);
                data.panel->setBorderWidth(0);
                data.panel->setSize(320, 240);
                data.panel->setRadius(0);
                data.panel->setPadding(0, 0, 0, 0);
                data.panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

                data.btn_back = std::make_unique<Button>(data.panel->get());
                apply_button_common_style(*data.btn_back);
                data.btn_back->align(LV_ALIGN_TOP_LEFT, 6, 4);
                data.btn_back->setSize(62, 28);
                data.btn_back->setRadius(8);
                data.btn_back->setBgColor(lv_color_hex(0xD4D9E0));
                data.btn_back->label().setText("Back");
                data.btn_back->label().setTextFont(&lv_font_montserrat_14);
                data.btn_back->label().setTextColor(lv_color_hex(0x525064));
                data.btn_back->onClick().connect([this]() { _state_touch_keyboard_data.back_clicked = true; });

                data.btn_skip = std::make_unique<Button>(data.panel->get());
                apply_button_common_style(*data.btn_skip);
                data.btn_skip->align(LV_ALIGN_TOP_LEFT, 72, 4);
                data.btn_skip->setSize(62, 28);
                data.btn_skip->setRadius(8);
                data.btn_skip->setBgColor(lv_color_hex(0xB8D3FD));
                data.btn_skip->label().setText("Skip");
                data.btn_skip->label().setTextFont(&lv_font_montserrat_14);
                data.btn_skip->label().setTextColor(lv_color_hex(0x26206A));
                data.btn_skip->onClick().connect([this]() { _state_touch_keyboard_data.skip_clicked = true; });

                data.btn_connect = std::make_unique<Button>(data.panel->get());
                apply_button_common_style(*data.btn_connect);
                data.btn_connect->align(LV_ALIGN_TOP_RIGHT, -6, 4);
                data.btn_connect->setSize(96, 28);
                data.btn_connect->setRadius(8);
                data.btn_connect->setBgColor(lv_color_hex(0x10B981));
                data.btn_connect->label().setText("Connect");
                data.btn_connect->label().setTextFont(&lv_font_montserrat_14);
                data.btn_connect->label().setTextColor(lv_color_hex(0xFFFFFF));
                data.btn_connect->onClick().connect([this]() { _state_touch_keyboard_data.connect_clicked = true; });

                data.ta_ssid = std::make_unique<TextArea>(data.panel->get());
                data.ta_ssid->setOneLine(true);
                data.ta_ssid->setMaxLength(32);
                data.ta_ssid->setPlaceholderText("2.4G Wi-Fi SSID");
                data.ta_ssid->setSize(150, 34);
                data.ta_ssid->align(LV_ALIGN_TOP_LEFT, 6, 36);
                lv_obj_set_style_text_font(data.ta_ssid->get(), &lv_font_montserrat_14, LV_PART_MAIN);

                data.ta_password = std::make_unique<TextArea>(data.panel->get());
                data.ta_password->setOneLine(true);
                data.ta_password->setMaxLength(64);
                data.ta_password->setPlaceholderText("Password");
                data.ta_password->setSize(150, 34);
                data.ta_password->align(LV_ALIGN_TOP_RIGHT, -6, 36);
                lv_obj_set_style_text_font(data.ta_password->get(), &lv_font_montserrat_14, LV_PART_MAIN);

                const auto& ssid_list = SsidManager::GetInstance().GetSsidList();
                if (!ssid_list.empty()) {
                    data.ta_ssid->setText(ssid_list[0].ssid);
                    data.ta_password->setText(ssid_list[0].password);
                }

                data.label_status = std::make_unique<Label>(data.panel->get());
                data.label_status->setTextFont(&lv_font_montserrat_14);
                data.label_status->setTextColor(lv_color_hex(0x26206A));
                data.label_status->setWidth(308);
                data.label_status->setTextAlign(LV_TEXT_ALIGN_CENTER);
                data.label_status->align(LV_ALIGN_TOP_MID, 0, 73);
                data.label_status->setText("Tap SSID or Password box to type, then Connect");

                data.keyboard = lv_keyboard_create(data.panel->get());
                lv_obj_set_size(data.keyboard, 320, 145);
                lv_obj_align(data.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
                lv_keyboard_set_textarea(data.keyboard, data.ta_ssid->get());

                lv_obj_add_event_cb(
                    data.ta_ssid->get(),
                    [](lv_event_t* e) {
                        auto* kb = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
                        if (kb) lv_keyboard_set_textarea(kb, static_cast<lv_obj_t*>(lv_event_get_target(e)));
                    },
                    LV_EVENT_FOCUSED, data.keyboard);

                lv_obj_add_event_cb(
                    data.ta_password->get(),
                    [](lv_event_t* e) {
                        auto* kb = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
                        if (kb) lv_keyboard_set_textarea(kb, static_cast<lv_obj_t*>(lv_event_get_target(e)));
                    },
                    LV_EVENT_FOCUSED, data.keyboard);

                lv_obj_add_event_cb(
                    data.keyboard,
                    [](lv_event_t* e) {
                        auto* self = static_cast<WifiSetupWorker*>(lv_event_get_user_data(e));
                        if (self) {
                            self->_state_touch_keyboard_data.connect_clicked = true;
                        }
                    },
                    LV_EVENT_READY, this);
            }

            if (_state_touch_keyboard_data.back_clicked) {
                switch_state(State::AppDownload);
                break;
            }

            if (_state_touch_keyboard_data.skip_clicked) {
                switch_state(State::NameKeyboard);
                break;
            }

            if (_state_touch_keyboard_data.connect_clicked) {
                _state_touch_keyboard_data.connect_clicked = false;
                const char* ssid_cstr =
                    _state_touch_keyboard_data.ta_ssid
                        ? lv_textarea_get_text(_state_touch_keyboard_data.ta_ssid->get())
                        : "";
                const char* pass_cstr =
                    _state_touch_keyboard_data.ta_password
                        ? lv_textarea_get_text(_state_touch_keyboard_data.ta_password->get())
                        : "";
                std::string ssid = ssid_cstr ? ssid_cstr : "";
                std::string pass = pass_cstr ? pass_cstr : "";

                if (ssid.empty()) {
                    _state_touch_keyboard_data.label_status->setTextColor(lv_color_hex(0xDC2626));
                    _state_touch_keyboard_data.label_status->setText("Please enter a 2.4GHz Wi-Fi SSID first!");
                } else {
                    _state_touch_keyboard_data.label_status->setTextColor(lv_color_hex(0x2563EB));
                    _state_touch_keyboard_data.label_status->setText(fmt::format("Connecting to {} ...", ssid));
                    GetHAL().connectWifiDirect(ssid, pass);
                }
            }

            if (_last_app_config_event != AppConfigEvent::None) {
                if (_last_app_config_event == AppConfigEvent::TryWifiConnect) {
                    if (_state_touch_keyboard_data.label_status) {
                        _state_touch_keyboard_data.label_status->setTextColor(lv_color_hex(0x2563EB));
                        _state_touch_keyboard_data.label_status->setText("Connecting to Wi-Fi...");
                    }
                } else if (_last_app_config_event == AppConfigEvent::WifiConnectFailed) {
                    if (_state_touch_keyboard_data.label_status) {
                        _state_touch_keyboard_data.label_status->setTextColor(lv_color_hex(0xDC2626));
                        _state_touch_keyboard_data.label_status->setText("Connect failed! Check SSID/Password (2.4G).");
                    }
                } else if (_last_app_config_event == AppConfigEvent::WifiConnected) {
                    switch_state(State::NameKeyboard);
                }
                _last_app_config_event = AppConfigEvent::None;
            }

            break;
        }
        case State::NameKeyboard: {
            if (_is_first_in) {
                _is_first_in = false;

                auto& data = _state_name_keyboard_data;

                data.panel = std::make_unique<Container>(lv_screen_active());
                data.panel->setBgColor(lv_color_hex(0xEDF4FF));
                data.panel->align(LV_ALIGN_CENTER, 0, 0);
                data.panel->setBorderWidth(0);
                data.panel->setSize(320, 240);
                data.panel->setRadius(0);
                data.panel->setPadding(0, 0, 0, 0);
                data.panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

                data.label_title = std::make_unique<Label>(data.panel->get());
                data.label_title->setTextFont(&lv_font_montserrat_20);
                data.label_title->setTextColor(lv_color_hex(0x26206A));
                data.label_title->align(LV_ALIGN_TOP_LEFT, 10, 6);
                data.label_title->setText("What is my name?");

                data.btn_skip = std::make_unique<Button>(data.panel->get());
                apply_button_common_style(*data.btn_skip);
                data.btn_skip->align(LV_ALIGN_TOP_RIGHT, -84, 4);
                data.btn_skip->setSize(68, 28);
                data.btn_skip->setRadius(8);
                data.btn_skip->setBgColor(lv_color_hex(0xD4D9E0));
                data.btn_skip->label().setText("Skip");
                data.btn_skip->label().setTextFont(&lv_font_montserrat_14);
                data.btn_skip->label().setTextColor(lv_color_hex(0x525064));
                data.btn_skip->onClick().connect([this]() { _state_name_keyboard_data.skip_clicked = true; });

                data.btn_save = std::make_unique<Button>(data.panel->get());
                apply_button_common_style(*data.btn_save);
                data.btn_save->align(LV_ALIGN_TOP_RIGHT, -6, 4);
                data.btn_save->setSize(72, 28);
                data.btn_save->setRadius(8);
                data.btn_save->setBgColor(lv_color_hex(0x10B981));
                data.btn_save->label().setText("Save");
                data.btn_save->label().setTextFont(&lv_font_montserrat_14);
                data.btn_save->label().setTextColor(lv_color_hex(0xFFFFFF));
                data.btn_save->onClick().connect([this]() { _state_name_keyboard_data.save_clicked = true; });

                data.ta_name = std::make_unique<TextArea>(data.panel->get());
                data.ta_name->setOneLine(true);
                data.ta_name->setMaxLength(24);
                data.ta_name->setPlaceholderText("Enter wake word name (e.g. Toby)");
                data.ta_name->setSize(304, 34);
                data.ta_name->align(LV_ALIGN_TOP_MID, 0, 36);
                lv_obj_set_style_text_font(data.ta_name->get(), &lv_font_montserrat_16, LV_PART_MAIN);
                data.ta_name->setText(hal_openai::get_wake_word());

                data.label_subtitle = std::make_unique<Label>(data.panel->get());
                data.label_subtitle->setTextFont(&lv_font_montserrat_14);
                data.label_subtitle->setTextColor(lv_color_hex(0x475569));
                data.label_subtitle->setWidth(308);
                data.label_subtitle->setTextAlign(LV_TEXT_ALIGN_CENTER);
                data.label_subtitle->align(LV_ALIGN_TOP_MID, 0, 74);
                data.label_subtitle->setText("Say my name anytime to wake me up for AI chat!");

                data.keyboard = lv_keyboard_create(data.panel->get());
                lv_obj_set_size(data.keyboard, 320, 145);
                lv_obj_align(data.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
                lv_keyboard_set_textarea(data.keyboard, data.ta_name->get());

                lv_obj_add_event_cb(
                    data.keyboard,
                    [](lv_event_t* e) {
                        auto* self = static_cast<WifiSetupWorker*>(lv_event_get_user_data(e));
                        if (self) {
                            self->_state_name_keyboard_data.save_clicked = true;
                        }
                    },
                    LV_EVENT_READY, this);
            }

            if (_state_name_keyboard_data.skip_clicked) {
                GetHAL().setAppConfiged(true);
                switch_state(State::Done);
                break;
            }

            if (_state_name_keyboard_data.save_clicked) {
                _state_name_keyboard_data.save_clicked = false;
                const char* name_cstr =
                    _state_name_keyboard_data.ta_name
                        ? lv_textarea_get_text(_state_name_keyboard_data.ta_name->get())
                        : "";
                std::string name = name_cstr ? name_cstr : "";
                hal_openai::set_wake_word(name);
                GetHAL().setAppConfiged(true);
                switch_state(State::Done);
                break;
            }

            break;
        }
        case State::AppConnected: {
            if (_is_first_in) {
                _is_first_in = false;

                auto& avatar = GetStackChan().avatar();
                avatar.leftEye().setVisible(true);
                avatar.rightEye().setVisible(true);
                avatar.mouth().setVisible(true);
                avatar.setSpeech("Ready to Configure ~");

                GetStackChan().addModifier(std::make_unique<TimedEmotionModifier>(avatar::Emotion::Happy, 4000));
                GetStackChan().addModifier(std::make_unique<BreathModifier>());
                GetStackChan().addModifier(std::make_unique<BlinkModifier>());
                GetStackChan().addModifier(std::make_unique<SpeakingModifier>(2000, 180, false));
            }

            if (_last_app_config_event != AppConfigEvent::None) {
                if (_last_app_config_event == AppConfigEvent::AppDisconnected) {
                    switch_state(State::WaitAppConnection);
                } else if (_last_app_config_event == AppConfigEvent::TryWifiConnect) {
                    auto& avatar = GetStackChan().avatar();
                    avatar.setSpeech("Verifying...");
                    GetStackChan().addModifier(std::make_unique<SpeakingModifier>(2000, 180, false));
                } else if (_last_app_config_event == AppConfigEvent::WifiConnectFailed) {
                    GetStackChan().addModifier(std::make_unique<TimedEmotionModifier>(avatar::Emotion::Sad, 4000));
                    GetStackChan().addModifier(
                        std::make_unique<TimedSpeechModifier>("Connect Failed. Try again?", 6000));
                    GetStackChan().addModifier(std::make_unique<SpeakingModifier>(3000, 180, false));
                } else if (_last_app_config_event == AppConfigEvent::WifiConnected) {
                    switch_state(State::NameKeyboard);
                }
                _last_app_config_event = AppConfigEvent::None;
            }

            break;
        }
        case State::Done: {
            if (_is_first_in) {
                _is_first_in = false;

                auto& avatar = GetStackChan().avatar();
                avatar.leftEye().setVisible(true);
                avatar.rightEye().setVisible(true);
                avatar.mouth().setVisible(true);
                avatar.setEmotion(avatar::Emotion::Happy);

                GetStackChan().addModifier(std::make_unique<SpeakingModifier>(1500, 180, false));

                _state_done_data.reboot_count = 4;
            }

            if (GetHAL().millis() - _last_tick > 1000) {
                _last_tick = GetHAL().millis();
                if (_state_done_data.reboot_count > 0) {
                    _state_done_data.reboot_count--;
                    auto& avatar = GetStackChan().avatar();
                    avatar.setSpeech(fmt::format("Hi, I'm {}! Reboot in {}s.", hal_openai::get_wake_word(),
                                                 _state_done_data.reboot_count));
                } else {
                    mclog::tagInfo(_tag, "rebooting...");
                    GetHAL().delay(100);
                    GetHAL().reboot();
                }
            }

            break;
        }
        default:
            break;
    }
}

void WifiSetupWorker::cleanup_ui()
{
    if (_last_state == State::None) {
        return;
    }

    switch (_last_state) {
        case State::AppDownload: {
            _state_app_download_data.reset();
            break;
        }
        case State::WaitAppConnection: {
            _state_wait_app_connection_data.reset();
            break;
        }
        case State::TouchKeyboard: {
            _state_touch_keyboard_data.reset();
            break;
        }
        case State::NameKeyboard: {
            _state_name_keyboard_data.reset();
            break;
        }
        case State::AppConnected: {
            GetStackChan().avatar().setSpeech("");
            GetStackChan().clearModifiers();
            break;
        }
        case State::Done: {
            break;
        }
        default:
            break;
    }

    _last_state = State::None;
}

void WifiSetupWorker::switch_state(State newState)
{
    _last_state  = _state;
    _state       = newState;
    _is_first_in = true;
}
