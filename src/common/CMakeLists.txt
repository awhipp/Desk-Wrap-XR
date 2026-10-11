cmake_minimum_required(VERSION 3.24)

add_library(deskwrap-common STATIC
    include/deskwrap/common/types.hpp
    include/deskwrap/common/config.hpp
    src/config.cpp
)

target_include_directories(deskwrap-common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(deskwrap-common PUBLIC cxx_std_20)
