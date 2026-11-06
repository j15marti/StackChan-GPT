/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace idle_camera_motion {

enum class PixelFormat {
    Yuyv,
    Uyvy,
    Gray8,
    Rgb565LittleEndian,
    Rgb888,
    Yuv420,
};

struct LumaFrame {
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

struct MotionResult {
    bool valid       = false;
    bool significant = false;

    // 0: upper-left, 1: upper-right, 2: lower-left, 3: lower-right.
    int dominant_quadrant = -1;
    std::array<int, 4> quadrant_changed_permille = {0, 0, 0, 0};

    // Position of all changed pixels in normalized image coordinates.
    // -1000 is left/top, 0 is center, and +1000 is right/bottom.
    int centroid_x_permille = 0;
    int centroid_y_permille = 0;
    int changed_pixels      = 0;
    int brightness_delta    = 0;
};

struct TrackingTarget {
    bool valid       = false;
    bool centered    = false;
    bool should_move = false;
    int yaw          = 0;
    int pitch        = 0;
};

bool ConvertToLuma(const uint8_t* data, size_t data_size, int width, int height, PixelFormat format,
                   LumaFrame& output);

MotionResult AnalyzeMotion(const LumaFrame& previous, const LumaFrame& current,
                           uint8_t pixel_change_threshold = 24,
                           int quadrant_change_threshold_permille = 100);

TrackingTarget CalculateTrackingTarget(int current_yaw, int current_pitch, const MotionResult& motion,
                                       int center_deadband_permille = 120, int max_yaw_step = 320,
                                       int max_pitch_step = 220, int min_yaw = -1000, int max_yaw = 1000,
                                       int min_pitch = 50, int max_pitch = 800);

}  // namespace idle_camera_motion
