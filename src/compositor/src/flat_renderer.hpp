#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include "deskwrap/common/ipc_protocol.hpp"

namespace deskwrap::compositor {

using Microsoft::WRL::ComPtr;

class FlatRenderer {
public:
    FlatRenderer() = default;
    ~FlatRenderer();

    bool initialize(HWND hwnd, uint32_t width, uint32_t height);
    void shutdown();

    void resize(uint32_t width, uint32_t height);
    bool import_shared_texture(const ipc::SharedTexturePayload& payload);
    void release_shared_texture();

    void render_frame();

    bool has_shared_texture() const { return imported_texture_ != nullptr; }
    bool has_keyed_mutex() const { return keyed_mutex_ != nullptr; }

private:
    bool create_device_and_swapchain(HWND hwnd, uint32_t width, uint32_t height);
    bool create_render_target();
    bool init_shaders();
    bool create_sampler();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11Device1> device1_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swap_chain_;
    ComPtr<ID3D11RenderTargetView> render_target_view_;

    ComPtr<ID3D11VertexShader> vertex_shader_;
    ComPtr<ID3D11PixelShader> pixel_shader_;
    ComPtr<ID3D11SamplerState> sampler_state_;

    ComPtr<ID3D11Texture2D> imported_texture_;
    ComPtr<IDXGIKeyedMutex> keyed_mutex_;
    ComPtr<ID3D11ShaderResourceView> texture_srv_;

    uint32_t width_ = 1280;
    uint32_t height_ = 720;
    bool has_frame_ = false;
};

} // namespace deskwrap::compositor
