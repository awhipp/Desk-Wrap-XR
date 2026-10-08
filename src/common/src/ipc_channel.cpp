#include "deskwrap/common/ipc_channel.hpp"
#include <iostream>

namespace deskwrap::ipc {

// ==============================================================================
// IpcServer Implementation
// ==============================================================================

IpcServer::IpcServer() {
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

IpcServer::~IpcServer() {
    stop();
    if (stop_event_) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }
}

bool IpcServer::start(const std::wstring& pipe_name) {
    if (running_.load()) {
        return false;
    }

    pipe_name_ = pipe_name;
    ResetEvent(stop_event_);
    running_ = true;
    worker_thread_ = std::thread(&IpcServer::server_worker, this);
    return true;
}

void IpcServer::stop() {
    if (!running_.load()) {
        return;
    }

    running_ = false;
    if (stop_event_) {
        SetEvent(stop_event_);
    }

    if (pipe_handle_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipe_handle_, nullptr);
        CloseHandle(pipe_handle_);
        pipe_handle_ = INVALID_HANDLE_VALUE;
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    connected_ = false;
}

bool IpcServer::send_message(MessageType type, const void* payload, uint32_t payload_size) {
    if (!connected_.load() || pipe_handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);

    MessageHeader header{};
    header.version = IPC_PROTOCOL_VERSION;
    header.type = type;
    header.payload_size = payload_size;
    header.sequence = ++sequence_;

    std::vector<uint8_t> buffer(sizeof(MessageHeader) + payload_size);
    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    if (payload && payload_size > 0) {
        std::memcpy(buffer.data() + sizeof(MessageHeader), payload, payload_size);
    }

    DWORD bytes_written = 0;
    BOOL ok = WriteFile(
        pipe_handle_,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        &bytes_written,
        nullptr
    );

    return ok && (bytes_written == buffer.size());
}

bool IpcServer::send_shared_texture(const SharedTexturePayload& payload) {
    return send_message(MessageType::SharedTextureHandle, &payload, sizeof(payload));
}

bool IpcServer::send_frame_acquired(uint64_t frame_index, uint64_t timestamp_us, const RECT& dirty_rect) {
    FrameAcquiredPayload payload{};
    payload.frame_index = frame_index;
    payload.timestamp_us = timestamp_us;
    payload.dirty_rect = dirty_rect;
    return send_message(MessageType::FrameAcquired, &payload, sizeof(payload));
}

void IpcServer::server_worker() {
    while (running_.load()) {
        pipe_handle_ = CreateNamedPipeW(
            pipe_name_.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            65536,
            65536,
            0,
            nullptr
        );

        if (pipe_handle_ == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        OVERLAPPED connect_ov{};
        connect_ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        BOOL connected = ConnectNamedPipe(pipe_handle_, &connect_ov);
        if (!connected) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE events[2] = {connect_ov.hEvent, stop_event_};
                DWORD wait_res = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (wait_res == WAIT_OBJECT_0) {
                    connected = TRUE;
                }
            } else if (err == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            }
        }

        CloseHandle(connect_ov.hEvent);

        if (!connected || !running_.load()) {
            if (pipe_handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(pipe_handle_);
                pipe_handle_ = INVALID_HANDLE_VALUE;
            }
            continue;
        }

        connected_ = true;

        // Read messages loop
        OVERLAPPED read_ov{};
        read_ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::vector<uint8_t> read_buf(65536);

        while (running_.load() && connected_.load()) {
            ResetEvent(read_ov.hEvent);
            DWORD bytes_read = 0;
            BOOL read_ok = ReadFile(
                pipe_handle_,
                read_buf.data(),
                static_cast<DWORD>(read_buf.size()),
                &bytes_read,
                &read_ov
            );

            if (!read_ok) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    HANDLE wait_evs[2] = {read_ov.hEvent, stop_event_};
                    DWORD wait_res = WaitForMultipleObjects(2, wait_evs, FALSE, INFINITE);
                    if (wait_res == WAIT_OBJECT_0) {
                        GetOverlappedResult(pipe_handle_, &read_ov, &bytes_read, FALSE);
                        read_ok = TRUE;
                    } else {
                        break;
                    }
                } else {
                    // Pipe disconnected
                    break;
                }
            }

