# StackChan-GPT v1.8.7: web search + idle camera + Whisper STT reliability

Combines live OpenAI web search (first landed in v1.8.6), deterministic idle camera tracking, and Whisper STT fixes into the v1.8.7 package.

## Web search

- HTTP chat and Realtime-fallback expose `self__web__search`, which calls `POST /v1/responses` with hosted `{ "type": "web_search" }`, `external_web_access: true`, and `tool_choice: required` on `gpt-4.1-mini`.
- GPT-Live `/v1/live/sessions` Responses delegation also registers the hosted `web_search` tool so news/local lookups can run in the backend without a device HTTP round-trip. `self__web__search` remains available.
- Completed searches with speakable text are accepted even when weather/sports/finance feeds omit `url_citation` annotations. URL sources are still collected when present.
- Default system prompt and Live instructions encourage web search for news, weather, sports scores, local businesses, and current events. There were no "do not discuss news" bans to remove; prompts previously simply never steered the model toward search.

## Whisper STT

- Close any leftover Live WebSocket before multipart Whisper uploads to free TLS/socket RAM.
- Retry once when `fetch_headers` returns no status (the previous `STT HTTP -1` failure mode).
- Slightly larger HTTP RX buffer and lower `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` to keep more internal SRAM for TLS.

## Idle camera tracking

See [IDLE_CAMERA_TRACKING_v1.8.7.md](IDLE_CAMERA_TRACKING_v1.8.7.md).

## Validation

- Host test `openai_web_search_test` covers request construction, hosted tool schema, sourced parsing, weather-without-URLs acceptance, and failure paths.
- Full ESP-IDF firmware build + merge-bin for flash-at-0x0 packaging.
