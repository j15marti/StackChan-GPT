/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "idle_camera_motion.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace idle_camera_motion {
namespace {

bool CalculatePixelCount(int width, int height, size_t& count)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    const size_t width_size  = static_cast<size_t>(width);
    const size_t height_size = static_cast<size_t>(height);
    if (width_size > std::numeric_limits<size_t>::max() / height_size) {
        return false;
    }

    count = width_size * height_size;
    return true;
}

bool HasBytes(size_t pixel_count, size_t bytes_per_pixel, size_t data_size)
{
    return pixel_count <= std::numeric_limits<size_t>::max() / bytes_per_pixel &&
           data_size >= pixel_count * bytes_per_pixel;
}

uint8_t Expand5(uint16_t value)
{
    return static_cast<uint8_t>((value << 3) | (value >> 2));
}

uint8_t Expand6(uint16_t value)
{
    return static_cast<uint8_t>((value << 2) | (value >> 4));
}

uint8_t RgbToLuma(uint8_t red, uint8_t green, uint8_t blue)
{
    return static_cast<uint8_t>((77U * red + 150U * green + 29U * blue + 128U) >> 8U);
}

int RoundedDivide(int64_t numerator, int64_t denominator)
{
    if (numerator >= 0) {
        return static_cast<int>((numerator + denominator / 2) / denominator);
    }
    return -static_cast<int>((-numerator + denominator / 2) / denominator);
}

}  // namespace

bool ConvertToLuma(const uint8_t* data, size_t data_size, int width, int height, PixelFormat format,
                   LumaFrame& output)
{
    output.width  = 0;
    output.height = 0;
    output.pixels.clear();
    if (data == nullptr) {
        return false;
    }

    size_t pixel_count = 0;
    if (!CalculatePixelCount(width, height, pixel_count)) {
        return false;
    }

    switch (format) {
        case PixelFormat::Yuyv:
        case PixelFormat::Uyvy:
        case PixelFormat::Rgb565LittleEndian:
            if (!HasBytes(pixel_count, 2, data_size)) {
                return false;
            }
            break;
        case PixelFormat::Rgb888:
            if (!HasBytes(pixel_count, 3, data_size)) {
                return false;
            }
            break;
        case PixelFormat::Gray8:
        case PixelFormat::Yuv420:
            if (data_size < pixel_count) {
                return false;
            }
            break;
    }
    output.width  = width;
    output.height = height;
    output.pixels.resize(pixel_count);

    switch (format) {
        case PixelFormat::Yuyv:
            for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
                output.pixels[pixel] = data[pixel * 2];
            }
            break;
        case PixelFormat::Uyvy:
            for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
                output.pixels[pixel] = data[pixel * 2 + 1];
            }
            break;
        case PixelFormat::Gray8:
        case PixelFormat::Yuv420:
            std::copy_n(data, pixel_count, output.pixels.begin());
            break;
        case PixelFormat::Rgb565LittleEndian:
            for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
                const uint16_t packed = static_cast<uint16_t>(data[pixel * 2]) |
                                        (static_cast<uint16_t>(data[pixel * 2 + 1]) << 8U);
                const uint8_t red   = Expand5((packed >> 11U) & 0x1FU);
                const uint8_t green = Expand6((packed >> 5U) & 0x3FU);
                const uint8_t blue  = Expand5(packed & 0x1FU);
                output.pixels[pixel] = RgbToLuma(red, green, blue);
            }
            break;
        case PixelFormat::Rgb888:
            for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
                output.pixels[pixel] =
                    RgbToLuma(data[pixel * 3], data[pixel * 3 + 1], data[pixel * 3 + 2]);
            }
            break;
    }

    return true;
}

