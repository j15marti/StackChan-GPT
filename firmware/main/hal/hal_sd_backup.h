/*
 * SPDX-FileCopyrightText: 2026 StackChan-GPT Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "hal_openai.h"
#include <string>
#include <vector>

// Mirrors Wi-Fi credentials and OpenAI settings to the microSD card so they
// survive full-flash updates (which wipe NVS).
namespace hal_sd_backup {

struct WifiCredential_t {
    std::string ssid;
    std::string password;
};

struct Backup_t {
    std::vector<WifiCredential_t> wifi;
    OpenaiConfig_t openai;  // Only fields that were explicitly stored; empty fields mean "unset".
};

struct LoadResult_t {
    bool cardPresent = false;
    bool hasBackup   = false;
    Backup_t backup;
};

LoadResult_t load();

// Snapshots the current NVS state and writes it to the card from a background task.
void request_save();

}  // namespace hal_sd_backup
