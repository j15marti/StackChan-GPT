/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal/idle_camera_motion.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using idle_camera_motion::AnalyzeMotion;
using idle_camera_motion::CalculateTrackingTarget;
using idle_camera_motion::ConvertToLuma;
using idle_camera_motion::LumaFrame;
using idle_camera_motion::MotionResult;
using idle_camera_motion::PixelFormat;

namespace {

LumaFrame SolidFrame(int width, int height, uint8_t value)
{
    LumaFrame frame;
    frame.width  = width;
    frame.height = height;
    frame.pixels.assign(static_cast<size_t>(width * height), value);
    return frame;
}

void SetPixel(LumaFrame& frame, int x, int y, uint8_t value)
{
    frame.pixels[static_cast<size_t>(y * frame.width + x)] = value;
}

void TestStrictTenPercentThreshold()
{
    auto previous = SolidFrame(20, 20, 50);
    auto current  = previous;

    for (int x = 0; x < 10; ++x) {
        SetPixel(current, x, 0, 200);
    }
    auto exact_ten_percent = AnalyzeMotion(previous, current);
    assert(exact_ten_percent.valid);
    assert(exact_ten_percent.quadrant_changed_permille[0] == 100);
    assert(!exact_ten_percent.significant);

    SetPixel(current, 0, 1, 200);
    auto above_ten_percent = AnalyzeMotion(previous, current);
    assert(above_ten_percent.significant);
    assert(above_ten_percent.dominant_quadrant == 0);
    assert(above_ten_percent.quadrant_changed_permille[0] == 110);
}

void TestQuadrantAndCentroid()
{
    auto previous = SolidFrame(40, 40, 80);
    auto current  = previous;
    for (int y = 25; y < 35; ++y) {
        for (int x = 26; x < 36; ++x) {
            SetPixel(current, x, y, 210);
        }
    }

    const auto result = AnalyzeMotion(previous, current);
    assert(result.significant);
    assert(result.dominant_quadrant == 3);
    assert(result.changed_pixels == 100);
    assert(result.centroid_x_permille > 500);
    assert(result.centroid_y_permille > 450);
}

void TestUniformBrightnessChangeIsRejected()
{
    auto previous = SolidFrame(32, 24, 60);
    auto current  = SolidFrame(32, 24, 100);

    const auto result = AnalyzeMotion(previous, current);
    assert(result.valid);
    assert(result.brightness_delta == 40);
    assert(result.changed_pixels == 0);
    assert(!result.significant);
}

void TestLargeLocalChangeDoesNotShiftBackground()
{
    auto previous = SolidFrame(40, 40, 50);
    auto current  = previous;
    for (int y = 0; y < 20; ++y) {
        for (int x = 20; x < 40; ++x) {
            SetPixel(current, x, y, 150);
        }
    }

    const auto result = AnalyzeMotion(previous, current);
    assert(result.significant);
    assert(result.brightness_delta == 0);
    assert(result.changed_pixels == 400);
    assert(result.dominant_quadrant == 1);
    assert(result.quadrant_changed_permille[0] == 0);
    assert(result.quadrant_changed_permille[1] == 1000);
    assert(result.quadrant_changed_permille[2] == 0);
    assert(result.quadrant_changed_permille[3] == 0);
}

void TestPackedYuvConversion()
{
    const std::vector<uint8_t> yuyv = {10, 128, 200, 128};
    LumaFrame output;
    assert(ConvertToLuma(yuyv.data(), yuyv.size(), 2, 1, PixelFormat::Yuyv, output));
    assert((output.pixels == std::vector<uint8_t>{10, 200}));

    const std::vector<uint8_t> yuyv_2x2 = {
        10, 128, 200, 128,
        30, 128, 220, 128,
    };
    assert(ConvertToLuma(yuyv_2x2.data(), yuyv_2x2.size(), 2, 2, PixelFormat::Yuyv, output));
    assert((output.pixels == std::vector<uint8_t>{10, 200, 30, 220}));

    const std::vector<uint8_t> uyvy_2x2 = {
        128, 11, 128, 201,
        128, 31, 128, 221,
    };
    assert(ConvertToLuma(uyvy_2x2.data(), uyvy_2x2.size(), 2, 2, PixelFormat::Uyvy, output));
    assert((output.pixels == std::vector<uint8_t>{11, 201, 31, 221}));
}

void TestRgb565Conversion()
{
    // Little-endian RGB565: red, green, blue, white.
    const std::vector<uint8_t> rgb565 = {0x00, 0xF8, 0xE0, 0x07, 0x1F, 0x00, 0xFF, 0xFF};
    LumaFrame output;
    assert(ConvertToLuma(rgb565.data(), rgb565.size(), 2, 2,
                         PixelFormat::Rgb565LittleEndian, output));
    assert(output.pixels[0] >= 76 && output.pixels[0] <= 77);
    assert(output.pixels[1] >= 149 && output.pixels[1] <= 150);
    assert(output.pixels[2] >= 28 && output.pixels[2] <= 29);
    assert(output.pixels[3] == 255);
}

void TestTrackingCorrections()
{
    MotionResult motion;
    motion.valid                 = true;
    motion.significant           = true;
    motion.centroid_x_permille   = 500;
    motion.centroid_y_permille   = -500;

    const auto target = CalculateTrackingTarget(0, 200, motion);
    assert(target.valid);
    assert(!target.centered);
    assert(target.should_move);
    assert(target.yaw == 160);
    assert(target.pitch == 310);

    motion.centroid_x_permille = 80;
    motion.centroid_y_permille = -100;
    const auto centered = CalculateTrackingTarget(25, 250, motion);
    assert(centered.valid);
    assert(centered.centered);
    assert(!centered.should_move);
    assert(centered.yaw == 25);
    assert(centered.pitch == 250);

    motion.centroid_x_permille = 1000;
    motion.centroid_y_permille = -1000;
    const auto clamped = CalculateTrackingTarget(950, 790, motion);
    assert(clamped.yaw == 1000);
    assert(clamped.pitch == 800);
}

}  // namespace

int main()
{
    TestStrictTenPercentThreshold();
    TestQuadrantAndCentroid();
    TestUniformBrightnessChangeIsRejected();
    TestLargeLocalChangeDoesNotShiftBackground();
    TestPackedYuvConversion();
    TestRgb565Conversion();
    TestTrackingCorrections();
    std::cout << "idle camera motion tests passed\n";
    return 0;
}
