#include "d3d11_renderer.hpp"
#include <iostream>

namespace deskwrap::compositor {

namespace {
const char* SHADER_SOURCE = R"(
cbuffer MatrixBuffer : register(b0)
{
    matrix world_view_projection;
};

struct VS_INPUT
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;
    output.position = mul(float4(input.position, 1.0f), world_view_projection);
    output.uv = input.uv;
    return output;
}

Texture2D desktop_texture : register(t0);
SamplerState wrap_sampler : register(s0);

float4 PSMain(PS_INPUT input) : SV_TARGET
{
    return desktop_texture.Sample(wrap_sampler, input.uv);
}
)";
} // namespace

D3D11Renderer::~D3D11Renderer() {
    shutdown();
}

bool D3D11Renderer::initialize(HWND hwnd, uint32_t width, uint32_t height) {
    viewport_width_ = width;
    viewport_height_ = height;

    if (!create_device_and_swapchain(hwnd, width, height)) return false;
    if (!create_render_target()) return false;
    if (!init_shaders_and_pipeline()) return false;
    if (!create_sampler()) return false;

    return true;
}

void D3D11Renderer::shutdown() {
    keyed_mutex_.Reset();
    texture_srv_.Reset();
    imported_texture_.Reset();
    index_buffer_.Reset();
    vertex_buffer_.Reset();
    sampler_state_.Reset();
    constant_buffer_.Reset();
    input_layout_.Reset();
    pixel_shader_.Reset();
    vertex_shader_.Reset();
    render_target_view_.Reset();
    swap_chain_.Reset();
    context_.Reset();
    device1_.Reset();
    device_.Reset();
}

bool D3D11Renderer::create_device_and_swapchain(HWND hwnd, uint32_t width, uint32_t height) {
    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
        0,
        feature_levels,
        ARRAYSIZE(feature_levels),
        D3D11_SDK_VERSION,
        &scd,
        &swap_chain_,
        &device_,
        &feature_level,
        &context_
    );

    if (FAILED(hr)) return false;

    device_.As(&device1_);
    return true;
}

bool D3D11Renderer::create_render_target() {
    ComPtr<ID3D11Texture2D> back_buffer;
    HRESULT hr = swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), &back_buffer);
    if (FAILED(hr)) return false;

    hr = device_->CreateRenderTargetView(back_buffer.Get(), nullptr, &render_target_view_);
    return SUCCEEDED(hr);
}

bool D3D11Renderer::init_shaders_and_pipeline() {
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
                      << static_cast<char*>(error_blob->GetBufferPointer()) << std::endl;
        }
        return false;
    }

    hr = device_->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vertex_shader_);
    if (FAILED(hr)) return false;

    D3D11_INPUT_ELEMENT_DESC layout_desc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0}
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
                      << static_cast<char*>(error_blob->GetBufferPointer()) << std::endl;
        }
        return false;
    }

    hr = device_->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &pixel_shader_);
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(math::Matrix4x4);
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = device_->CreateBuffer(&cbd, nullptr, &constant_buffer_);
    return SUCCEEDED(hr);
}

bool D3D11Renderer::create_sampler() {
    D3D11_SAMPLER_DESC samp_desc{};
    samp_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;   // Toroidal wrapping along horizontal cylinder arc
    samp_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;  // Vertical bounds clamp
    samp_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samp_desc.MinLOD = 0;
    samp_desc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = device_->CreateSamplerState(&samp_desc, &sampler_state_);
    return SUCCEEDED(hr);
}

