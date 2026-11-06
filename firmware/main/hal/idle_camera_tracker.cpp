/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "idle_camera_tracker.h"

#include "board/hal_bridge.h"
#include "hal.h"
#include "idle_camera_motion.h"

#include <application.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stackchan/stackchan.h>

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace {

constexpr char kTag[] = "IdleCameraTracker";

constexpr uint32_t kRequiredIdleMs       = 30000;
constexpr uint32_t kIdleScanIntervalMs   = 10000;
constexpr uint32_t kFramePairGapMs       = 250;
constexpr uint32_t kTrackingCycleWaitMs  = 650;
constexpr uint32_t kMaximumTrackingMs    = 30000;
constexpr uint32_t kCancellationPollMs   = 50;
constexpr int kServoSpeed                = 220;
constexpr uint8_t kPixelChangeThreshold  = 24;
constexpr int kQuadrantThresholdPermille = 100;

std::atomic<bool> g_tracker_started{false};

uint32_t NowMs()
{
    return GetHAL().millis();
}

bool HasElapsed(uint32_t start, uint32_t interval)
{
    return static_cast<uint32_t>(NowMs() - start) >= interval;
}

bool IsRobotAtRest()
{
    if (!hal_bridge::is_xiaozhi_ready()) {
        return false;
    }

    auto& app = Application::GetInstance();
    return app.GetDeviceState() == kDeviceStateIdle && app.GetAudioService().IsPlaybackIdle() &&
           hal_bridge::get_touch_point().num == 0;
}

bool IsMotionAvailableForScan()
{
    LvglLockGuard lock;
    auto& motion = GetStackChan().motion();
    return !motion.isMoving() && !motion.isModifyLocked();
}

bool IsTrackingSessionUsable()
{
    if (!IsRobotAtRest()) {
        return false;
    }

    LvglLockGuard lock;
    auto& motion = GetStackChan().motion();
    return motion.hasIdleTrackingLock() && !motion.isExternallyModifyLocked();
}

bool IsHeadMoving()
{
    LvglLockGuard lock;
    return GetStackChan().motion().isMoving();
}

bool WaitWithCancellation(uint32_t wait_ms, bool tracking)
{
    const uint32_t started = NowMs();
    while (!HasElapsed(started, wait_ms)) {
        if (tracking ? !IsTrackingSessionUsable() : !IsRobotAtRest()) {
            return false;
        }

        const uint32_t elapsed   = static_cast<uint32_t>(NowMs() - started);
        const uint32_t remaining = wait_ms > elapsed ? wait_ms - elapsed : 0;
        const uint32_t delay_ms  = std::min(remaining, kCancellationPollMs);
        if (delay_ms == 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    return tracking ? IsTrackingSessionUsable() : IsRobotAtRest();
}

bool MapPixelFormat(int camera_format, idle_camera_motion::PixelFormat& format)
{
    switch (static_cast<uint32_t>(camera_format)) {
        case V4L2_PIX_FMT_YUYV:
            format = idle_camera_motion::PixelFormat::Yuyv;
            return true;
        case V4L2_PIX_FMT_UYVY:
            format = idle_camera_motion::PixelFormat::Uyvy;
            return true;
        case V4L2_PIX_FMT_GREY:
            format = idle_camera_motion::PixelFormat::Gray8;
            return true;
        case V4L2_PIX_FMT_RGB565:
            format = idle_camera_motion::PixelFormat::Rgb565LittleEndian;
            return true;
        case V4L2_PIX_FMT_RGB24:
            format = idle_camera_motion::PixelFormat::Rgb888;
            return true;
        case V4L2_PIX_FMT_YUV420:
            format = idle_camera_motion::PixelFormat::Yuv420;
            return true;
        default:
            return false;
    }
}

bool CaptureLumaFrame(idle_camera_motion::LumaFrame& frame)
{
    auto* camera = hal_bridge::board_get_camera();
    if (camera == nullptr) {
        return false;
    }

    auto camera_lock = camera->TryLockFrame();
    // DVP cameras use a single queued buffer. Discarding one frame flushes a
    // stale image captured while the head was moving, then returns a fresh
    // frame from the now-stationary camera.
    if (!camera_lock.owns_lock() || !camera->StreamCaptures(1)) {
        return false;
    }

    idle_camera_motion::PixelFormat pixel_format;
    if (!MapPixelFormat(camera->GetFrameFormat(), pixel_format)) {
        ESP_LOGW(kTag, "unsupported camera format: 0x%08x", camera->GetFrameFormat());
        return false;
    }

    return idle_camera_motion::ConvertToLuma(
        camera->GetFrameData(), camera->GetFrameSize(), camera->GetFrameWidth(),
        camera->GetFrameHeight(), pixel_format, frame);
}

bool CaptureMotionPair(idle_camera_motion::MotionResult& result, bool tracking)
{
    result = {};
    const auto can_continue = [tracking]() {
        if (tracking) {
            return IsTrackingSessionUsable() && !IsHeadMoving();
        }
        return IsRobotAtRest() && IsMotionAvailableForScan();
    };

    if (!can_continue()) {
        return false;
    }

    // This function runs only on the single tracker task. Retaining the two
    // luma buffers avoids repeated PSRAM allocation during long idle periods.
    static idle_camera_motion::LumaFrame previous;
    static idle_camera_motion::LumaFrame current;
    if (!CaptureLumaFrame(previous) || !can_continue()) {
        return false;
    }
    if (!WaitWithCancellation(kFramePairGapMs, tracking) || !can_continue()) {
        return false;
    }
    if (!CaptureLumaFrame(current) || !can_continue()) {
        return false;
    }

    result = idle_camera_motion::AnalyzeMotion(previous, current, kPixelChangeThreshold,
                                                kQuadrantThresholdPermille);
    return result.valid;
}

bool AcquireTrackingLock()
{
    LvglLockGuard lock;
    auto& motion = GetStackChan().motion();
    return !motion.isMoving() && motion.tryAcquireIdleTrackingLock();
}

void ReleaseTrackingLock()
{
    LvglLockGuard lock;
    auto& motion = GetStackChan().motion();
    if (motion.hasIdleTrackingLock()) {
        motion.releaseIdleTrackingLock();
    }
}

void StopTrackingMotionAtTimeout()
{
    if (!IsRobotAtRest()) {
        return;
    }

    LvglLockGuard lock;
    auto& motion = GetStackChan().motion();
    if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle &&
        motion.hasIdleTrackingLock() && !motion.isExternallyModifyLocked()) {
        motion.stop();
    }
}

void ApplyTrackingCorrection(const idle_camera_motion::MotionResult& result)
{
    if (!result.significant) {
        return;
    }

    LvglLockGuard lock;
    auto& motion = GetStackChan().motion();
    if (!motion.hasIdleTrackingLock() || motion.isExternallyModifyLocked()) {
        return;
    }

    const auto target = idle_camera_motion::CalculateTrackingTarget(
        motion.getCurrentYawAngle(), motion.getCurrentPitchAngle(), result);
    if (!target.valid || !target.should_move) {
        return;
    }

    ESP_LOGI(kTag, "centering movement: quadrant=%d, centroid=(%d,%d), target=(%d,%d)",
             result.dominant_quadrant, result.centroid_x_permille, result.centroid_y_permille,
             target.yaw, target.pitch);
    motion.moveWithSpeed(target.yaw, target.pitch, kServoSpeed);
}

bool WaitForHeadToSettle(uint32_t tracking_started)
{
    while (IsHeadMoving()) {
        if (!IsTrackingSessionUsable() || HasElapsed(tracking_started, kMaximumTrackingMs)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(kCancellationPollMs));
    }
    return true;
}

void RunTrackingSession(const idle_camera_motion::MotionResult& initial_result)
{
    if (!IsRobotAtRest() || !AcquireTrackingLock()) {
        return;
    }

    if (!IsTrackingSessionUsable()) {
        ReleaseTrackingLock();
        return;
    }

    const uint32_t tracking_started = NowMs();
    ESP_LOGI(kTag, "deterministic motion tracking started");
    ApplyTrackingCorrection(initial_result);

    bool cancelled = false;
    while (!HasElapsed(tracking_started, kMaximumTrackingMs)) {
        const uint32_t elapsed = static_cast<uint32_t>(NowMs() - tracking_started);
        const uint32_t remaining =
            elapsed < kMaximumTrackingMs ? kMaximumTrackingMs - elapsed : 0;
        const uint32_t wait_ms = std::min(kTrackingCycleWaitMs, remaining);
        if (!WaitWithCancellation(wait_ms, true)) {
            cancelled = true;
            break;
        }
        if (HasElapsed(tracking_started, kMaximumTrackingMs)) {
            break;
        }
        if (!WaitForHeadToSettle(tracking_started)) {
            cancelled = !IsTrackingSessionUsable();
            break;
        }

        idle_camera_motion::MotionResult result;
        if (CaptureMotionPair(result, true) &&
            !HasElapsed(tracking_started, kMaximumTrackingMs)) {
            ESP_LOGD(kTag, "motion sample: quadrants=%d,%d,%d,%d permille",
                     result.quadrant_changed_permille[0], result.quadrant_changed_permille[1],
                     result.quadrant_changed_permille[2], result.quadrant_changed_permille[3]);
            ApplyTrackingCorrection(result);
        } else if (!IsTrackingSessionUsable()) {
            cancelled = true;
            break;
        }
    }

    const bool timed_out = !cancelled && HasElapsed(tracking_started, kMaximumTrackingMs);
    if (timed_out) {
        StopTrackingMotionAtTimeout();
    }
    ReleaseTrackingLock();
    ESP_LOGI(kTag, "deterministic motion tracking stopped: %s",
             timed_out ? "30 second limit" : "cancelled");
}

void IdleCameraTrackerTask(void*)
{
    uint32_t last_busy = NowMs();
    uint32_t last_scan = last_busy;
    bool idle_window_started = false;

    while (true) {
        if (!IsRobotAtRest()) {
            last_busy           = NowMs();
            last_scan           = last_busy;
            idle_window_started = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (!HasElapsed(last_busy, kRequiredIdleMs)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (!idle_window_started) {
            idle_window_started = true;
            last_scan = static_cast<uint32_t>(NowMs() - kIdleScanIntervalMs);
        }
        if (!HasElapsed(last_scan, kIdleScanIntervalMs)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        last_scan = NowMs();
        if (!IsMotionAvailableForScan()) {
            continue;
        }

        idle_camera_motion::MotionResult result;
        if (!CaptureMotionPair(result, false) || !result.significant) {
            continue;
        }

        ESP_LOGI(kTag, "idle movement detected: quadrant=%d, changed=%d permille",
                 result.dominant_quadrant,
                 result.quadrant_changed_permille[result.dominant_quadrant]);
        RunTrackingSession(result);
        last_scan = NowMs();
    }
}

}  // namespace

void start_idle_camera_tracker()
{
    bool expected = false;
    if (!g_tracker_started.compare_exchange_strong(expected, true)) {
        return;
    }

    const BaseType_t created =
        xTaskCreatePinnedToCore(IdleCameraTrackerTask, "idle_cam_track", 8192, nullptr, 2, nullptr, 0);
    if (created != pdPASS) {
        g_tracker_started.store(false);
        ESP_LOGE(kTag, "failed to create tracker task");
    }
}
