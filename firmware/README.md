# StackChan-GPT Firmware (`Agent.ai` + OpenAI API)

Modified M5Stack **StackChan** firmware (`ESP32-S3` / `M5Stack CoreS3`) that replaces the original cloud-bound AI backend with a **direct, on-device OpenAI API integration** (`Whisper STT` + `Chat Completions with Function/Tool Calling` + low-latency OpenAI PCM voice streaming + `OpenAI Vision`), while preserving compatibility with the **StackChan World** iOS app, Feetech SCS **servo motors**, and **12 RGB neon LEDs**.

---

## Key Features

1. **`Agent.ai` Launcher App**:
   - The main launcher app and settings menu are updated to **`Agent.ai`**.
   - Opening **`Agent.ai`** launches the OpenAI voice assistant directly on the device (`OpenaiProtocol`).
   - **No original AI server login/activation**: Bypasses `api.tenclass.net` / M5Stack cloud AI binding completely.
2. **Full OpenAI Voice & Response Pipeline**:
   - **Wake Word & Tap-to-Talk**: Say `"Hi StackChan"` (ESP-SR WakeNet9) or tap StackChan's face to start talking.
   - **Speech-to-Text (`POST /v1/audio/transcriptions`)**: Captures 16 kHz, 16-bit mono PCM from the 4-mic array (`ES7210`) with hardware AFE beamforming, echo cancellation, and VAD, and sends WAV audio to OpenAI (`whisper-1` / `gpt-4o-mini-transcribe`).
   - **Chat Completions + Tool Calling (`POST /v1/chat/completions`)**: Sends conversation history and all hardware MCP tools to OpenAI (`gpt-4o-mini` / `gpt-4o`).
   - **Low-Latency OpenAI Voice (`POST /v1/audio/speech`)**: Requests raw 24 kHz PCM from OpenAI TTS (`tts-1` or a configured compatible model) and streams it directly to the `AW88298` speaker, avoiding Ogg demuxing and Opus decoding. `GPT-Live` uses OpenAI's direct speech-to-speech PCM stream.
3. **Servo Motors, Facial Expressions & RGB LED Integration**:
   - **Automatic State Feedback**:
     - **Listening**: All 12 RGB LEDs glow emerald green (`0, 75, 25`) and the pitch servo tilts up attentively.
     - **Thinking**: RGB LEDs glow purple (`50, 15, 85`) with a `"Thinking..."` speech bubble and doubtful expression.
     - **Speaking**: RGB LEDs glow ocean cyan/blue (`0, 45, 95`), `SpeakingModifier` animates the mouth and performs natural speaking nods/yaw movements, and emotion tags (`[happy]`, `[laughing]`, `[sad]`, `[angry]`, `[doubtful]`, `[sleepy]`, `[neutral]`) drive facial decorators and servo postures.
   - **OpenAI Function Calling Tools**:
     - `self.robot.set_head_angles` / `self.robot.get_head_angles`: Move yaw (`-128°` to `128°`) and pitch (`0°` to `90°`) servos at custom speeds.
     - `self.robot.perform_gesture`: Trigger multi-keyframe animations (`nod`, `shake`, `happy_dance`, `robot_dance`, `look_around`, `panic`, `center`).
     - `self.robot.set_led_color` / `self.robot.set_led_side_color`: Set all 12 RGB LEDs or left/right 6-LED strips independently (`0-168` per channel). Custom LED colors set by the AI are preserved across states until turned off (`0, 0, 0`).
     - `self.robot.set_emotion`: Change facial emotion on screen.
     - `self.camera.take_photo`: Captures a JPEG photo from the CoreS3 GC0308 camera and analyzes it with OpenAI Vision (`gpt-4o-mini` / `gpt-4o`).
     - `self.robot.create_reminder` / `get_reminders` / `stop_reminder`, `self.audio_speaker.set_volume`, `self.screen.set_brightness`.
4. **StackChan World iOS App Compatibility**:
   - Keeps the NimBLE GATT services (`e2e5e5ff-1234-5678-1234-56789abcdef0` for Wi-Fi provisioning and `e2e5e5e0-1234-5678-1234-56789abcdef0` for Motion/Avatar/RGB control) and LAN WebSocket Avatar protocol intact.

---

## Connecting Your OpenAI API Key (3 Explicit Methods)

### Method 1: On-Device Touchscreen + Local Web Portal (No Reflash Required)
1. Connect StackChan to 2.4 GHz Wi-Fi (via the **StackChan World** iOS app over Bluetooth in `SETUP -> Wi-Fi -> Change Wi-Fi`, or via `main/openai_config.h`).
2. On StackChan's touchscreen, swipe to **`SETUP`** -> **`Agent.ai`** -> **`OpenAI API`**.
3. StackChan starts a local web server and displays a **QR Code** and URL (`http://<stackchan-ip>/`).
4. Scan the QR code with your iPhone or open `http://<stackchan-ip>/` in any browser on the same Wi-Fi network.
5. Paste your OpenAI API key (`sk-...`), choose your Chat Model (`gpt-4o-mini`, `gpt-4o`, etc.) and TTS Voice (`nova`, `alloy`, `shimmer`, `echo`, `fable`, `onyx`, `coral`, `ash`, `sage`), and click **Save OpenAI Configuration**.
   - You can also tap **Voice** and **Model** directly on StackChan's touchscreen in `SETUP -> Agent.ai -> OpenAI API`, or click **Test Servos & RGB LEDs** in the web portal to verify hardware motion and lighting immediately.

