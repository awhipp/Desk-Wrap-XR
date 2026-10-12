#include "cylinder_renderer.hpp"
#include <iostream>
#include <d3dcompiler.h>
#include <cmath>
#include <algorithm>

namespace deskwrap::compositor {

namespace {

const char* SHADER_SOURCE = R"(
cbuffer CameraBuffer : register(b0) {
    row_major float4x4 view_projection;
};

struct VSInput {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOutput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.pos = mul(float4(input.pos, 1.0f), view_projection);
    output.uv = input.uv;
    return output;
}

Texture2D desktopTexture : register(t0);
SamplerState cylinderSampler : register(s0);

float4 PSMain(VSOutput input) : SV_TARGET {
    return desktopTexture.Sample(cylinderSampler, input.uv);
}
)";

} // namespace

CylinderRenderer::~CylinderRenderer() {
    shutdown();
}

bool CylinderRenderer::initialize(HWND hwnd, uint32_t width, uint32_t height, const DeskWrapConfig& config) {
    width_ = width;
    height_ = height;
    arc_degrees_ = config.arc_degrees;
    radius_ = config.radius;
    cylinder_height_ = config.height;

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

    if (!create_rasterizer_and_sampler()) {
        std::cerr << "[Renderer] Failed to create rasterizer and sampler states.\n";
        return false;
    }

    if (!rebuild_mesh()) {
        std::cerr << "[Renderer] Failed to construct cylinder mesh.\n";
        return false;
    }

    std::cout << "[Renderer] Direct3D 11 cylinder renderer initialized (" << width_ << "x" << height_
              << ", arc: " << arc_degrees_ << "°).\n";
    return true;
}

void CylinderRenderer::shutdown() {
    release_shared_texture();
    sampler_state_clamp_.Reset();
    sampler_state_wrap_.Reset();
    rasterizer_state_.Reset();
    index_buffer_.Reset();
    vertex_buffer_.Reset();
    camera_constant_buffer_.Reset();
    input_layout_.Reset();
    pixel_shader_.Reset();
    vertex_shader_.Reset();
    render_target_view_.Reset();
    swap_chain_.Reset();
    context_.Reset();
    device1_.Reset();
    device_.Reset();
}

bool CylinderRenderer::create_device_and_swapchain(HWND hwnd, uint32_t width, uint32_t height) {
    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 120;
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

bool CylinderRenderer::create_render_target() {
    ComPtr<ID3D11Texture2D> back_buffer;
    HRESULT hr = swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), &back_buffer);
    if (FAILED(hr)) return false;

    hr = device_->CreateRenderTargetView(back_buffer.Get(), nullptr, &render_target_view_);
    return SUCCEEDED(hr);
}

