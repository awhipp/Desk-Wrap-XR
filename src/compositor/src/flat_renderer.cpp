#include "flat_renderer.hpp"
#include <iostream>
#include <d3dcompiler.h>

namespace deskwrap::compositor {

namespace {

const char* SHADER_SOURCE = R"(
struct VSOutput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint id : SV_VertexID) {
    VSOutput output;
    // Full-screen triangle covering [-1, 1] NDC
    float2 uv = float2((id << 1) & 2, id & 2);
    output.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = uv;
    return output;
}

Texture2D desktopTexture : register(t0);
SamplerState linearSampler : register(s0);

float4 PSMain(VSOutput input) : SV_TARGET {
    return desktopTexture.Sample(linearSampler, input.uv);
}
)";

} // namespace

FlatRenderer::~FlatRenderer() {
    shutdown();
}

bool FlatRenderer::initialize(HWND hwnd, uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;

    if (!create_device_and_swapchain(hwnd, width, height)) {
        std::cerr << "[Renderer] Failed to create D3D11 device and swap chain.\n";
        return false;
    }

    if (!create_render_target()) {
        std::cerr << "[Renderer] Failed to create render target view.\n";
        return false;
    }

    if (!init_shaders()) {
        std::cerr << "[Renderer] Failed to compile and initialize shaders.\n";
        return false;
    }

    if (!create_sampler()) {
        std::cerr << "[Renderer] Failed to create sampler state.\n";
        return false;
    }

    std::cout << "[Renderer] Direct3D 11 flat renderer initialized (" << width_ << "x" << height_ << ").\n";
    return true;
}

void FlatRenderer::shutdown() {
    release_shared_texture();
    sampler_state_.Reset();
    pixel_shader_.Reset();
    vertex_shader_.Reset();
    render_target_view_.Reset();
    swap_chain_.Reset();
    context_.Reset();
    device1_.Reset();
    device_.Reset();
}

bool FlatRenderer::create_device_and_swapchain(HWND hwnd, uint32_t width, uint32_t height) {
    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    D3D_FEATURE_LEVEL feature_level;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        feature_levels,
        ARRAYSIZE(feature_levels),
        D3D11_SDK_VERSION,
        &scd,
        &swap_chain_,
        &device_,
        &feature_level,
        &context_
    );

    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            feature_levels,
            ARRAYSIZE(feature_levels),
            D3D11_SDK_VERSION,
            &scd,
            &swap_chain_,
            &device_,
            &feature_level,
            &context_
        );
    }

    if (FAILED(hr)) return false;

    device_.As(&device1_);
    return true;
}

bool FlatRenderer::create_render_target() {
    ComPtr<ID3D11Texture2D> back_buffer;
    HRESULT hr = swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), &back_buffer);
    if (FAILED(hr)) return false;

    hr = device_->CreateRenderTargetView(back_buffer.Get(), nullptr, &render_target_view_);
    return SUCCEEDED(hr);
}

bool FlatRenderer::init_shaders() {
    ComPtr<ID3DBlob> vs_blob;
    ComPtr<ID3DBlob> error_blob;

    HRESULT hr = D3DCompile(
        SHADER_SOURCE,
        strlen(SHADER_SOURCE),
        nullptr,
        nullptr,
        nullptr,
        "VSMain",
        "vs_5_0",
        0,
        0,
        &vs_blob,
        &error_blob
    );

    if (FAILED(hr)) {
        if (error_blob) {
            std::cerr << "[Renderer] VS compile error: "
                      << static_cast<char*>(error_blob->GetBufferPointer()) << "\n";
        }
        return false;
    }

    hr = device_->CreateVertexShader(
        vs_blob->GetBufferPointer(),
        vs_blob->GetBufferSize(),
        nullptr,
        &vertex_shader_
    );
    if (FAILED(hr)) return false;

    ComPtr<ID3DBlob> ps_blob;
    error_blob.Reset();

    hr = D3DCompile(
        SHADER_SOURCE,
        strlen(SHADER_SOURCE),
        nullptr,
        nullptr,
        nullptr,
        "PSMain",
        "ps_5_0",
        0,
        0,
        &ps_blob,
        &error_blob
    );

    if (FAILED(hr)) {
        if (error_blob) {
            std::cerr << "[Renderer] PS compile error: "
                      << static_cast<char*>(error_blob->GetBufferPointer()) << "\n";
        }
        return false;
    }

    hr = device_->CreatePixelShader(
        ps_blob->GetBufferPointer(),
        ps_blob->GetBufferSize(),
        nullptr,
        &pixel_shader_
    );
    return SUCCEEDED(hr);
}

