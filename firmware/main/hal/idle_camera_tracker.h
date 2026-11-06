/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

// Starts the deterministic idle camera motion tracker task once. The tracker
// never uploads frames and does not call an AI model.
void start_idle_camera_tracker();
