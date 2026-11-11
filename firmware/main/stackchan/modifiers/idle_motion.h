/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../modifiable.h"
#include "../utils/random.h"
#include <smooth_ui_toolkit.hpp>
// #include <mooncake_log.h>
#include <hal/hal.h>
#include <cstdint>

namespace stackchan {

/**
 * @brief
 *
 */
class IdleMotionModifier : public Modifier {
public:
    IdleMotionModifier(uint32_t interval_min = 4000, uint32_t interval_max = 8000, uint32_t burst_period_ms = 0,
                       uint32_t burst_max_duration_ms = 0)
        : _interval_min(interval_min),
          _interval_max(interval_max),
          _burst_period_ms(burst_period_ms),
          _burst_max_duration_ms(burst_max_duration_ms)
    {
        const uint32_t now = GetHAL().millis();
        if (_burst_period_ms > 0 && _burst_max_duration_ms > 0) {
            // Wait one full rest period (e.g. 5 min for Little, 3 min for Medium) before the first movement window
            _burst_start_ms = now + _burst_period_ms;
            _next_tick      = _burst_start_ms;
        } else {
            _next_tick = now + 1000;  // 启动 1 秒后开始第一次动作
        }
    }

    void pause()
    {
        _paused = true;
    }
    void resume()
    {
        if (_paused) {
            _paused            = false;
            const uint32_t now = GetHAL().millis();
            if (_burst_period_ms > 0 && _burst_max_duration_ms > 0) {
                _burst_start_ms = now + _burst_period_ms;
                _next_tick      = _burst_start_ms;
            } else {
                _next_tick = now + 500;
            }
        }
    }

    void _update(Modifiable& stackchan) override
    {
        if (_paused || !stackchan.hasAvatar()) return;

        uint32_t now = GetHAL().millis();

        if (_burst_period_ms > 0 && _burst_max_duration_ms > 0) {
            if (static_cast<int32_t>(now - _burst_start_ms) < 0) {
                return;
            }
            const uint32_t elapsed_in_burst = now - _burst_start_ms;
            if (elapsed_in_burst >= _burst_max_duration_ms) {
                if (stackchan.motion().isMoving() && !stackchan.motion().isModifyLocked()) {
                    stackchan.motion().stop();
                }
                while (static_cast<int32_t>(now - (_burst_start_ms + _burst_period_ms)) >= 0) {
                    _burst_start_ms += _burst_period_ms;
                }
                _burst_start_ms += _burst_period_ms;
                _next_tick = _burst_start_ms;
                return;
            }
        }

        // 如果时间没到，直接跳过
        if (static_cast<int32_t>(now - _next_tick) < 0) {
            return;
        }

        // 如果上次动作还没做完，就把下一次尝试推迟 500ms，避免指令堆积
        if (stackchan.motion().isMoving()) {
            _next_tick = now + 500;
            return;
        }

        // 执行动作
        perform_idle_motion(stackchan);

        // 算下一次的时间间隔
        uint32_t delay = Random::getInstance().getInt(_interval_min, _interval_max);
        _next_tick     = now + delay;
        if (_burst_period_ms > 0 && _burst_max_duration_ms > 0) {
            const uint32_t burst_end_ms = _burst_start_ms + _burst_max_duration_ms;
            if (static_cast<int32_t>(_next_tick - burst_end_ms) >= 0) {
                _next_tick = burst_end_ms;
            }
        }
        // mclog::info("next idle motion in {} ms", delay);
    }

private:
    void perform_idle_motion(Modifiable& stackchan)
    {
        auto& motion = stackchan.motion();
        if (motion.isModifyLocked()) {
            return;
        }

        int action = Random::getInstance().getInt(0, 100);

        if (action < 50) {
            // 【动作 1：随意环视】使用归一化坐标 (-1.0 ~ 1.0)
            float target_x = Random::getInstance().getFloat(-0.4f, 0.4f);   // 左右看
            float target_y = Random::getInstance().getFloat(-0.95f, 0.2f);  // 上下看
            int speed      = Random::getInstance().getInt(150, 300);

            // mclog::info("action 1: look at normalized ({}, {}) in speed {}", target_x, target_y, speed);
            motion.lookAtNormalized(target_x, target_y, speed);
        } else if (action < 80) {
            // 【动作 2：微小的观察动作】基于当前位置的小偏移
            auto current = motion.getCurrentAngles();  // Vector2i(yaw, pitch)

            int diff_yaw   = Random::getInstance().getInt(-150, 150);
            int diff_pitch = Random::getInstance().getInt(-80, 80);

            int target_yaw   = uitk::clamp(current.x + diff_yaw, -800, 800);
            int target_pitch = uitk::clamp(current.y + diff_pitch, 0, 600);
            int speed        = Random::getInstance().getInt(100, 250);

            // mclog::info("action 2: small move to ({}, {}) in speed {}", target_yaw, target_pitch, speed);
            motion.moveWithSpeed(target_yaw, target_pitch, speed);
        } else if (action < 90) {
            // 【动作 3：快速撇一眼】速度快，跨度中等
            int target_yaw   = Random::getInstance().getInt(-500, 500);
            int target_pitch = Random::getInstance().getInt(100, 400);
            int speed        = Random::getInstance().getInt(250, 400);

            // mclog::info("action 3: quick glance to ({}, {}) in speed {}", target_yaw, target_pitch, speed);
            motion.moveWithSpeed(target_yaw, target_pitch, speed);
        } else {
            // 【动作 4：yaw 回正】
            int target_pitch = Random::getInstance().getInt(50, 400);
            int speed        = Random::getInstance().getInt(100, 300);

            // mclog::info("action 4: go home to (0, {}) in speed {}", target_pitch, speed);
            motion.moveWithSpeed(0, target_pitch, speed);
        }
    }

    uint32_t _interval_min;
    uint32_t _interval_max;
    uint32_t _burst_period_ms       = 0;
    uint32_t _burst_max_duration_ms = 0;
    uint32_t _burst_start_ms        = 0;
    uint32_t _next_tick             = 0;
    bool _paused                    = false;
};

}  // namespace stackchan