### Method 2: Compile-Time Configuration (`main/openai_config.h`)
Edit `firmware/main/openai_config.h` before building:
```cpp
#define OPENAI_API_KEY_DEFAULT       "sk-proj-YOUR_OPENAI_API_KEY"
#define OPENAI_BASE_URL_DEFAULT      "https://api.openai.com/v1"
#define OPENAI_CHAT_MODEL_DEFAULT    "gpt-4o-mini"
#define OPENAI_TTS_MODEL_DEFAULT     "tts-1"
#define OPENAI_TTS_VOICE_DEFAULT     "nova"
#define OPENAI_STT_MODEL_DEFAULT     "whisper-1"
#define OPENAI_WIFI_SSID_DEFAULT     "Your_2.4GHz_WiFi_SSID"
#define OPENAI_WIFI_PASSWORD_DEFAULT "Your_WiFi_Password"
```

### Method 3: Bluetooth Low Energy (BLE)
Write a JSON command to BLE Characteristic `e2e5e5e3-1234-5678-1234-56789abcdef0`:
```json
{
  "cmd": "setOpenAI",
  "data": {
    "apiKey": "sk-proj-YOUR_OPENAI_API_KEY",
    "model": "gpt-4o-mini",
    "voice": "nova",
    "baseUrl": "https://api.openai.com/v1"
  }
}
```

### Method 4: microSD backup / restore (survives full `0x0` flash)
On every save of Wi-Fi or OpenAI settings, firmware writes `/sdcard/stackchan_config.json` when a card is mounted (GPIO35 is remuxed safely around LCD D/C for the session). You can also drop a one-line key in `/sdcard/openai_key.txt` or `/sdcard/api_key.txt`.

On boot (`hal_openai::apply_boot_defaults` → `restore_from_sd_card`):

1. Empty NVS + SD backup present → restore Wi-Fi / OpenAI fields into NVS.
2. NVS populated + SD missing/outdated → snapshot NVS back to the card.
3. Empty NVS never clobbers an existing good SD backup.

**Flashing tip:** `idf.py flash` (app partition only) preserves NVS at `0x9000`. Flashing the merged `StackChan-GPT-v*.bin` at `0x0` (or M5Burner full erase) wipes NVS — leave the microSD in so settings come back on the next boot.

---

## Build & Flash Instructions

### 1. Fetch Dependencies
```bash
cd ~/Code/StackChan-GPT/firmware
python3 ./fetch_repos.py
```

### 2. Activate ESP-IDF v6.1 & Build
```bash
source ~/esp-idf/export.sh
idf.py build
```

### 3. Flash & Monitor
```bash
idf.py -p /dev/cu.usbmodem* flash monitor
```


## Live web search (v1.8.6)

Ask a self-contained question such as "Search the web for today's Los Angeles news" or
"What is the weather in Long Beach today?" The firmware exposes `self.web.search` to both
HTTP chat and GPT-Live/Realtime voice. It calls `/v1/responses` with hosted `web_search`
using `gpt-4.1-mini`, independently of your selected voice/chat model. The existing API key
and base URL are reused. A custom base URL must support Responses and hosted web search.
Search calls incur additional OpenAI API charges and may take longer than ordinary replies.

No additional setup is needed with the standard OpenAI base URL. Your account must have
access to the search model. Builders may override `OPENAI_WEB_SEARCH_MODEL_DEFAULT` in
`main/openai_config.h` with another compatible non-reasoning model.

The tool returns explicit errors on HTTP failures, incomplete results, or missing sources.
It requests live access, requires search execution, and retains the most recent answer and
source links in RAM. Open `SETUP -> GPT-Live -> OpenAI API`, scan the QR code, then select
**Latest web search and source links**. This page is on your local network and is cleared
on restart. It is not a remotely accessible browsing service.

For host tests after dependencies have been downloaded:

```bash
cmake -S tests -B build-host -DCJSON_DIR="$PWD/managed_components/espressif__cjson/cJSON"
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

Alternatively set `CJSON_DIR` to a checkout of cJSON v1.7.19. See
[build and validation notes](../docs/WEB_SEARCH_v1.8.6.md) for the delivered images.

## Beat-synced music dance (v1.8.9)

Ask StackChan to dance (or dance to the music). The LLM calls `self.robot.dance`, which estimates
BPM from microphone onset energy (default 120), then drives Feetech yaw/pitch servos and the left/right
neon strips in sync with the beat for 45 seconds by default (`stop=true` ends early). Wake STT is paused
for the dance; listening and thinking LED patterns are overridden. See
[v1.8.9 dance notes](../docs/DANCE_v1.8.9.md).

## Deterministic idle camera tracking (v1.8.7)

When the assistant has remained idle with audio playback drained for 30 seconds, the firmware
checks the CoreS3 camera every 10 seconds. Each check compares the luma values of two raw frames
captured 250 ms apart. A quadrant triggers tracking only when strictly more than 10% of its pixels
change after global brightness compensation.

During a tracking session, StackChan estimates the centroid of the changed pixels and adjusts yaw
and pitch toward the center of the frame. It samples movement at approximately one-second intervals
and stops after 30 seconds. Listening, speaking, notification playback, touch interaction, or another
servo modifier cancels tracking. Camera frames remain in volatile memory and are neither encoded for
upload nor sent to an AI model. See the
[v1.8.7 behavior and validation notes](../docs/IDLE_CAMERA_TRACKING_v1.8.7.md).
