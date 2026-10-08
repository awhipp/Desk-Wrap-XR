cmake_minimum_required(VERSION 3.24)

add_executable(deskwrap-compositor
    src/main.cpp
    src/viture_client.cpp
    src/cylinder_mesh.cpp
    src/d3d11_renderer.cpp
    src/viture_client.hpp
    src/cylinder_mesh.hpp
    src/d3d11_renderer.hpp
    src/shaders.hlsl
)

target_include_directories(deskwrap-compositor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(deskwrap-compositor
    PRIVATE
        deskwrap-common
        viture::glasses
        d3d11
        dxgi
        d3dcompiler
        user32
        gdi32
        hid
)

target_compile_features(deskwrap-compositor PRIVATE cxx_std_20)

# Stage VITURE runtime DLLs (glasses.dll, libusb-1.0.dll, etc.) to the binary directory
deskwrap_stage_runtime_dlls(deskwrap-compositor)
