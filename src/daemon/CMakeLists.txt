cmake_minimum_required(VERSION 3.24)

add_executable(deskwrap-daemon
    src/main.cpp
    src/virtual_display.cpp
    src/dxgi_capture.cpp
    src/mouse_hook.cpp
    src/device_monitor.cpp
    src/virtual_display.hpp
    src/dxgi_capture.hpp
    src/mouse_hook.hpp
    src/device_monitor.hpp
)

target_include_directories(deskwrap-daemon
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(deskwrap-daemon
    PRIVATE
        deskwrap-common
        d3d11
        dxgi
        setupapi
        user32
        gdi32
)

target_compile_features(deskwrap-daemon PRIVATE cxx_std_20)