bool FlatRenderer::create_sampler() {
    D3D11_SAMPLER_DESC desc{};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MinLOD = 0;
    desc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = device_->CreateSamplerState(&desc, &sampler_state_);
    return SUCCEEDED(hr);
}

void FlatRenderer::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || !swap_chain_) return;

    width_ = width;
    height_ = height;

    context_->OMSetRenderTargets(0, nullptr, nullptr);
    render_target_view_.Reset();

    HRESULT hr = swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (SUCCEEDED(hr)) {
        create_render_target();
    }
}

bool FlatRenderer::import_shared_texture(const ipc::SharedTexturePayload& payload) {
    if (!device_) return false;

    release_shared_texture();

    HRESULT hr = E_FAIL;
    if (device1_ && payload.shared_name[0] != 0) {
        hr = device1_->OpenSharedResourceByName(
            payload.shared_name,
            DXGI_SHARED_RESOURCE_READ,
            __uuidof(ID3D11Texture2D),
            &imported_texture_
        );
    }

    if (FAILED(hr) && device1_ && payload.shared_handle != 0) {
        hr = device1_->OpenSharedResource1(
            reinterpret_cast<HANDLE>(payload.shared_handle),
            __uuidof(ID3D11Texture2D),
            &imported_texture_
        );
    }

    if (FAILED(hr) && payload.shared_handle != 0) {
        hr = device_->OpenSharedResource(
            reinterpret_cast<HANDLE>(payload.shared_handle),
            __uuidof(ID3D11Texture2D),
            &imported_texture_
        );
    }

    if (FAILED(hr) || !imported_texture_) {
        std::cerr << "[Renderer] Failed to open shared texture resource: 0x" << std::hex << hr << std::dec << "\n";
        return false;
    }

    if (payload.has_keyed_mutex) {
        imported_texture_.As(&keyed_mutex_);
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = static_cast<DXGI_FORMAT>(payload.dxgi_format);
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;

    hr = device_->CreateShaderResourceView(imported_texture_.Get(), &srv_desc, &texture_srv_);
    if (FAILED(hr)) {
        std::cerr << "[Renderer] Failed to create shader resource view for shared texture: 0x"
                  << std::hex << hr << std::dec << "\n";
        return false;
    }

    std::cout << "[Renderer] Shared texture successfully imported into compositor ("
              << payload.width << "x" << payload.height
              << ", KeyedMutex: " << (keyed_mutex_ ? "YES" : "NO") << ").\n";
    return true;
}

void FlatRenderer::release_shared_texture() {
    texture_srv_.Reset();
    keyed_mutex_.Reset();
    imported_texture_.Reset();
    has_frame_ = false;
}

void FlatRenderer::render_frame() {
    if (!context_ || !render_target_view_) return;

    // Set viewport and render target
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<FLOAT>(width_);
    vp.Height = static_cast<FLOAT>(height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);

    ID3D11RenderTargetView* rtvs[] = {render_target_view_.Get()};
    context_->OMSetRenderTargets(1, rtvs, nullptr);

    // Standby clear color (dark slate)
    float clear_color[4] = {0.05f, 0.05f, 0.08f, 1.0f};
    context_->ClearRenderTargetView(render_target_view_.Get(), clear_color);

    if (imported_texture_ && texture_srv_) {
        bool acquired = false;
        if (keyed_mutex_) {
            // Acquire Key 1 (Compositor read key) with 16ms timeout
            HRESULT hr = keyed_mutex_->AcquireSync(1, 16);
            if (SUCCEEDED(hr)) {
                acquired = true;
                has_frame_ = true;
            }
        } else {
            has_frame_ = true;
        }

        if (has_frame_) {
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->IASetInputLayout(nullptr);

            context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
            context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);

            ID3D11SamplerState* samplers[] = {sampler_state_.Get()};
            context_->PSSetSamplers(0, 1, samplers);

            ID3D11ShaderResourceView* srvs[] = {texture_srv_.Get()};
            context_->PSSetShaderResources(0, 1, srvs);

            context_->Draw(3, 0);

            // Unbind SRV to prevent pipeline hazard
            ID3D11ShaderResourceView* null_srv[] = {nullptr};
            context_->PSSetShaderResources(0, 1, null_srv);
        }

        if (acquired && keyed_mutex_) {
            keyed_mutex_->ReleaseSync(0); // Release back to Key 0 for capture daemon
        }
    }

    swap_chain_->Present(1, 0);
}

} // namespace deskwrap::compositor
