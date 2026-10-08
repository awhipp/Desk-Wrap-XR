#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include "deskwrap/common/ipc_protocol.hpp"

namespace deskwrap::ipc {

class IpcServer {
public:
    IpcServer();
    ~IpcServer();

    bool start(const std::wstring& pipe_name = DEFAULT_PIPE_NAME);
    void stop();

    bool send_message(MessageType type, const void* payload, uint32_t payload_size);
    bool send_shared_texture(const SharedTexturePayload& payload);
    bool send_frame_acquired(uint64_t frame_index, uint64_t timestamp_us, const RECT& dirty_rect);

    bool is_connected() const { return connected_.load(); }
    void set_on_message_callback(std::function<void(const MessageHeader&, const std::vector<uint8_t>&)> cb) {
        on_message_ = std::move(cb);
    }

private:
    void server_worker();

    std::wstring pipe_name_;
    HANDLE pipe_handle_ = INVALID_HANDLE_VALUE;
    HANDLE stop_event_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread worker_thread_;
    std::mutex send_mutex_;
    uint32_t sequence_ = 0;
    std::function<void(const MessageHeader&, const std::vector<uint8_t>&)> on_message_;
};

class IpcClient {
public:
    IpcClient();
    ~IpcClient();

    bool connect(const std::wstring& pipe_name = DEFAULT_PIPE_NAME, uint32_t timeout_ms = 5000);
    void disconnect();

    bool send_message(MessageType type, const void* payload, uint32_t payload_size);
    bool send_hello(uint32_t client_pid);
    bool send_recenter(float yaw_offset_deg);

    bool is_connected() const { return connected_.load(); }
    void set_on_message_callback(std::function<void(const MessageHeader&, const std::vector<uint8_t>&)> cb) {
        on_message_ = std::move(cb);
    }

private:
    void client_worker();

    std::wstring pipe_name_;
    HANDLE pipe_handle_ = INVALID_HANDLE_VALUE;
    HANDLE stop_event_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread worker_thread_;
    std::mutex send_mutex_;
    uint32_t sequence_ = 0;
    std::function<void(const MessageHeader&, const std::vector<uint8_t>&)> on_message_;
};

} // namespace deskwrap::ipc
