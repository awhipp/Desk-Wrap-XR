cmake_minimum_required(VERSION 3.24)

add_executable(prototype-dxgi-ipc
    prototype_dxgi_ipc.cpp
)

target_include_directories(prototype-dxgi-ipc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)

target_link_libraries(prototype-dxgi-ipc
    PRIVATE
        deskwrap-common
        d3d11
        dxgi
)

target_compile_features(prototype-dxgi-ipc PRIVATE cxx_std_20)