            if (bytes_read >= sizeof(MessageHeader)) {
                MessageHeader header;
                std::memcpy(&header, read_buf.data(), sizeof(MessageHeader));

                std::vector<uint8_t> payload;
                if (bytes_read > sizeof(MessageHeader)) {
                    payload.assign(read_buf.data() + sizeof(MessageHeader), read_buf.data() + bytes_read);
                }

                if (on_message_) {
                    on_message_(header, payload);
                }
            }
        }

        CloseHandle(read_ov.hEvent);
        connected_ = false;

        if (pipe_handle_ != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(pipe_handle_);
            CloseHandle(pipe_handle_);
            pipe_handle_ = INVALID_HANDLE_VALUE;
        }
    }
}

// ==============================================================================
// IpcClient Implementation
// ==============================================================================

IpcClient::IpcClient() {
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

IpcClient::~IpcClient() {
    disconnect();
    if (stop_event_) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }
}

bool IpcClient::connect(const std::wstring& pipe_name, uint32_t timeout_ms) {
    if (connected_.load()) {
        return true;
    }

    pipe_name_ = pipe_name;

    DWORD start_time = GetTickCount();
    while (true) {
        pipe_handle_ = CreateFileW(
            pipe_name_.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr
        );

        if (pipe_handle_ != INVALID_HANDLE_VALUE) {
            break;
        }

        if (GetLastError() != ERROR_PIPE_BUSY) {
            if (GetTickCount() - start_time >= timeout_ms) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (!WaitNamedPipeW(pipe_name_.c_str(), timeout_ms)) {
            return false;
        }
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe_handle_, &mode, nullptr, nullptr);

    ResetEvent(stop_event_);
    running_ = true;
    connected_ = true;

    worker_thread_ = std::thread(&IpcClient::client_worker, this);
    return true;
}

void IpcClient::disconnect() {
    if (!running_.load()) {
        return;
    }

    running_ = false;
    if (stop_event_) {
        SetEvent(stop_event_);
    }

    if (pipe_handle_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipe_handle_, nullptr);
        CloseHandle(pipe_handle_);
        pipe_handle_ = INVALID_HANDLE_VALUE;
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    connected_ = false;
}

bool IpcClient::send_message(MessageType type, const void* payload, uint32_t payload_size) {
    if (!connected_.load() || pipe_handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);

    MessageHeader header{};
    header.version = IPC_PROTOCOL_VERSION;
    header.type = type;
    header.payload_size = payload_size;
    header.sequence = ++sequence_;

    std::vector<uint8_t> buffer(sizeof(MessageHeader) + payload_size);
    std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
    if (payload && payload_size > 0) {
        std::memcpy(buffer.data() + sizeof(MessageHeader), payload, payload_size);
    }

    DWORD bytes_written = 0;
    BOOL ok = WriteFile(
        pipe_handle_,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        &bytes_written,
        nullptr
    );

    return ok && (bytes_written == buffer.size());
}

bool IpcClient::send_hello(uint32_t client_pid) {
    HelloPayload payload{};
    payload.client_pid = client_pid;
    return send_message(MessageType::Hello, &payload, sizeof(payload));
}

bool IpcClient::send_recenter(float yaw_offset_deg) {
    RecenterPayload payload{};
    payload.yaw_offset_deg = yaw_offset_deg;
    return send_message(MessageType::Recenter, &payload, sizeof(payload));
}

void IpcClient::client_worker() {
    OVERLAPPED read_ov{};
    read_ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::vector<uint8_t> read_buf(65536);

    while (running_.load() && connected_.load()) {
        ResetEvent(read_ov.hEvent);
        DWORD bytes_read = 0;
        BOOL read_ok = ReadFile(
            pipe_handle_,
            read_buf.data(),
            static_cast<DWORD>(read_buf.size()),
            &bytes_read,
            &read_ov
        );

        if (!read_ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE wait_evs[2] = {read_ov.hEvent, stop_event_};
                DWORD wait_res = WaitForMultipleObjects(2, wait_evs, FALSE, INFINITE);
                if (wait_res == WAIT_OBJECT_0) {
                    GetOverlappedResult(pipe_handle_, &read_ov, &bytes_read, FALSE);
                    read_ok = TRUE;
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        if (bytes_read >= sizeof(MessageHeader)) {
            MessageHeader header;
            std::memcpy(&header, read_buf.data(), sizeof(MessageHeader));

            std::vector<uint8_t> payload;
            if (bytes_read > sizeof(MessageHeader)) {
                payload.assign(read_buf.data() + sizeof(MessageHeader), read_buf.data() + bytes_read);
            }

            if (on_message_) {
                on_message_(header, payload);
            }
        }
    }

    CloseHandle(read_ov.hEvent);
    connected_ = false;
}

} // namespace deskwrap::ipc
