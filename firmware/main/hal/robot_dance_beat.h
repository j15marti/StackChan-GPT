/*
 * SPDX-FileCopyrightText: 2026 StackChan-GPT Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace robot_dance_beat {

constexpr int kDefaultBpm           = 120;
constexpr int kMinBpm               = 70;
constexpr int kMaxBpm               = 160;
constexpr int kDefaultSampleRateHz  = 16000;
constexpr int kEnergyWindowMs       = 40;
constexpr int kMinOnsetGapMs        = 280;   // ~214 BPM ceiling for onset spacing
constexpr int kMaxOnsetGapMs        = 1200;  // ~50 BPM floor
constexpr int kOnsetHistoryCapacity = 12;
constexpr float kOnsetRiseRatio     = 1.45f;
constexpr int kOnsetMinDeltaRms     = 55;

inline int BpmToBeatPeriodMs(int bpm)
{
    if (bpm < kMinBpm) {
        bpm = kMinBpm;
    }
    if (bpm > kMaxBpm) {
        bpm = kMaxBpm;
    }
    return (60 * 1000) / bpm;
}

inline int BeatPeriodMsToBpm(int period_ms)
{
    if (period_ms <= 0) {
        return kDefaultBpm;
    }
    int bpm = (60 * 1000) / period_ms;
    if (bpm < kMinBpm) {
        return kMinBpm;
    }
    if (bpm > kMaxBpm) {
        return kMaxBpm;
    }
    return bpm;
}

inline int ComputeRms(const int16_t* samples, size_t count)
{
    if (samples == nullptr || count == 0) {
        return 0;
    }
    uint64_t sum_sq = 0;
    for (size_t i = 0; i < count; ++i) {
        const int32_t s = samples[i];
        sum_sq += static_cast<uint64_t>(s * s);
    }
    const double mean = static_cast<double>(sum_sq) / static_cast<double>(count);
    return static_cast<int>(mean > 0.0 ? std::sqrt(mean) : 0.0);
}

/**
 * Lightweight onset / tempo tracker for ambient music.
 * Feed short PCM frames; call EstimateBeatPeriodMs() periodically.
 */
class TempoTracker {
public:
    explicit TempoTracker(int sample_rate_hz = kDefaultSampleRateHz) : sample_rate_hz_(sample_rate_hz)
    {
        if (sample_rate_hz_ <= 0) {
            sample_rate_hz_ = kDefaultSampleRateHz;
        }
        window_samples_ = (sample_rate_hz_ * kEnergyWindowMs) / 1000;
        if (window_samples_ < 64) {
            window_samples_ = 64;
        }
        Reset();
    }

    void Reset()
    {
        noise_floor_rms_      = 220;
        prev_energy_          = 0;
        samples_since_window_ = 0;
        window_sum_sq_        = 0;
        last_onset_ms_        = -100000;
        onset_count_          = 0;
        estimated_period_ms_  = BpmToBeatPeriodMs(kDefaultBpm);
        total_ms_             = 0;
        for (int& gap : onset_gaps_ms_) {
            gap = 0;
        }
    }

    void FeedPcm(const int16_t* samples, size_t count)
    {
        if (samples == nullptr || count == 0) {
            return;
        }

        for (size_t i = 0; i < count; ++i) {
            const int32_t s = samples[i];
            window_sum_sq_ += static_cast<uint64_t>(s * s);
            ++samples_since_window_;
            if (samples_since_window_ < window_samples_) {
                continue;
            }

            const double mean = static_cast<double>(window_sum_sq_) / static_cast<double>(window_samples_);
            const int energy  = static_cast<int>(mean > 0.0 ? std::sqrt(mean) : 0.0);
            const int now_ms  = total_ms_ + (static_cast<int>(i + 1) * 1000) / sample_rate_hz_;

            // Soft ambient floor tracking on quiet frames.
            if (energy < noise_floor_rms_ + 40) {
                noise_floor_rms_ = (noise_floor_rms_ * 7 + energy) / 8;
                if (noise_floor_rms_ < 40) {
                    noise_floor_rms_ = 40;
                }
                if (noise_floor_rms_ > 500) {
                    noise_floor_rms_ = 500;
                }
            }

            const int threshold =
                noise_floor_rms_ + std::max(kOnsetMinDeltaRms, (noise_floor_rms_ * 35) / 100);
            const bool rising =
                energy >= threshold && energy >= static_cast<int>(prev_energy_ * kOnsetRiseRatio);
            const int gap = now_ms - last_onset_ms_;
            if (rising && gap >= kMinOnsetGapMs) {
                if (gap <= kMaxOnsetGapMs && last_onset_ms_ > 0) {
                    PushGap(gap);
                }
                last_onset_ms_ = now_ms;
            }

            prev_energy_          = energy;
            samples_since_window_ = 0;
            window_sum_sq_        = 0;
        }

        total_ms_ += static_cast<int>((count * 1000) / sample_rate_hz_);
        RefreshEstimate();
    }

    int EstimateBeatPeriodMs() const
    {
        return estimated_period_ms_;
    }

    int EstimateBpm() const
    {
        return BeatPeriodMsToBpm(estimated_period_ms_);
    }

    int OnsetCount() const
    {
        return onset_count_;
    }

private:
    void PushGap(int gap_ms)
    {
        onset_gaps_ms_[onset_count_ % kOnsetHistoryCapacity] = gap_ms;
        ++onset_count_;
    }

    void RefreshEstimate()
    {
        const int available = onset_count_ < kOnsetHistoryCapacity ? onset_count_ : kOnsetHistoryCapacity;
        if (available < 3) {
            estimated_period_ms_ = BpmToBeatPeriodMs(kDefaultBpm);
            return;
        }

        int sorted[kOnsetHistoryCapacity];
        for (int i = 0; i < available; ++i) {
            const int idx =
                (onset_count_ - available + i + kOnsetHistoryCapacity * 4) % kOnsetHistoryCapacity;
            sorted[i] = onset_gaps_ms_[idx];
        }
        // Insertion sort is fine for N<=12.
        for (int i = 1; i < available; ++i) {
            const int key = sorted[i];
            int j         = i - 1;
            while (j >= 0 && sorted[j] > key) {
                sorted[j + 1] = sorted[j];
                --j;
            }
            sorted[j + 1] = key;
        }

        const int median = sorted[available / 2];
        estimated_period_ms_ = median;
        const int bpm        = BeatPeriodMsToBpm(median);
        estimated_period_ms_ = BpmToBeatPeriodMs(bpm);
    }

    int sample_rate_hz_       = kDefaultSampleRateHz;
    int window_samples_       = 640;
    int noise_floor_rms_      = 220;
    int prev_energy_          = 0;
    int samples_since_window_ = 0;
    uint64_t window_sum_sq_   = 0;
    int last_onset_ms_        = -100000;
    int onset_count_          = 0;
    int estimated_period_ms_  = BpmToBeatPeriodMs(kDefaultBpm);
    int total_ms_             = 0;
    int onset_gaps_ms_[kOnsetHistoryCapacity]{};
};

}  // namespace robot_dance_beat
