cmake_minimum_required(VERSION 3.24)

add_executable(deskwrap-compositor
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
    )
endif()

target_compile_features(deskwrap-compositor PRIVATE cxx_std_20)

# Stage VITURE runtime DLLs next to the compositor executable
deskwrap_stage_runtime_dlls(deskwrap-compositor)
