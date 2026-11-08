/*
 * SPDX-FileCopyrightText: 2026 StackChan-GPT Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal/robot_dance_beat.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

void ExpectTrue(bool cond, const char* msg)
{
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

void TestBpmClamp()
{
    ExpectTrue(robot_dance_beat::BpmToBeatPeriodMs(120) == 500, "120 BPM -> 500 ms");
    ExpectTrue(robot_dance_beat::BeatPeriodMsToBpm(500) == 120, "500 ms -> 120 BPM");
    ExpectTrue(robot_dance_beat::BpmToBeatPeriodMs(40) == robot_dance_beat::BpmToBeatPeriodMs(70),
               "BPM floor");
    ExpectTrue(robot_dance_beat::BpmToBeatPeriodMs(300) == robot_dance_beat::BpmToBeatPeriodMs(160),
               "BPM ceiling");
}

void TestDefaultWithoutOnsets()
{
    robot_dance_beat::TempoTracker tracker(16000);
    std::vector<int16_t> quiet(1600, 20);  // 100 ms of near-silence
    for (int i = 0; i < 20; ++i) {
        tracker.FeedPcm(quiet.data(), quiet.size());
    }
    ExpectTrue(tracker.EstimateBpm() == robot_dance_beat::kDefaultBpm, "quiet audio keeps default BPM");
    ExpectTrue(tracker.OnsetCount() == 0, "no onsets on quiet audio");
}

void TestSyntheticBeatTrain()
{
    constexpr int kSampleRate = 16000;
    constexpr int kBpm        = 100;
    const int period_samples  = (kSampleRate * 60) / kBpm;

    robot_dance_beat::TempoTracker tracker(kSampleRate);

    // ~6 seconds of impulse clicks at 100 BPM with quiet between.
    for (int beat = 0; beat < 10; ++beat) {
        std::vector<int16_t> frame(period_samples, 30);
        // 8 ms loud click at the start of each beat window.
        const int click = (kSampleRate * 8) / 1000;
        for (int i = 0; i < click && i < static_cast<int>(frame.size()); ++i) {
            frame[i] = 12000;
        }
        tracker.FeedPcm(frame.data(), frame.size());
    }

    ExpectTrue(tracker.OnsetCount() >= 3, "synthetic beats produce onsets");
    const int bpm = tracker.EstimateBpm();
    ExpectTrue(bpm >= 90 && bpm <= 110, "estimated BPM near 100");
    std::printf("synthetic beat train estimated bpm=%d onsets=%d\n", bpm, tracker.OnsetCount());
}

void TestRmsHelper()
{
    int16_t zeros[8] = {};
    ExpectTrue(robot_dance_beat::ComputeRms(zeros, 8) == 0, "zero RMS");
    int16_t const_val[4] = {1000, 1000, 1000, 1000};
    ExpectTrue(robot_dance_beat::ComputeRms(const_val, 4) == 1000, "constant RMS");
}

}  // namespace

int main()
{
    TestBpmClamp();
    TestRmsHelper();
    TestDefaultWithoutOnsets();
    TestSyntheticBeatTrain();
    std::printf("robot_dance_beat_test: all passed\n");
    return 0;
}