MotionResult AnalyzeMotion(const LumaFrame& previous, const LumaFrame& current,
                           uint8_t pixel_change_threshold, int quadrant_change_threshold_permille)
{
    MotionResult result;
    if (previous.width != current.width || previous.height != current.height || previous.width <= 1 ||
        previous.height <= 1 || previous.pixels.size() != current.pixels.size()) {
        return result;
    }

    size_t pixel_count = 0;
    if (!CalculatePixelCount(previous.width, previous.height, pixel_count) ||
        previous.pixels.size() != pixel_count) {
        return result;
    }

    result.valid = true;
    quadrant_change_threshold_permille = std::clamp(quadrant_change_threshold_permille, 0, 1000);

    // A 511-bin histogram gives the median per-pixel brightness shift without
    // allocating another frame. Median compensation is robust when a large
    // moving object changes the mean brightness of one quadrant.
    std::array<int, 511> brightness_histogram{};
    for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const int difference =
            static_cast<int>(current.pixels[pixel]) - static_cast<int>(previous.pixels[pixel]);
        ++brightness_histogram[static_cast<size_t>(difference + 255)];
    }
    const int64_t median_rank = static_cast<int64_t>((pixel_count - 1) / 2);
    int64_t cumulative        = 0;
    for (size_t bin = 0; bin < brightness_histogram.size(); ++bin) {
        cumulative += brightness_histogram[bin];
        if (cumulative > median_rank) {
            result.brightness_delta = static_cast<int>(bin) - 255;
            break;
        }
    }

    std::array<int, 4> quadrant_pixels  = {0, 0, 0, 0};
    std::array<int, 4> quadrant_changed = {0, 0, 0, 0};
    int64_t changed_x_sum               = 0;
    int64_t changed_y_sum               = 0;

    for (int y = 0; y < previous.height; ++y) {
        for (int x = 0; x < previous.width; ++x) {
            const size_t pixel = static_cast<size_t>(y) * static_cast<size_t>(previous.width) +
                                 static_cast<size_t>(x);
            const int quadrant = (y >= previous.height / 2 ? 2 : 0) +
                                 (x >= previous.width / 2 ? 1 : 0);
            ++quadrant_pixels[quadrant];

            const int residual = static_cast<int>(current.pixels[pixel]) -
                                 static_cast<int>(previous.pixels[pixel]) - result.brightness_delta;
            if (std::abs(residual) < static_cast<int>(pixel_change_threshold)) {
                continue;
            }

            ++quadrant_changed[quadrant];
            ++result.changed_pixels;
            changed_x_sum += x;
            changed_y_sum += y;
        }
    }

    int dominant = -1;
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        result.quadrant_changed_permille[quadrant] =
            quadrant_pixels[quadrant] == 0
                ? 0
                : static_cast<int>((static_cast<int64_t>(quadrant_changed[quadrant]) * 1000) /
                                   quadrant_pixels[quadrant]);
        if (dominant < 0 ||
            static_cast<int64_t>(quadrant_changed[quadrant]) * quadrant_pixels[dominant] >
                static_cast<int64_t>(quadrant_changed[dominant]) * quadrant_pixels[quadrant]) {
            dominant = quadrant;
        }
    }
    result.dominant_quadrant = dominant;

    if (result.changed_pixels > 0) {
        const int64_t x_denominator =
            static_cast<int64_t>(result.changed_pixels) * (previous.width - 1);
        const int64_t y_denominator =
            static_cast<int64_t>(result.changed_pixels) * (previous.height - 1);
        result.centroid_x_permille =
            RoundedDivide(changed_x_sum * 2000, x_denominator) - 1000;
        result.centroid_y_permille =
            RoundedDivide(changed_y_sum * 2000, y_denominator) - 1000;
    }

    if (dominant >= 0) {
        result.significant =
            static_cast<int64_t>(quadrant_changed[dominant]) * 1000 >
            static_cast<int64_t>(quadrant_pixels[dominant]) * quadrant_change_threshold_permille;
    }
    return result;
}

TrackingTarget CalculateTrackingTarget(int current_yaw, int current_pitch, const MotionResult& motion,
                                       int center_deadband_permille, int max_yaw_step,
                                       int max_pitch_step, int min_yaw, int max_yaw, int min_pitch,
                                       int max_pitch)
{
    TrackingTarget target;
    target.yaw   = current_yaw;
    target.pitch = current_pitch;
    if (!motion.valid || !motion.significant || min_yaw > max_yaw || min_pitch > max_pitch) {
        return target;
    }

    target.valid = true;
    center_deadband_permille = std::clamp(center_deadband_permille, 0, 1000);
    max_yaw_step             = std::max(max_yaw_step, 0);
    max_pitch_step           = std::max(max_pitch_step, 0);

    const int x_error = std::clamp(motion.centroid_x_permille, -1000, 1000);
    const int y_error = std::clamp(motion.centroid_y_permille, -1000, 1000);
    target.centered   = std::abs(x_error) <= center_deadband_permille &&
                      std::abs(y_error) <= center_deadband_permille;

    int yaw_step   = 0;
    int pitch_step = 0;
    if (std::abs(x_error) > center_deadband_permille) {
        yaw_step = RoundedDivide(static_cast<int64_t>(x_error) * max_yaw_step, 1000);
    }
    if (std::abs(y_error) > center_deadband_permille) {
        pitch_step = -RoundedDivide(static_cast<int64_t>(y_error) * max_pitch_step, 1000);
    }

    target.yaw   = std::clamp(current_yaw + yaw_step, min_yaw, max_yaw);
    target.pitch = std::clamp(current_pitch + pitch_step, min_pitch, max_pitch);
    target.should_move = target.yaw != current_yaw || target.pitch != current_pitch;
    return target;
}

}  // namespace idle_camera_motion
