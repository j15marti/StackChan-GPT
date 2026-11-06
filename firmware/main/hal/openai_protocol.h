/*
 * SPDX-FileCopyrightText: 2026 StackChan-GPT Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <protocol.h>
#include <web_socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class OpenaiProtocol : public Protocol {
public:
    OpenaiProtocol();
    ~OpenaiProtocol() override;

    bool Start() override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel(bool send_goodbye = true) override;
    bool IsAudioChannelOpened() const override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    void SendWakeWordDetected(const std::string& wake_word) override;
    void SendStartListening(ListeningMode mode) override;
    void SendStopListening() override;
    void SendAbortSpeaking(AbortReason reason) override;
    void SendMcpMessage(const std::string& message) override;

protected:
    bool SendText(const std::string& text) override;
    bool IsTimeout() const override;

private:
    struct ChatTurn {
        std::string role;
        std::string content;
    };

    static constexpr EventBits_t kEventProcessUtterance = (1 << 0);
    static constexpr EventBits_t kEventStopWorker       = (1 << 1);
    static constexpr EventBits_t kEventLiveStarted      = (1 << 2);
    static constexpr EventBits_t kEventCloseLiveWs      = (1 << 4);

    EventGroupHandle_t worker_events_ = nullptr;
    TaskHandle_t worker_task_         = nullptr;
    StaticTask_t* worker_task_buffer_ = nullptr;
    StackType_t* worker_task_stack_   = nullptr;

    std::atomic<bool> channel_opened_{false};
    std::atomic<bool> listening_{false};
    std::atomic<bool> processing_{false};
    std::atomic<bool> abort_requested_{false};
    std::atomic<bool> wake_check_only_{false};
    ListeningMode listening_mode_ = kListeningModeAutoStop;

    mutable std::mutex audio_mutex_;
    std::deque<std::vector<int16_t>> preroll_frames_;
    std::vector<int16_t> utterance_pcm_;
    std::vector<int16_t> ready_pcm_;
    bool speech_detected_ = false;
    int speech_ms_        = 0;
    int silence_ms_       = 0;
    int listen_wait_ms_   = 0;

    std::mutex history_mutex_;
    std::deque<ChatTurn> history_;

    // GPT-Live / Realtime WebSocket state
    std::mutex live_mutex_;
    std::unique_ptr<WebSocket> live_ws_;
    std::atomic<bool> live_session_ready_{false};
    std::atomic<bool> live_is_realtime_api_{false};
    std::atomic<bool> live_turn_active_{false};
    std::atomic<bool> live_speaking_{false};
    std::atomic<bool> live_output_started_{false};
    std::atomic<bool> live_delegation_active_{false};
    std::atomic<int64_t> last_live_audio_rx_us_{0};
    std::string live_transcript_accum_;
    std::vector<int16_t> live_pcm_accum_;
    std::vector<int16_t> live_pre_onset_pcm_;

    void WorkerLoop();
    void ProcessUtterance(std::vector<int16_t>&& pcm, bool is_wake_check);
    void EmitJsonString(const std::string& json_str);
    void EmitTtsState(const char* state, const char* text = nullptr);
    void EmitSttText(const std::string& text);
    void EmitEmotion(const std::string& emotion);

    bool IsLiveModelConfigured() const;
    bool EnsureLiveWebSocketConnected(std::string& out_error);
    void CloseLiveWebSocket();
    void HandleLiveServerEvent(const char* data, size_t len);
    bool StreamUtteranceOverLiveWs(const std::vector<int16_t>& pcm, std::string& out_error);
    std::string ExecuteLocalMcpTool(const std::string& mcp_tool_name, const std::string& args_str);
    cJSON* BuildMcpToolsForOpenai(bool flat_realtime_format);

    bool TranscribeWav(const std::vector<int16_t>& pcm, std::string& out_text, std::string& out_error);
    bool RunChatCompletionWithTools(const std::string& user_text, std::string& out_reply, std::string& out_emotion,
                                    std::string& out_error);
    bool StreamTtsAudio(const std::string& text, std::string& out_error);
    std::string ExplainCameraPhotoWithOpenai(const std::string& question);
    std::string SearchWebWithOpenai(const std::string& arguments);
};
