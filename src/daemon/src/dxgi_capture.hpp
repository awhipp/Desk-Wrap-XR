#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include "deskwrap/common/types.hpp"
#include "deskwrap/common/ipc_protocol.hpp"

namespace deskwrap::daemon {

using Microsoft::WRL::ComPtr;

class DxgiDesktopCapture {
public:
    DxgiDesktopCapture() = default;
    ~DxgiDesktopCapture();

    bool initialize(const std::wstring& target_device_name = L"", const DisplayMode& fallback_mode = {});
    void shutdown();

    bool acquire_frame(uint64_t timeout_ms, ipc::RectPod& out_dirty_rect);
    void release_frame();

    HANDLE get_shared_texture_handle() const { return shared_handle_; }
    const std::wstring& get_shared_name() const { return shared_name_; }
    bool has_keyed_mutex() const { return keyed_mutex_ != nullptr; }
    uint32_t get_width() const { return width_; }
    uint32_t get_height() const { return height_; }
    uint64_t get_frame_index() const { return frame_index_; }

    ipc::SharedTexturePayload get_shared_texture_payload() const;

private:
    bool create_d3d11_device();
    bool initialize_duplication(const std::wstring& target_device_name);
    bool create_shared_texture();

    ComPtr<ID3D11Device> d3d_device_;
    ComPtr<ID3D11Device1> d3d_device1_;
    ComPtr<ID3D11DeviceContext> d3d_context_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<ID3D11Texture2D> shared_texture_;
    ComPtr<IDXGIKeyedMutex> keyed_mutex_;
    HANDLE shared_handle_ = nullptr;
    std::wstring shared_name_;
    std::wstring target_device_name_;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool frame_acquired_ = false;
    uint64_t frame_index_ = 0;
};

} // namespace deskwrap::daemon
