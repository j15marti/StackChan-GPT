/*
 * SPDX-FileCopyrightText: 2026 StackChan-GPT Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#include "robot_dance.h"
#include "robot_dance_beat.h"
#include "hal.h"
#include "hal_openai.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>

static const char* TAG = "robot_dance";

namespace {

// Safe expressive head poses (tenths of a degree). Clamped by ScsServo angle limits.
constexpr int kPitchUp    = 380;   // ~38°
constexpr int kPitchDown  = 90;    // ~9°
constexpr int kYawLeft    = 420;   // robot left
constexpr int kYawRight   = -420;  // robot right
constexpr int kPitchCenter = 150;
constexpr int kYawCenter   = 0;
constexpr int kServoSpeed  = 480;

constexpr int kLedBrightness = 140;
constexpr int kPhaseBeats    = 16;
constexpr int kTempoRefreshMs = 4000;

std::atomic<bool> g_active{false};
std::atomic<bool> g_stop_requested{false};
std::atomic<int> g_duration_sec{robot_dance::kDefaultDurationSec};
std::atomic<int> g_beat_period_ms{robot_dance_beat::BpmToBeatPeriodMs(robot_dance_beat::kDefaultBpm)};
std::atomic<int> g_estimated_bpm{robot_dance_beat::kDefaultBpm};
std::atomic<TaskHandle_t> g_task{nullptr};

std::mutex g_tracker_mutex;
robot_dance_beat::TempoTracker g_tracker(16000);

struct LedColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

constexpr LedColor kPalette[] = {
    {kLedBrightness, 0, 0},                         // red
    {0, 0, kLedBrightness},                         // blue
    {0, kLedBrightness, 0},                         // green
    {static_cast<uint8_t>((kLedBrightness * 5) / 7), 0, kLedBrightness},  // purple
};

void ApplySideLeds(bool light_right, const LedColor& color)
{
    LvglLockGuard lock;
    auto& sc = GetStackChan();
    if (light_right) {
        sc.rightNeonLight().setColor(color.r, color.g, color.b);
        sc.leftNeonLight().setColor(0, 0, 0);
        for (int i = 0; i < 6; ++i) {
            GetHAL().setRgbColor(i, 0, 0, 0);
            GetHAL().setRgbColor(i + 6, color.r, color.g, color.b);
        }
    } else {
        sc.leftNeonLight().setColor(color.r, color.g, color.b);
        sc.rightNeonLight().setColor(0, 0, 0);
        for (int i = 0; i < 6; ++i) {
            GetHAL().setRgbColor(i, color.r, color.g, color.b);
            GetHAL().setRgbColor(i + 6, 0, 0, 0);
        }
    }
    GetHAL().refreshRgb();
}

void ClearLeds()
{
    LvglLockGuard lock;
    GetStackChan().leftNeonLight().setColor(0, 0, 0);
    GetStackChan().rightNeonLight().setColor(0, 0, 0);
    GetHAL().showRgbColor(0, 0, 0);
}

void MoveHead(int yaw, int pitch)
{
    LvglLockGuard lock;
    auto& motion = GetStackChan().motion();
    // Extra clamp into the documented natural interaction band.
    yaw   = std::clamp(yaw, -1000, 1000);
    pitch = std::clamp(pitch, 50, 800);
    motion.yawServo().moveWithSpeed(yaw, kServoSpeed);
    motion.pitchServo().moveWithSpeed(pitch, kServoSpeed);
}

void GoHome()
{
    LvglLockGuard lock;
    GetStackChan().motion().goHome(280);
}

void AcquireMotionLock()
{
    LvglLockGuard lock;
    auto& motion = GetStackChan().motion();
    if (motion.hasIdleTrackingLock()) {
        motion.releaseIdleTrackingLock();
    }
    motion.setModifyLock(true);
    motion.setAutoAngleSyncEnabled(false);
}

void ReleaseMotionLock()
{
    LvglLockGuard lock;
    auto& motion = GetStackChan().motion();
    motion.setAutoAngleSyncEnabled(true);
    motion.setModifyLock(false);
}

void ApplyBeatStep(int beat_index)
{
    const int phase = (beat_index / kPhaseBeats) % 2;  // 0 = up/down, 1 = left/right
    const int step  = beat_index % 2;

    if (phase == 0) {
        // Beats 1–16 (0-indexed 0–15): up, down, up, down...
        if (step == 0) {
            MoveHead(kYawCenter, kPitchUp);
        } else {
            MoveHead(kYawCenter, kPitchDown);
        }
    } else {
        // Next 16: right, left, right, left...
        if (step == 0) {
            MoveHead(kYawRight, kPitchCenter);
        } else {
            MoveHead(kYawLeft, kPitchCenter);
        }
    }

    // LED sequence: right red, left red, right blue, left blue, ...
    const int led_step   = beat_index % 8;
    const bool light_right = (led_step % 2) == 0;
    const LedColor color   = kPalette[led_step / 2];
    ApplySideLeds(light_right, color);
}

void RefreshTempoFromTracker()
{
    std::lock_guard<std::mutex> lock(g_tracker_mutex);
    const int period = g_tracker.EstimateBeatPeriodMs();
    const int bpm    = g_tracker.EstimateBpm();
    g_beat_period_ms.store(period);
    g_estimated_bpm.store(bpm);
}

void DanceTask(void* /*arg*/)
{
    ESP_LOGI(TAG, "dance task start duration=%ds", g_duration_sec.load());

    hal_openai::set_thinking_led_active(false);
    hal_openai::set_custom_led_active(true);
    AcquireMotionLock();

    {
        std::lock_guard<std::mutex> lock(g_tracker_mutex);
        g_tracker.Reset();
    }
    g_beat_period_ms.store(robot_dance_beat::BpmToBeatPeriodMs(robot_dance_beat::kDefaultBpm));
    g_estimated_bpm.store(robot_dance_beat::kDefaultBpm);

    // Brief listen window so ambient music can seed a tempo before the first move.
    const int64_t listen_deadline_us = esp_timer_get_time() + 1500000;
    while (!g_stop_requested.load() && esp_timer_get_time() < listen_deadline_us) {
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    RefreshTempoFromTracker();
    ESP_LOGI(TAG, "initial tempo estimate bpm=%d period=%dms", g_estimated_bpm.load(),
             g_beat_period_ms.load());

    const int64_t end_us           = esp_timer_get_time() +
                           static_cast<int64_t>(g_duration_sec.load()) * 1000000LL;
    int64_t next_tempo_refresh_us  = esp_timer_get_time() + static_cast<int64_t>(kTempoRefreshMs) * 1000;
    int beat_index                 = 0;

    while (!g_stop_requested.load() && esp_timer_get_time() < end_us) {
        ApplyBeatStep(beat_index);
        ++beat_index;

        const int period_ms = std::clamp(g_beat_period_ms.load(),
                                         robot_dance_beat::BpmToBeatPeriodMs(robot_dance_beat::kMaxBpm),
                                         robot_dance_beat::BpmToBeatPeriodMs(robot_dance_beat::kMinBpm));
        vTaskDelay(pdMS_TO_TICKS(period_ms));

        if (esp_timer_get_time() >= next_tempo_refresh_us) {
            RefreshTempoFromTracker();
            next_tempo_refresh_us = esp_timer_get_time() + static_cast<int64_t>(kTempoRefreshMs) * 1000;
            ESP_LOGD(TAG, "tempo refresh bpm=%d period=%dms beat=%d", g_estimated_bpm.load(),
                     g_beat_period_ms.load(), beat_index);
        }
    }

    GoHome();
    ClearLeds();
    ReleaseMotionLock();
    hal_openai::set_custom_led_active(false);

    g_active.store(false);
    g_stop_requested.store(false);
    g_task.store(nullptr);
    ESP_LOGI(TAG, "dance task end after %d beats bpm=%d", beat_index, g_estimated_bpm.load());
    vTaskDelete(nullptr);
}

bool SpawnDanceTask()
{
    TaskHandle_t existing = g_task.load();
    if (existing != nullptr) {
        return false;
    }

    g_stop_requested.store(false);
    g_active.store(true);

    TaskHandle_t handle = nullptr;
    const BaseType_t ok =
        xTaskCreatePinnedToCore(DanceTask, "robot_dance", 6144, nullptr, 3, &handle, 0);
    if (ok != pdPASS || handle == nullptr) {
        g_active.store(false);
        ESP_LOGE(TAG, "failed to create dance task");
        return false;
    }
    g_task.store(handle);
    return true;
}

}  // namespace

