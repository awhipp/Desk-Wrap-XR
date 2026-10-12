#include "dxgi_capture.hpp"
#include <iostream>
#include <chrono>

namespace deskwrap::daemon {

DxgiDesktopCapture::~DxgiDesktopCapture() {
    shutdown();
}

bool DxgiDesktopCapture::initialize(const std::wstring& target_device_name, const DisplayMode& fallback_mode) {
    target_device_name_ = target_device_name;
    width_ = fallback_mode.width;
    height_ = fallback_mode.height;

    if (!create_d3d11_device()) {
        std::cerr << "[DxgiCapture] Failed to create D3D11 device.\n";
        return false;
    }

    if (!initialize_duplication(target_device_name_)) {
        std::cerr << "[DxgiCapture] Failed to initialize DXGI Desktop Duplication.\n";
        return false;
    }

    if (!create_shared_texture()) {
        std::cerr << "[DxgiCapture] Failed to create shared texture with keyed mutex.\n";
        return false;
    }

    std::cout << "[DxgiCapture] Capture initialized (" << width_ << "x" << height_
              << ", KeyedMutex: " << (keyed_mutex_ ? "YES" : "NO") << ").\n";
    return true;
}

void DxgiDesktopCapture::shutdown() {
    release_frame();
    keyed_mutex_.Reset();
    duplication_.Reset();
    shared_texture_.Reset();
    d3d_context_.Reset();
    d3d_device1_.Reset();
    d3d_device_.Reset();
    if (shared_handle_) {
        CloseHandle(shared_handle_);
        shared_handle_ = nullptr;
    }
    shared_name_.clear();
}

bool DxgiDesktopCapture::create_d3d11_device() {
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    D3D_FEATURE_LEVEL feature_level;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        feature_levels,
        ARRAYSIZE(feature_levels),
        D3D11_SDK_VERSION,
        &d3d_device_,
        &feature_level,
        &d3d_context_
    );

    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            feature_levels,
            ARRAYSIZE(feature_levels),
            D3D11_SDK_VERSION,
            &d3d_device_,
            &feature_level,
            &d3d_context_
        );
    }

    if (FAILED(hr)) return false;

    d3d_device_.As(&d3d_device1_);
    return true;
}

bool DxgiDesktopCapture::initialize_duplication(const std::wstring& target_device_name) {
    ComPtr<IDXGIDevice> dxgi_device;
    if (FAILED(d3d_device_.As(&dxgi_device))) return false;

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgi_device->GetAdapter(&adapter))) return false;

    ComPtr<IDXGIOutput> selected_output;
    ComPtr<IDXGIOutput> output;

    for (UINT i = 0; adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_OUTPUT_DESC desc{};
        output->GetDesc(&desc);

        if (!desc.AttachedToDesktop) {
            continue;
        }

        if (!target_device_name.empty()) {
            if (wcsstr(desc.DeviceName, target_device_name.c_str()) != nullptr) {
                selected_output = output;
                break;
            }
        } else {
            // Target the primary display if possible (coordinates at 0,0)
            if (desc.DesktopCoordinates.left == 0 && desc.DesktopCoordinates.top == 0) {
                selected_output = output;
                break;
            } else if (!selected_output) {
                selected_output = output;
            }
        }
    }

    if (!selected_output) {
        std::cerr << "[DxgiCapture] No suitable desktop output found for duplication.\n";
        return false;
    }

    DXGI_OUTPUT_DESC chosen_desc{};
    selected_output->GetDesc(&chosen_desc);
    uint32_t out_w = static_cast<uint32_t>(chosen_desc.DesktopCoordinates.right - chosen_desc.DesktopCoordinates.left);
    uint32_t out_h = static_cast<uint32_t>(chosen_desc.DesktopCoordinates.bottom - chosen_desc.DesktopCoordinates.top);
    if (out_w > 0 && out_h > 0) {
        width_ = out_w;
        height_ = out_h;
    }

    ComPtr<IDXGIOutput1> output1;
    if (FAILED(selected_output.As(&output1))) {
        return false;
    }

    HRESULT hr = output1->DuplicateOutput(d3d_device_.Get(), &duplication_);
    if (FAILED(hr)) {
        std::cerr << "[DxgiCapture] DuplicateOutput failed: 0x" << std::hex << hr << std::dec << "\n";
        return false;
    }

    return true;
}

