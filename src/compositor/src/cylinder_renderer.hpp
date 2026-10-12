#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include "deskwrap/common/ipc_protocol.hpp"
#include "deskwrap/common/config.hpp"
#include "deskwrap/common/cylinder_mesh.hpp"

namespace deskwrap::compositor {

using Microsoft::WRL::ComPtr;

class CylinderRenderer {
public:
    CylinderRenderer() = default;
    ~CylinderRenderer();

    bool initialize(HWND hwnd, uint32_t width, uint32_t height, const DeskWrapConfig& config);
    void shutdown();

    void resize(uint32_t width, uint32_t height);
    bool import_shared_texture(const ipc::SharedTexturePayload& payload);
    void release_shared_texture();

    void update_geometry(float arc_degrees, float radius, float height);
    void set_camera_rotation(float yaw_deg, float pitch_deg, float roll_deg);
    void adjust_camera_yaw(float delta_deg);
    void adjust_camera_pitch(float delta_deg);
    void reset_camera();

    void render_frame();

    bool has_shared_texture() const { return imported_texture_ != nullptr; }
    bool has_keyed_mutex() const { return keyed_mutex_ != nullptr; }
    float get_arc_degrees() const { return arc_degrees_; }

private:
    bool create_device_and_swapchain(HWND hwnd, uint32_t width, uint32_t height);
    bool create_render_target();
    bool init_shaders();
    bool create_rasterizer_and_sampler();
    bool rebuild_mesh();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11Device1> device1_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swap_chain_;
    ComPtr<ID3D11RenderTargetView> render_target_view_;

    ComPtr<ID3D11VertexShader> vertex_shader_;
    ComPtr<ID3D11PixelShader> pixel_shader_;
    ComPtr<ID3D11InputLayout> input_layout_;
    ComPtr<ID3D11Buffer> camera_constant_buffer_;

    ComPtr<ID3D11Buffer> vertex_buffer_;
    ComPtr<ID3D11Buffer> index_buffer_;
    uint32_t index_count_ = 0;

    ComPtr<ID3D11RasterizerState> rasterizer_state_;
    ComPtr<ID3D11SamplerState> sampler_state_wrap_;
    ComPtr<ID3D11SamplerState> sampler_state_clamp_;

    ComPtr<ID3D11Texture2D> imported_texture_;
    ComPtr<IDXGIKeyedMutex> keyed_mutex_;
    ComPtr<ID3D11ShaderResourceView> texture_srv_;

    uint32_t width_ = 1920;
    uint32_t height_ = 1200;
    bool has_frame_ = false;

    float arc_degrees_ = 180.0f;
    float radius_ = 1.8f;
    float cylinder_height_ = 1.0f;

    float yaw_deg_ = 0.0f;
    float pitch_deg_ = 0.0f;
    float roll_deg_ = 0.0f;
    float fov_y_deg_ = 30.0f; // Native vertical FOV for VITURE glasses
};

} // namespace deskwrap::compositor
