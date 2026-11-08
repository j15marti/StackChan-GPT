/*
 * SPDX-FileCopyrightText: 2026 StackChan-GPT Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace robot_dance {

constexpr int kDefaultDurationSec = 45;
constexpr int kMinDurationSec     = 5;
constexpr int kMaxDurationSec     = 120;

/** True while the beat-synced dance task is running. */
bool is_active();

/**
 * Feed 16 kHz PCM from the live mic path while dancing.
 * Used for onset / BPM estimation; does not own the audio pipeline.
 */
void feed_pcm(const int16_t* samples, size_t count, int sample_rate_hz = 16000);

/**
 * Start (or restart) a beat-synced dance, or stop the current one.
 * @param duration_sec Dance length; default 45 s (clamped 5–120).
 * @param stop If true, stop any active dance and ignore duration.
 * @return Short JSON status for the LLM tool result.
 */
std::string start(int duration_sec = kDefaultDurationSec, bool stop = false);

/** Request stop; safe to call from any task. */
void stop();

}  // namespace robot_dance
