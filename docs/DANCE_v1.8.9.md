# StackChan-GPT v1.8.9: beat-synced music dance

Version 1.8.9 adds a dedicated LLM tool so StackChan can dance to ambient music with
microphone-based tempo estimation, Feetech head servos, and the 12 onboard RGB LEDs.

## Tool

- **Name:** `self.robot.dance` (OpenAI / GPT-Live flat name: `self__robot__dance`)
- **Also reachable via:** `self.robot.perform_gesture` with `gesture=dance`
- **Parameters:**
  - `duration_seconds` (integer, default **45**, range 5–120)
  - `stop` (boolean, default false) — end an active dance early

## Beat detection

1. While dancing, `OpenaiProtocol::SendAudio` feeds 16 kHz mic PCM into a lightweight onset tracker
   and skips wake-word / STT capture for that period.
2. Energy is computed over ~40 ms windows. An onset is declared when RMS rises above an adaptive
   noise floor (rise ratio ≈ 1.45 and minimum delta).
3. Inter-onset intervals (clamped ≈ 70–160 BPM) yield a median beat period. With fewer than three
   valid gaps the firmware uses **120 BPM** (500 ms).
4. Tempo is re-estimated about every 4 seconds from ongoing mic audio.

## Motion pattern

Angles are tenths of a degree and clamped to safe StackChan ranges (yaw ±100°, pitch ≈5–80°):

| Phase (16 beats) | Odd beats (1,3,…) | Even beats (2,4,…) |
|------------------|-------------------|--------------------|
| Up / down        | head up           | head down          |
| Right / left     | head right        | head left          |

Phases alternate until the duration timeout or `stop=true`.

## LED pattern

Left and right neon strips are 6 RGB LEDs each (12 total via PY32 / existing HAL RGB API).
Each beat lights one side and clears the other, repeating:

right red → left red → right blue → left blue → right green → left green → right purple → left purple → …

`hal_openai::set_custom_led_active(true)` keeps green listening LEDs and the thinking heartbeat from
overwriting the dance sequence.

## Safety and coexistence

- Servo targets use `moveWithSpeed` on the existing Feetech SCS APIs with clamped angles.
- Motion modify-lock is held so idle camera tracking and other modifiers do not fight the dance.
- Idle camera tracking treats an active dance as “not at rest.”
- After the dance ends, the head returns home and LEDs clear.

## Release artifact

- Merged image: `StackChan-GPT-v1.8.9.bin` (root + `firmware/`)
- SHA-256: `73ac3000c44a741b4f562e04d0e7eb98e8818f98c0dea081fcc4b2a1b1a3733b`

## Automated validation

Host tests cover BPM clamping, quiet-audio default tempo, and a synthetic 100 BPM click train.

```bash
cd firmware
cmake -S tests -B build-host -DCJSON_DIR="$PWD/managed_components/espressif__cjson/cJSON"
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

## Build / flash

```bash
cd firmware
source ~/esp-idf/export.sh
idf.py build
idf.py merge-bin -o ../../StackChan-GPT-v1.8.9.bin
# Prefer app-only flash to preserve NVS:
idf.py -p /dev/cu.usbmodem* flash
```

## How to test on hardware

1. Flash **v1.8.9**, confirm the UI shows `V1.8.9`, and restore Wi-Fi / API settings if needed.
2. Play music near StackChan (or clap a steady beat).
3. Say the wake word, then “dance to the music” (or “dance”).
4. Confirm head up/down for ~16 beats, then left/right, with alternating side LEDs through the color cycle.
5. Say “stop dancing” (or wait ~45 s) and confirm the head centers and LEDs clear.
6. Confirm wake listening resumes afterward.