bool DxgiDesktopCapture::create_shared_texture() {
    uint32_t pid = GetCurrentProcessId();
    shared_name_ = L"Local\\DeskWrapSharedDesktop_" + std::to_wstring(pid);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width_;
    desc.Height = height_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    HRESULT hr = d3d_device_->CreateTexture2D(&desc, nullptr, &shared_texture_);
    if (SUCCEEDED(hr)) {
        ComPtr<IDXGIResource1> dxgi_resource1;
        if (SUCCEEDED(shared_texture_.As(&dxgi_resource1))) {
            hr = dxgi_resource1->CreateSharedHandle(
                nullptr,
                DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                shared_name_.c_str(),
                &shared_handle_
            );
            if (SUCCEEDED(hr)) {
                shared_texture_.As(&keyed_mutex_);
                return true;
            }
        }
    }

    // Secondary attempt: Legacy Shared Handle with Keyed Mutex
    shared_texture_.Reset();
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    hr = d3d_device_->CreateTexture2D(&desc, nullptr, &shared_texture_);
    if (SUCCEEDED(hr)) {
        ComPtr<IDXGIResource> dxgi_resource;
        if (SUCCEEDED(shared_texture_.As(&dxgi_resource))) {
            hr = dxgi_resource->GetSharedHandle(&shared_handle_);
            if (SUCCEEDED(hr)) {
                shared_texture_.As(&keyed_mutex_);
                shared_name_.clear();
                return true;
            }
        }
    }

    // Tertiary fallback: Standard legacy shared handle without keyed mutex
    shared_texture_.Reset();
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    hr = d3d_device_->CreateTexture2D(&desc, nullptr, &shared_texture_);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIResource> dxgi_resource;
    if (FAILED(shared_texture_.As(&dxgi_resource))) return false;

    hr = dxgi_resource->GetSharedHandle(&shared_handle_);
    shared_name_.clear();
    keyed_mutex_.Reset();

    return SUCCEEDED(hr);
}

bool DxgiDesktopCapture::acquire_frame(uint64_t timeout_ms, ipc::RectPod& out_dirty_rect) {
    if (!duplication_) return false;

    DXGI_OUTDUPL_FRAME_INFO frame_info{};
    ComPtr<IDXGIResource> desktop_resource;

    HRESULT hr = duplication_->AcquireNextFrame(
        static_cast<UINT>(timeout_ms),
        &frame_info,
        &desktop_resource
    );

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false;
    }

    if (hr == DXGI_ERROR_ACCESS_LOST) {
        std::cerr << "[DxgiCapture] DXGI_ERROR_ACCESS_LOST encountered, reinitializing duplication...\n";
        duplication_.Reset();
        initialize_duplication(target_device_name_);
        return false;
    }

    if (FAILED(hr)) {
        return false;
    }

    frame_acquired_ = true;
    frame_index_++;

    ComPtr<ID3D11Texture2D> desktop_image;
    if (SUCCEEDED(desktop_resource.As(&desktop_image))) {
        if (keyed_mutex_) {
            // Acquire Key 0 (Daemon writer key), 16ms timeout
            HRESULT km_hr = keyed_mutex_->AcquireSync(0, 16);
            if (SUCCEEDED(km_hr)) {
                d3d_context_->CopyResource(shared_texture_.Get(), desktop_image.Get());
                // Release to Key 1 (Signals Compositor that frame is ready)
                keyed_mutex_->ReleaseSync(1);
            } else {
                d3d_context_->CopyResource(shared_texture_.Get(), desktop_image.Get());
            }
        } else {
            d3d_context_->CopyResource(shared_texture_.Get(), desktop_image.Get());
            d3d_context_->Flush();
        }
    }

    out_dirty_rect = {0, 0, static_cast<int32_t>(width_), static_cast<int32_t>(height_)};
    return true;
}

void DxgiDesktopCapture::release_frame() {
    if (frame_acquired_ && duplication_) {
        duplication_->ReleaseFrame();
        frame_acquired_ = false;
    }
}

ipc::SharedTexturePayload DxgiDesktopCapture::get_shared_texture_payload() const {
    ipc::SharedTexturePayload payload{};
    payload.shared_handle = reinterpret_cast<uint64_t>(shared_handle_);
    payload.width = width_;
    payload.height = height_;
    payload.dxgi_format = DXGI_FORMAT_B8G8R8A8_UNORM;
    payload.creation_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    payload.has_keyed_mutex = (keyed_mutex_ != nullptr) ? 1 : 0;

    if (!shared_name_.empty()) {
        wcsncpy_s(payload.shared_name, shared_name_.c_str(), _TRUNCATE);
    }
    return payload;
}

} // namespace deskwrap::daemon
