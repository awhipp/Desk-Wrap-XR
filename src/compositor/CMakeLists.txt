cmake_minimum_required(VERSION 3.24)

add_executable(deskwrap-compositor
    src/monitor_finder.hpp
    src/monitor_finder.cpp
    src/flat_renderer.hpp
    src/flat_renderer.cpp
    src/cylinder_renderer.hpp
    src/cylinder_renderer.cpp
    src/main.cpp
)

target_include_directories(deskwrap-compositor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(deskwrap-compositor
    PRIVATE
        deskwrap-common
        viture::glasses
)

if(WIN32)
    target_link_libraries(deskwrap-compositor
        PRIVATE
            user32
            shell32
            ole32
            d3d11
            dxgi
            d3dcompiler
    )
endif()

target_compile_features(deskwrap-compositor PRIVATE cxx_std_20)

# Stage VITURE runtime DLLs next to the compositor executable
deskwrap_stage_runtime_dlls(deskwrap-compositor)
