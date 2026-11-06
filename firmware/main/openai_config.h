/*
 * SPDX-FileCopyrightText: 2026 StackChan-GPT Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <sdkconfig.h>

/**
 * ============================================================================
 * StackChan-GPT OpenAI Configuration
 * ============================================================================
 *
 * You can configure your OpenAI API key and preferences in any of 3 ways:
 *
 * 1. COMPILE-TIME (this file or sdkconfig.defaults.local):
 *    Paste your OpenAI API key into OPENAI_API_KEY_DEFAULT below (or set
 *    CONFIG_OPENAI_API_KEY in firmware/sdkconfig.defaults.local) before
 *    running `idf.py build flash`.
 *
 * 2. ON-DEVICE WEB PORTAL (No reflash needed):
 *    On StackChan's screen, open `SETUP -> Agent.ai -> OpenAI API`.
 *    Scan the QR code with your iPhone / open `http://<stackchan-ip>/` in
 *    any browser on the same Wi-Fi network, and paste your OpenAI API key.
 *    You can also pick your preferred OpenAI voice and model directly on
 *    the touchscreen.
 *
 * 3. BLUETOOTH (BLE Config Characteristic e2e5e5e3-1234-5678-1234-56789abcdef0):
 *    Send JSON:
 *    {"cmd":"setOpenAI","data":{"apiKey":"sk-...","model":"gpt-4o-mini","voice":"nova"}}
 * ============================================================================
 */

#ifndef OPENAI_API_KEY_DEFAULT
#ifdef CONFIG_OPENAI_API_KEY
#define OPENAI_API_KEY_DEFAULT CONFIG_OPENAI_API_KEY
#else
#define OPENAI_API_KEY_DEFAULT ""
#endif
#endif

#ifndef OPENAI_BASE_URL_DEFAULT
#ifdef CONFIG_OPENAI_BASE_URL
#define OPENAI_BASE_URL_DEFAULT CONFIG_OPENAI_BASE_URL
#else
#define OPENAI_BASE_URL_DEFAULT "https://api.openai.com/v1"
#endif
#endif

#ifndef OPENAI_CHAT_MODEL_DEFAULT
#ifdef CONFIG_OPENAI_CHAT_MODEL
#define OPENAI_CHAT_MODEL_DEFAULT CONFIG_OPENAI_CHAT_MODEL
#else
#define OPENAI_CHAT_MODEL_DEFAULT "gpt-4o-mini"
#endif
#endif

#ifndef OPENAI_STT_MODEL_DEFAULT
#ifdef CONFIG_OPENAI_STT_MODEL
#define OPENAI_STT_MODEL_DEFAULT CONFIG_OPENAI_STT_MODEL
#else
#define OPENAI_STT_MODEL_DEFAULT "whisper-1"
#endif
#endif

#ifndef OPENAI_TTS_MODEL_DEFAULT
#ifdef CONFIG_OPENAI_TTS_MODEL
#define OPENAI_TTS_MODEL_DEFAULT CONFIG_OPENAI_TTS_MODEL
#else
#define OPENAI_TTS_MODEL_DEFAULT "tts-1"
#endif
#endif

#ifndef OPENAI_TTS_VOICE_DEFAULT
#ifdef CONFIG_OPENAI_TTS_VOICE
#define OPENAI_TTS_VOICE_DEFAULT CONFIG_OPENAI_TTS_VOICE
#else
#define OPENAI_TTS_VOICE_DEFAULT "nova"
#endif
#endif

#ifndef OPENAI_SYSTEM_PROMPT_DEFAULT
#define OPENAI_SYSTEM_PROMPT_DEFAULT                                                                                   \
    "You are StackChan, a super kawaii, friendly, and expressive desktop robot powered by M5Stack CoreS3. "            \
    "You have a 2-axis motorized head (yaw left/right and pitch up/down servos), 12 RGB neon LEDs on your body, "      \
    "an animated face on your screen, and a camera. "                                                                  \
    "Always begin your final spoken reply with an emotion tag in brackets from this list: "                            \
    "[neutral], [happy], [laughing], [sad], [angry], [doubtful], [sleepy]. "                                           \
    "Keep your spoken responses concise, warm, and natural for voice conversation (1 to 3 short sentences). "          \
    "Actively use your tools when asked to move/look/nod/shake/dance, change your LED light colors, take a photo, "    \
    "set reminders, or adjust volume/brightness. You may also proactively use gestures, head movements, or LED "       \
    "colors to make your reactions lively!"
#endif

/**
 * Optional compile-time Wi-Fi credentials.
 * If OPENAI_WIFI_SSID_DEFAULT is non-empty, StackChan will automatically
 * register this Wi-Fi network on boot (while still supporting Wi-Fi setup
 * via the StackChan World iOS app over Bluetooth).
 */
#ifndef OPENAI_WIFI_SSID_DEFAULT
#ifdef CONFIG_OPENAI_WIFI_SSID
#define OPENAI_WIFI_SSID_DEFAULT CONFIG_OPENAI_WIFI_SSID
#else
#define OPENAI_WIFI_SSID_DEFAULT ""
#endif
#endif

#ifndef OPENAI_WIFI_PASSWORD_DEFAULT
#ifdef CONFIG_OPENAI_WIFI_PASSWORD
#define OPENAI_WIFI_PASSWORD_DEFAULT CONFIG_OPENAI_WIFI_PASSWORD
#else
#define OPENAI_WIFI_PASSWORD_DEFAULT ""
#endif
#endif