bool D3D11Renderer::import_shared_texture(HANDLE shared_handle, const std::wstring& shared_name, bool use_keyed_mutex) {
    if (!device_) return false;

    texture_srv_.Reset();
    imported_texture_.Reset();
    keyed_mutex_.Reset();

    HRESULT hr = E_FAIL;
    if (device1_) {
        if (!shared_name.empty()) {
            hr = device1_->OpenSharedResourceByName(
                shared_name.c_str(),
                DXGI_SHARED_RESOURCE_READ,
                __uuidof(ID3D11Texture2D),
                &imported_texture_
            );
        } else if (shared_handle) {
            hr = device1_->OpenSharedResource1(
                shared_handle,
                __uuidof(ID3D11Texture2D),
                &imported_texture_
            );
        }
    }

    if (FAILED(hr) && shared_handle) {
        // Fallback to legacy OpenSharedResource
        hr = device_->OpenSharedResource(shared_handle, __uuidof(ID3D11Texture2D), &imported_texture_);
    }

    if (FAILED(hr) || !imported_texture_) {
        std::cerr << "[Renderer] Failed to open shared resource, hr: 0x" << std::hex << hr << std::endl;
        return false;
    }

    if (use_keyed_mutex) {
        imported_texture_.As(&keyed_mutex_);
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;

    hr = device_->CreateShaderResourceView(imported_texture_.Get(), &srv_desc, &texture_srv_);
    return SUCCEEDED(hr);
}

void D3D11Renderer::set_mesh(const MeshData& mesh) {
    if (!device_ || mesh.vertices.empty()) return;

    index_count_ = static_cast<uint32_t>(mesh.indices.size());

    D3D11_BUFFER_DESC vbd{};
    vbd.ByteWidth = static_cast<UINT>(mesh.vertices.size() * sizeof(Vertex));
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vinit{};
    vinit.pSysMem = mesh.vertices.data();
    device_->CreateBuffer(&vbd, &vinit, &vertex_buffer_);

    D3D11_BUFFER_DESC ibd{};
    ibd.ByteWidth = static_cast<UINT>(mesh.indices.size() * sizeof(uint32_t));
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA iinit{};
    iinit.pSysMem = mesh.indices.data();
    device_->CreateBuffer(&ibd, &iinit, &index_buffer_);
}

void D3D11Renderer::render_frame(const math::Quaternion& camera_rot, float fov_y_rad) {
    if (!context_ || !render_target_view_ || index_count_ == 0) return;

    bool acquired_key = false;
    if (keyed_mutex_) {
        // Attempt to acquire Key 1 (Compositor reading key) with 1ms timeout
        HRESULT km_hr = keyed_mutex_->AcquireSync(1, 1);
        if (SUCCEEDED(km_hr)) {
            acquired_key = true;
        }
    }

    float clear_color[4] = {0.05f, 0.05f, 0.08f, 1.0f};
    context_->ClearRenderTargetView(render_target_view_.Get(), clear_color);

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<FLOAT>(viewport_width_);
    vp.Height = static_cast<FLOAT>(viewport_height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);

    ID3D11RenderTargetView* rtvs[] = {render_target_view_.Get()};
    context_->OMSetRenderTargets(1, rtvs, nullptr);

    // Compute View and Projection matrices
    float aspect = static_cast<float>(viewport_width_) / static_cast<float>(viewport_height_);
    math::Matrix4x4 proj = math::Matrix4x4::perspective_fov_lh(fov_y_rad, aspect, 0.1f, 100.0f);

    // View matrix from camera orientation quaternion
    math::Matrix4x4 view_rot = math::Matrix4x4::from_quaternion(camera_rot.conjugate());
    math::Matrix4x4 wvp = view_rot * proj;

    context_->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &wvp, 0, 0);

    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    ID3D11Buffer* vbs[] = {vertex_buffer_.Get()};
    context_->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    context_->IASetIndexBuffer(index_buffer_.Get(), DXGI_FORMAT_R32_UINT, 0);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetInputLayout(input_layout_.Get());

    context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    ID3D11Buffer* cbs[] = {constant_buffer_.Get()};
    context_->VSSetConstantBuffers(0, 1, cbs);

    context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    ID3D11SamplerState* samplers[] = {sampler_state_.Get()};
    context_->PSSetSamplers(0, 1, samplers);

    if (texture_srv_) {
        ID3D11ShaderResourceView* srvs[] = {texture_srv_.Get()};
        context_->PSSetShaderResources(0, 1, srvs);
    }

    context_->DrawIndexed(index_count_, 0, 0);

    if (acquired_key && keyed_mutex_) {
        keyed_mutex_->ReleaseSync(0); // Release back to Key 0 for capture daemon
    }

    swap_chain_->Present(1, 0);
}

} // namespace deskwrap::compositor
