cmake_minimum_required(VERSION 3.24)

project(desk-wrap-xr
    VERSION 0.1.0
    DESCRIPTION "Toroidal cylindrical virtual desktop for XR glasses"
    LANGUAGES C CXX
)

# Enforce C++20 across all targets
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Output directory alignment for multi-config generators (MSVC Debug/Release)
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${CMAKE_BINARY_DIR}/lib/${OUTPUTCONFIG})
endforeach()

# Platform check: Desk Wrap XR Phase 1 targets Windows Direct3D 11 / DXGI
if(NOT WIN32)
    message(WARNING "Desk Wrap XR Phase 1 is optimized for Windows (MSVC, Direct3D 11, DXGI). Non-Windows platforms will only build portable common modules.")
endif()

# Global MSVC Compiler and Preprocessor Options
if(MSVC)
    add_compile_options(
        /W4
        /permissive-
        /EHsc
        /utf-8
        /MP
        /Zc:__cplusplus
    )
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# ==============================================================================
# Bundled VITURE XR Glasses SDK Setup
# ==============================================================================
set(VITURE_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/VITURE_XR_Glasses_SDK_for_Windows_x86_64" CACHE PATH "Path to VITURE Windows SDK")

if(NOT EXISTS "${VITURE_SDK_DIR}/include/viture_glasses_provider.h")
    # Fallback check for third_party path
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/viture-sdk/include/viture_glasses_provider.h")
        set(VITURE_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/viture-sdk")
    else()
        message(FATAL_ERROR "VITURE XR Glasses SDK not found at ${VITURE_SDK_DIR}. Please ensure the SDK folder is present.")
    endif()
endif()

message(STATUS "Using VITURE SDK from: ${VITURE_SDK_DIR}")

# Define imported target viture::glasses
add_library(viture::glasses SHARED IMPORTED GLOBAL)
set_target_properties(viture::glasses PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${VITURE_SDK_DIR}/include"
    IMPORTED_LOCATION "${VITURE_SDK_DIR}/x86_64/glasses.dll"
    IMPORTED_IMPLIB "${VITURE_SDK_DIR}/x86_64/glasses.lib"
)

# Collect runtime DLLs for staging
set(VITURE_RUNTIME_DLLS
    "${VITURE_SDK_DIR}/x86_64/glasses.dll"
    "${VITURE_SDK_DIR}/x86_64/libusb-1.0.dll"
    "${VITURE_SDK_DIR}/x86_64/carina_vio.dll"
    "${VITURE_SDK_DIR}/x86_64/glew32.dll"
    "${VITURE_SDK_DIR}/x86_64/opencv_world4100.dll"
)

# Post-build helper to stage VITURE runtime DLLs alongside target executables
function(deskwrap_stage_runtime_dlls TARGET_NAME)
    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${VITURE_RUNTIME_DLLS}
            $<TARGET_FILE_DIR:${TARGET_NAME}>
        COMMENT "Staging VITURE runtime DLLs to output directory for ${TARGET_NAME}"
    )
endfunction()

# Enable CTest suite
enable_testing()

# ==============================================================================
# Subdirectories
# ==============================================================================
add_subdirectory(src/common)
add_subdirectory(src/daemon)
add_subdirectory(src/compositor)
add_subdirectory(tests)
