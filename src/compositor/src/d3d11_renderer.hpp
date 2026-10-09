#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include "cylinder_mesh.hpp"
#include "deskwrap/common/math.hpp"

namespace deskwrap::compositor {

using Microsoft::WRL::ComPtr;

class D3D11Renderer {
public:
    D3D11Renderer() = default;
    ~D3D11Renderer();

    bool initialize(HWND hwnd, uint32_t width, uint32_t height);
    void shutdown();

    bool import_shared_texture(HANDLE shared_handle, const std::wstring& shared_name = L"", bool use_keyed_mutex = true);
    void set_mesh(const MeshData& mesh);

    void render_frame(const math::Quaternion& camera_rot, float fov_y_rad);

    void set_wireframe(bool enabled);
    bool is_wireframe() const { return wireframe_mode_; }

    bool has_shared_texture() const { return imported_texture_ != nullptr; }
    bool has_keyed_mutex() const { return keyed_mutex_ != nullptr; }

private:
    bool create_device_and_swapchain(HWND hwnd, uint32_t width, uint32_t height);
    bool create_render_target();
    bool init_shaders_and_pipeline();
    bool create_sampler();
    bool create_rasterizer_states();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11Device1> device1_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swap_chain_;
    ComPtr<ID3D11RenderTargetView> render_target_view_;

    ComPtr<ID3D11VertexShader> vertex_shader_;
    ComPtr<ID3D11PixelShader> pixel_shader_;
    ComPtr<ID3D11InputLayout> input_layout_;
    ComPtr<ID3D11Buffer> constant_buffer_;
    ComPtr<ID3D11SamplerState> sampler_state_;
    ComPtr<ID3D11RasterizerState> rasterizer_state_;
    ComPtr<ID3D11RasterizerState> wireframe_rasterizer_state_;
    bool wireframe_mode_ = false;

    ComPtr<ID3D11Buffer> vertex_buffer_;
    ComPtr<ID3D11Buffer> index_buffer_;
    uint32_t index_count_ = 0;

    ComPtr<ID3D11Texture2D> imported_texture_;
    ComPtr<IDXGIKeyedMutex> keyed_mutex_;
    ComPtr<ID3D11ShaderResourceView> texture_srv_;

    uint32_t viewport_width_ = 1920;
    uint32_t viewport_height_ = 1200;
};

} // namespace deskwrap::compositor
