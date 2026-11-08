/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "hal_openai.h"
#include "robot_dance.h"
#include <mooncake_log.h>
#include <mcp_server.h>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>
#include <algorithm>
#include <cctype>

using namespace stackchan;

static const std::string_view _tag = "HAL-MCP";
static int _gesture_modifier_id    = -1;

void Hal::xiaozhi_mcp_init()
{
    mclog::tagInfo(_tag, "init");

    // https://github.com/78/xiaozhi-esp32/blob/main/docs/mcp-usage.md
    auto& mcp_server = McpServer::GetInstance();

    mclog::tagInfo(_tag, "add robot.get_head_angles tool");
    mcp_server.AddTool("self.robot.get_head_angles",
                       "Returns current yaw/pitch servo angles in degrees. Neutral position is {yaw:0, pitch:0}.",
                       std::vector<Property>{}, [this](const PropertyList& properties) -> ReturnValue {
                           LvglLockGuard lock;  // StackChan motion update is under the lvgl lock

                           auto& motion      = GetStackChan().motion();
                           int current_yaw   = motion.yawServo().getCurrentAngle() / 10;
                           int current_pitch = motion.pitchServo().getCurrentAngle() / 10;

                           auto result = fmt::format(R"({{"yaw": {}, "pitch": {}}})", current_yaw, current_pitch);
                           mclog::tagInfo(_tag, "get_head_angles: {}", result);
                           return result;
                       });

    mclog::tagInfo(_tag, "add robot.set_head_angles tool");
    mcp_server.AddTool("self.robot.set_head_angles",
                       "Adjust head servo position. GUIDELINES: "
                       "1. For natural interaction, stay within +/- 45 degrees. "
                       "2. Only use values > 70 if the user explicitly asks to look far away/behind. "
                       "3. Max ranges: Yaw(-128 to 128, -128 as your left), Pitch(0 to 90, 90 as your up). "
                       "Speed(100-1000, 180 is natural).",
                       PropertyList({Property("yaw", kPropertyTypeInteger, -9999, -9999, 128),
                                     Property("pitch", kPropertyTypeInteger, -9999, -9999, 90),
                                     Property("speed", kPropertyTypeInteger, 180, 100, 1000)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int speed = properties["speed"].value<int>();
                           int yaw   = properties["yaw"].value<int>();
                           int pitch = properties["pitch"].value<int>();

                           mclog::tagInfo(_tag, "motion set_angles: yaw: {}, pitch: {}, speed: {}", yaw, pitch, speed);

                           LvglLockGuard lock;

                           auto& motion = GetStackChan().motion();
                           if (pitch != -9999) {
                               motion.pitchServo().moveWithSpeed(pitch * 10, speed);
                           }
                           if (yaw != -9999) {
                               motion.yawServo().moveWithSpeed(yaw * 10, speed);
                           }

                           return true;
                       });

    mclog::tagInfo(_tag, "add robot.perform_gesture tool");
    mcp_server.AddTool(
        "self.robot.perform_gesture",
        "Perform an expressive servo and facial gesture on StackChan. "
        "Supported gesture values: 'nod' (agree/yes), 'shake' (disagree/no), 'dance' (beat-synced music dance), "
        "'happy_dance' (happy sway), 'robot_dance' (robotic moves), 'look_around' (scan room), "
        "'panic' (surprised shake), 'center' (reset pose). Prefer self.robot.dance for dancing to music.",
        PropertyList({Property("gesture", kPropertyTypeString, std::string("nod"))}),
        [this](const PropertyList& properties) -> ReturnValue {
            std::string gesture = properties["gesture"].value<std::string>();
            std::transform(gesture.begin(), gesture.end(), gesture.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            mclog::tagInfo(_tag, "perform_gesture: {}", gesture);

            LvglLockGuard lock;
            auto& sc = GetStackChan();
            if (_gesture_modifier_id >= 0) {
                sc.removeModifier(_gesture_modifier_id);
                _gesture_modifier_id = -1;
            }

            if (gesture == "nod" || gesture == "yes") {
                static const animation::KeyframeSequence kNodSeq = {
                    {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 30}, {0, 250}, {320, 350}, 350},
                    {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {0, 250}, {50, 350}, 350},
                    {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 30}, {0, 250}, {300, 350}, 350},
                    {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {0, 250}, {120, 250}, 400},
                };
                _gesture_modifier_id = sc.addModifier(std::make_unique<DanceModifier>(kNodSeq));
            } else if (gesture == "shake" || gesture == "no") {
                static const animation::KeyframeSequence kShakeSeq = {
                    {{-10, 0, 0, 100}, {-10, 0, 0, 100}, {0, 0, 0, 0}, {350, 400}, {100, 250}, 350},
                    {{10, 0, 0, 100}, {10, 0, 0, 100}, {0, 0, 0, 0}, {-350, 400}, {100, 250}, 400},
                    {{-10, 0, 0, 100}, {-10, 0, 0, 100}, {0, 0, 0, 0}, {280, 400}, {100, 250}, 350},
                    {{0, 0, 0, 100}, {0, 0, 0, 100}, {0, 0, 0, 0}, {0, 300}, {100, 250}, 400},
                };
                _gesture_modifier_id = sc.addModifier(std::make_unique<DanceModifier>(kShakeSeq));
            } else if (gesture == "dance" || gesture == "dance_to_music" || gesture == "music_dance") {
                // Beat-synced music dance (mic BPM + LED sequence); not the short Happy keyframe.
                return robot_dance::start(robot_dance::kDefaultDurationSec, false);
            } else if (gesture == "happy_dance" || gesture == "happy") {
                _gesture_modifier_id = sc.addModifier(std::make_unique<DanceModifier>(DanceModifier::Happy));
            } else if (gesture == "robot_dance" || gesture == "robot") {
                _gesture_modifier_id = sc.addModifier(std::make_unique<DanceModifier>(DanceModifier::Robot));
            } else if (gesture == "look_around" || gesture == "scan") {
                _gesture_modifier_id = sc.addModifier(std::make_unique<DanceModifier>(DanceModifier::LookAround));
            } else if (gesture == "panic") {
                _gesture_modifier_id = sc.addModifier(std::make_unique<DanceModifier>(DanceModifier::Panic));
            } else {
                sc.motion().goHome(250);
            }

            return true;
        });

    mclog::tagInfo(_tag, "add robot.dance tool");
    mcp_server.AddTool(
        "self.robot.dance",
        "Dance to ambient music with beat-synced head motion and RGB LEDs. "
        "Estimates BPM from the microphone (defaults to 120 if unclear), then for each 16-beat block "
        "alternates head up/down then right/left. LEDs flash right/left halves through red, blue, green, purple. "
        "Default duration is 45 seconds (max 120). Set stop=true to end an active dance early. "
        "Wake-word listening is paused while dancing; listening/thinking LED patterns are overridden. "
        "Use whenever the user says dance, dance to the music, or stop dancing.",
        PropertyList({Property("duration_seconds", kPropertyTypeInteger, robot_dance::kDefaultDurationSec,
                               robot_dance::kMinDurationSec, robot_dance::kMaxDurationSec),
                      Property("stop", kPropertyTypeBoolean, false)}),
        [](const PropertyList& properties) -> ReturnValue {
            const int duration = properties["duration_seconds"].value<int>();
            const bool stop    = properties["stop"].value<bool>();
            mclog::tagInfo(_tag, "dance: duration={} stop={}", duration, stop);
            return robot_dance::start(duration, stop);
        });

    mclog::tagInfo(_tag, "add robot.set_led_color tool");
    mcp_server.AddTool(
        "self.robot.set_led_color",
        "Set the RGB color of all 12 onboard LEDs (left and right neon strips) on StackChan. "
        "Values: 0-168 (safe brightness range). Red=168,0,0; Green=0,168,0; Blue=0,0,168; "
        "Cyan=0,140,168; Purple=120,0,168; WarmWhite=120,100,80; Off=0,0,0.",
        PropertyList({Property("red", kPropertyTypeInteger, 0, 0, 168),
                      Property("green", kPropertyTypeInteger, 0, 0, 168),
                      Property("blue", kPropertyTypeInteger, 0, 0, 168)}),
        [this](const PropertyList& properties) -> ReturnValue {
            int r = properties["red"].value<int>();
            int g = properties["green"].value<int>();
            int b = properties["blue"].value<int>();

            mclog::tagInfo(_tag, "set_led_color: r={}, g={}, b={}", r, g, b);

            hal_openai::set_custom_led_active((r != 0) || (g != 0) || (b != 0));

            LvglLockGuard lock;

            GetStackChan().leftNeonLight().setColor(r, g, b);
            GetStackChan().rightNeonLight().setColor(r, g, b);
            GetHAL().showRgbColor(r, g, b);

            return true;
        });

    mclog::tagInfo(_tag, "add robot.set_led_side_color tool");
    mcp_server.AddTool(
        "self.robot.set_led_side_color",
        "Set left and right RGB LED strips independently on StackChan (each side has 6 RGB LEDs). "
        "Values: 0-168 per channel.",
        PropertyList({Property("left_red", kPropertyTypeInteger, 0, 0, 168),
                      Property("left_green", kPropertyTypeInteger, 0, 0, 168),
                      Property("left_blue", kPropertyTypeInteger, 0, 0, 168),
                      Property("right_red", kPropertyTypeInteger, 0, 0, 168),
                      Property("right_green", kPropertyTypeInteger, 0, 0, 168),
                      Property("right_blue", kPropertyTypeInteger, 0, 0, 168)}),
        [this](const PropertyList& properties) -> ReturnValue {
            int lr = properties["left_red"].value<int>();
            int lg = properties["left_green"].value<int>();
            int lb = properties["left_blue"].value<int>();
            int rr = properties["right_red"].value<int>();
            int rg = properties["right_green"].value<int>();
            int rb = properties["right_blue"].value<int>();

            mclog::tagInfo(_tag, "set_led_side_color: L=({},{},{}) R=({},{},{})", lr, lg, lb, rr, rg, rb);

            hal_openai::set_custom_led_active((lr | lg | lb | rr | rg | rb) != 0);

            LvglLockGuard lock;
            GetStackChan().leftNeonLight().setColor(lr, lg, lb);
            GetStackChan().rightNeonLight().setColor(rr, rg, rb);
            for (int i = 0; i < 6; ++i) {
                GetHAL().setRgbColor(i, lr, lg, lb);
                GetHAL().setRgbColor(i + 6, rr, rg, rb);
            }
            GetHAL().refreshRgb();

            return true;
        });

    mclog::tagInfo(_tag, "add robot.set_emotion tool");
    mcp_server.AddTool(
        "self.robot.set_emotion",
        "Set StackChan's facial expression on the screen. "
        "Supported emotions: 'neutral', 'happy', 'laughing', 'angry', 'sad', 'crying', 'sleepy', 'doubtful'.",
        PropertyList({Property("emotion", kPropertyTypeString, std::string("happy"))}),
        [this](const PropertyList& properties) -> ReturnValue {
            std::string emotion = properties["emotion"].value<std::string>();
            mclog::tagInfo(_tag, "set_emotion: {}", emotion);

            LvglLockGuard lock;
            auto& sc = GetStackChan();
            if (sc.hasAvatar()) {
                auto& av = sc.avatar();
                if (emotion == "happy" || emotion == "laughing") {
                    av.setEmotion(avatar::Emotion::Happy);
                } else if (emotion == "angry") {
                    av.setEmotion(avatar::Emotion::Angry);
                } else if (emotion == "sad" || emotion == "crying") {
                    av.setEmotion(avatar::Emotion::Sad);
                } else if (emotion == "sleepy") {
                    av.setEmotion(avatar::Emotion::Sleepy);
                } else if (emotion == "doubtful") {
                    av.setEmotion(avatar::Emotion::Doubt);
                } else {
                    av.setEmotion(avatar::Emotion::Neutral);
                }
            }
            return true;
        });

    mclog::tagInfo(_tag, "add robot.create_reminder tool");
    mcp_server.AddTool("self.robot.create_reminder",
                       "Create a reminder. Duration is in seconds. Message is what to say when time is up. Set repeat "
                       "to true to repeat the reminder.",
                       PropertyList({Property("duration_seconds", kPropertyTypeInteger, 60, 1, 86400),
                                     Property("message", kPropertyTypeString, std::string("Time's up!")),
                                     Property("repeat", kPropertyTypeBoolean, false)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int duration_seconds = properties["duration_seconds"].value<int>();
                           std::string message  = properties["message"].value<std::string>();
                           bool repeat          = properties["repeat"].value<bool>();

                           // Default message
                           if (message.empty()) {
                               message = "Time's up!";
                           }

                           mclog::tagInfo(_tag, "create_reminder: duration={}s, message={}, repeat={}",
                                          duration_seconds, message, repeat);

                           int id = tools::create_reminder(duration_seconds * 1000, message, repeat);

                           return id;
                       });

    mclog::tagInfo(_tag, "add robot.get_reminders tool");
    mcp_server.AddTool("self.robot.get_reminders", "Get list of active reminders.", std::vector<Property>{},
                       [this](const PropertyList& properties) -> ReturnValue {
                           mclog::tagInfo(_tag, "get_reminders");
                           auto reminders          = tools::get_active_reminders();
                           std::string result_json = "[";
                           for (size_t i = 0; i < reminders.size(); ++i) {
                               const auto& r = reminders[i];
                               result_json +=
                                   fmt::format(R"({{"id": {}, "duration_ms": {}, "message": "{}", "repeat": {}}})",
                                               r.id, r.durationMs, r.message, r.repeat ? "true" : "false");
                               if (i < reminders.size() - 1) {
                                   result_json += ", ";
                               }
                           }
                           result_json += "]";
                           mclog::tagInfo(_tag, "get_reminders result: {}", result_json);
                           return result_json;
                       });

    mclog::tagInfo(_tag, "add robot.stop_reminder tool");
    mcp_server.AddTool("self.robot.stop_reminder", "Stop a reminder by ID.",
                       PropertyList({Property("id", kPropertyTypeInteger, -1)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int id = properties["id"].value<int>();
                           mclog::tagInfo(_tag, "stop_reminder: id={}", id);
                           tools::stop_reminder(id);
                           return true;
                       });
}
