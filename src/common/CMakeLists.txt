cmake_minimum_required(VERSION 3.24)

add_library(deskwrap-common STATIC
    include/deskwrap/common/types.hpp
    include/deskwrap/common/config.hpp
    include/deskwrap/common/ipc_protocol.hpp
    include/deskwrap/common/ipc_channel.hpp
    include/deskwrap/common/cylinder_mesh.hpp
    src/config.cpp
    src/ipc_channel.cpp
    src/cylinder_mesh.cpp
)

target_include_directories(deskwrap-common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(deskwrap-common PUBLIC cxx_std_20)
