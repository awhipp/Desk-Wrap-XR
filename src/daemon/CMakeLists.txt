cmake_minimum_required(VERSION 3.24)

add_executable(deskwrap-daemon
    src/dxgi_capture.hpp
    src/dxgi_capture.cpp
    src/main.cpp
)

target_include_directories(deskwrap-daemon
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(deskwrap-daemon
    PRIVATE
        deskwrap-common
)

if(WIN32)
    target_link_libraries(deskwrap-daemon
        PRIVATE
            user32
            shell32
            ole32
            d3d11
            dxgi
    )
endif()

target_compile_features(deskwrap-daemon PRIVATE cxx_std_20)

# Stage VITURE runtime DLLs next to the daemon executable
deskwrap_stage_runtime_dlls(deskwrap-daemon)
