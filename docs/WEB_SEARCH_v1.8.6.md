# StackChan-GPT v1.8.6: live web search

Adds `self.web.search` to the existing Chat Completions and GPT-Live/Realtime function-tool paths. The firmware makes a separate OpenAI Responses request with `web_search`, `tool_choice: required`, and live external access. It uses `gpt-4.1-mini` for search and the API key and base URL already configured on the robot. No new backend or additional API key is required.

The conversation prompt and GPT-Live delegation prompt now route current-information questions to this function. Hardware functions and the selected conversation model remain available. A final Chat Completions round summarizes tool outputs when the function-call budget is exhausted.

Search responses are checked for completion, an actual completed search, answer text, and source URLs. Invalid queries, HTTP errors, oversized responses, incomplete outputs and unsourced answers produce explicit tool errors. Network read timeout for searches is 90 seconds; normal calls retain their existing 35-second setting. Search HTTP calls run on the existing worker, outside the WebSocket receive callback. Cancellation is checked before and after the HTTP call; a blocked read can take up to the configured timeout to return.

Source annotations are retained for clickable citations in **Latest web search and source links**, accessible from the local setup portal. Only the latest result is held in RAM, cleared on reboot. Web-derived strings are rendered as text, and links are restricted to HTTP/HTTPS. Search content is treated as untrusted data in both prompts.

## Validation

- Host tests cover tool schemas for both voice paths, query validation, required/live search request flags, interleaved Responses items, multiple text blocks, source deduplication, inline annotations, invalid JSON, incomplete/failed responses, missing search execution, missing sources, and error escaping.
- Existing motion math host tests pass.
- Setup portal JavaScript syntax checked with Node.
- Firmware built with ESP-IDF v6.1, ESP32-S3 target, repository sdkconfig.defaults and locked component dependencies. No credential overlay is used.
- No physical robot or live authenticated OpenAI request was available. Voice, hardware timing and account/model access still need a device smoke test.

## Device smoke test after flashing

1. Confirm the UI reports v1.8.6 and your Wi-Fi/API configuration is restored.
2. Ask for today's news in a specified city. It should search and name sources and dates.
3. Repeat in GPT-Live/Realtime mode. Check the serial log for `Searching the web via Responses API`.
4. Open the setup portal's latest-search page and follow the citations.
5. Ask a general knowledge question, then request a head movement and LED color change.
6. On a failed lookup, confirm the robot reports failure rather than inventing live information.

## Flashing

`StackChan-GPT-v1.8.6.bin` is the merged image for flashing at **0x0** on the 16 MB ESP32-S3 StackChan. Keep a current microSD settings backup before flashing: padding in a merged image overwrites the NVS settings area. The binary has no embedded API key or Wi-Fi credentials. It is not an OTA application-only image.

From the firmware directory, with ESP-IDF v6.1 installed and exported:

```bash
python3 fetch_repos.py
idf.py build
idf.py merge-bin -o ../../StackChan-GPT-v1.8.6.bin
cmake -S tests -B build-host -DCJSON_DIR="$PWD/managed_components/espressif__cjson/cJSON"
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

OpenAI references: [Web search](https://developers.openai.com/api/docs/guides/tools-web-search) and [GPT-4.1 mini](https://developers.openai.com/api/docs/models/gpt-4.1-mini). Search calls incur OpenAI tool and token charges; custom API gateways must support Responses hosted web search.

## Binary manifest

- Base source commit: `f5e963dab122877aa05aa6336a97043e964ec1ea`.
- ESP-IDF: `v6.1` (`fff9895c82d744c7237be8847347bdd1b07c6643`).
- Merged image: 12,857,054 bytes; address `0x0`.
- Application: 3,944,656 bytes; partition limit 5,177,344 bytes (about 24% free).
- SHA-256: `e42f58f7866c37234356c0d7041f48336c7a911042bb289cf2cca857d10abb0a`.
- All five embedded flash components byte-compared against the build outputs. Application image checksum and validation hash passed esptool verification.