namespace robot_dance {

bool is_active()
{
    return g_active.load();
}

void feed_pcm(const int16_t* samples, size_t count, int sample_rate_hz)
{
    if (!g_active.load() || samples == nullptr || count == 0) {
        return;
    }
    // AudioService mic path is 16 kHz; tracker is constructed for that rate.
    if (sample_rate_hz > 0 && sample_rate_hz != 16000) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_tracker_mutex);
    g_tracker.FeedPcm(samples, count);
}

void stop()
{
    if (!g_active.load()) {
        return;
    }
    g_stop_requested.store(true);
}

std::string start(int duration_sec, bool stop_flag)
{
    if (stop_flag) {
        if (!is_active()) {
            return R"({"ok":true,"status":"idle","message":"No dance was running."})";
        }
        stop();
        // Wait briefly for the task to wind down.
        for (int i = 0; i < 50 && is_active(); ++i) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        return R"({"ok":true,"status":"stopped","message":"Dance stop requested."})";
    }

    duration_sec = std::clamp(duration_sec, kMinDurationSec, kMaxDurationSec);

    if (is_active()) {
        stop();
        for (int i = 0; i < 50 && is_active(); ++i) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    g_duration_sec.store(duration_sec);
    if (!SpawnDanceTask()) {
        return R"({"ok":false,"status":"error","message":"Failed to start dance task."})";
    }

    char buf[320];
    std::snprintf(
        buf, sizeof(buf),
        R"({"ok":true,"status":"dancing","duration_sec":%d,"default_bpm":%d,"message":"Dancing to the music until timeout or stop. Wake STT paused; green listen / thinking LEDs overridden."})",
        duration_sec, robot_dance_beat::kDefaultBpm);
    return std::string(buf);
}

}  // namespace robot_dance