bool CylinderRenderer::init_shaders() {
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

    D3D11_INPUT_ELEMENT_DESC layout_desc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = device_->CreateInputLayout(
        layout_desc,
        ARRAYSIZE(layout_desc),
        vs_blob->GetBufferPointer(),
        vs_blob->GetBufferSize(),
        &input_layout_
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
    if (FAILED(hr)) return false;

    // Constant buffer for camera view-projection matrix
    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(Matrix4x4);
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = 0;

    hr = device_->CreateBuffer(&cbd, nullptr, &camera_constant_buffer_);
    return SUCCEEDED(hr);
}

bool CylinderRenderer::create_rasterizer_and_sampler() {
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;

    HRESULT hr = device_->CreateRasterizerState(&rd, &rasterizer_state_);
    if (FAILED(hr)) return false;

    // Wrap sampler for 360 wrap
    D3D11_SAMPLER_DESC wrap_desc{};
    wrap_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    wrap_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    wrap_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    wrap_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    wrap_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    wrap_desc.MinLOD = 0;
    wrap_desc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = device_->CreateSamplerState(&wrap_desc, &sampler_state_wrap_);
    if (FAILED(hr)) return false;

    // Clamp sampler for arcs < 360
    D3D11_SAMPLER_DESC clamp_desc = wrap_desc;
    clamp_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;

    hr = device_->CreateSamplerState(&clamp_desc, &sampler_state_clamp_);
    return SUCCEEDED(hr);
}

bool CylinderRenderer::rebuild_mesh() {
    if (!device_) return false;

    CylinderMesh mesh = generate_cylinder_mesh(arc_degrees_, radius_, cylinder_height_, 128, 16);
    index_count_ = static_cast<uint32_t>(mesh.indices.size());

    vertex_buffer_.Reset();
    index_buffer_.Reset();

    D3D11_BUFFER_DESC vbd{};
    vbd.ByteWidth = static_cast<UINT>(mesh.vertices.size() * sizeof(CylinderVertex));
    vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vdata{};
    vdata.pSysMem = mesh.vertices.data();

    HRESULT hr = device_->CreateBuffer(&vbd, &vdata, &vertex_buffer_);
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC ibd{};
    ibd.ByteWidth = static_cast<UINT>(mesh.indices.size() * sizeof(uint32_t));
    ibd.Usage = D3D11_USAGE_DEFAULT;
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA idata{};
    idata.pSysMem = mesh.indices.data();

    hr = device_->CreateBuffer(&ibd, &idata, &index_buffer_);
    return SUCCEEDED(hr);
}

void CylinderRenderer::resize(uint32_t width, uint32_t height) {
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

void CylinderRenderer::update_geometry(float arc_degrees, float radius, float height) {
    arc_degrees_ = std::clamp(arc_degrees, 180.0f, 360.0f);
    radius_ = radius;
    cylinder_height_ = height;
    rebuild_mesh();
    std::cout << "[Renderer] Updated cylinder geometry (arc: " << arc_degrees_
              << "°, radius: " << radius_ << "m, height: " << cylinder_height_ << "m).\n";
}

void CylinderRenderer::set_camera_rotation(float yaw_deg, float pitch_deg, float roll_deg) {
    yaw_deg_ = yaw_deg;
    pitch_deg_ = pitch_deg;
    roll_deg_ = roll_deg;
}

void CylinderRenderer::adjust_camera_yaw(float delta_deg) {
    yaw_deg_ += delta_deg;
    while (yaw_deg_ > 180.0f) yaw_deg_ -= 360.0f;
    while (yaw_deg_ < -180.0f) yaw_deg_ += 360.0f;
}

void CylinderRenderer::adjust_camera_pitch(float delta_deg) {
    pitch_deg_ = std::clamp(pitch_deg_ + delta_deg, -85.0f, 85.0f);
}

void CylinderRenderer::reset_camera() {
    yaw_deg_ = 0.0f;
    pitch_deg_ = 0.0f;
    roll_deg_ = 0.0f;
}

bool CylinderRenderer::import_shared_texture(const ipc::SharedTexturePayload& payload) {
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

    std::cout << "[Renderer] Shared desktop texture successfully imported into cylinder renderer ("
              << payload.width << "x" << payload.height
              << ", KeyedMutex: " << (keyed_mutex_ ? "YES" : "NO") << ").\n";
    return true;
}

void CylinderRenderer::release_shared_texture() {
    texture_srv_.Reset();
    keyed_mutex_.Reset();
    imported_texture_.Reset();
    has_frame_ = false;
}

void CylinderRenderer::render_frame() {
    if (!context_ || !render_target_view_) return;

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<FLOAT>(width_);
    vp.Height = static_cast<FLOAT>(height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);

    ID3D11RenderTargetView* rtvs[] = {render_target_view_.Get()};
    context_->OMSetRenderTargets(1, rtvs, nullptr);

    float clear_color[4] = {0.05f, 0.05f, 0.08f, 1.0f};
    context_->ClearRenderTargetView(render_target_view_.Get(), clear_color);

    // Compute View-Projection Matrix
    float aspect = (height_ > 0) ? (static_cast<float>(width_) / static_cast<float>(height_)) : (1920.0f / 1200.0f);
    float fov_y_rad = fov_y_deg_ * (3.14159265358979323846f / 180.0f);
    float yaw_rad = yaw_deg_ * (3.14159265358979323846f / 180.0f);
    float pitch_rad = pitch_deg_ * (3.14159265358979323846f / 180.0f);
    float roll_rad = roll_deg_ * (3.14159265358979323846f / 180.0f);

    Matrix4x4 rot = Matrix4x4::rotation_yaw_pitch_roll(yaw_rad, pitch_rad, roll_rad);
    Matrix4x4 proj = Matrix4x4::perspective_lh(fov_y_rad, aspect, 0.1f, 100.0f);
    Matrix4x4 view_proj = Matrix4x4::multiply(rot, proj);

    context_->UpdateSubresource(camera_constant_buffer_.Get(), 0, nullptr, &view_proj, 0, 0);

    if (imported_texture_ && texture_srv_) {
        bool acquired = false;
        if (keyed_mutex_) {
            HRESULT hr = keyed_mutex_->AcquireSync(1, 16);
            if (SUCCEEDED(hr)) {
                acquired = true;
                has_frame_ = true;
            }
        } else {
            has_frame_ = true;
        }

        if (has_frame_ && vertex_buffer_ && index_buffer_) {
            UINT stride = sizeof(CylinderVertex);
            UINT offset = 0;
            ID3D11Buffer* vbs[] = {vertex_buffer_.Get()};
            context_->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
            context_->IASetIndexBuffer(index_buffer_.Get(), DXGI_FORMAT_R32_UINT, 0);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->IASetInputLayout(input_layout_.Get());

            context_->RSSetState(rasterizer_state_.Get());

            context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
            ID3D11Buffer* cbs[] = {camera_constant_buffer_.Get()};
            context_->VSSetConstantBuffers(0, 1, cbs);

            context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);

            ID3D11SamplerState* samplers[] = {
                (arc_degrees_ >= 359.9f) ? sampler_state_wrap_.Get() : sampler_state_clamp_.Get()
            };
            context_->PSSetSamplers(0, 1, samplers);

            ID3D11ShaderResourceView* srvs[] = {texture_srv_.Get()};
            context_->PSSetShaderResources(0, 1, srvs);

            context_->DrawIndexed(index_count_, 0, 0);

            ID3D11ShaderResourceView* null_srv[] = {nullptr};
            context_->PSSetShaderResources(0, 1, null_srv);
        }

        if (acquired && keyed_mutex_) {
            keyed_mutex_->ReleaseSync(0);
        }
    }

    swap_chain_->Present(1, 0);
}

} // namespace deskwrap::compositor
