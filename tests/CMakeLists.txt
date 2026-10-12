cmake_minimum_required(VERSION 3.24)

add_executable(deskwrap-config-tests
    config_test.cpp
)

target_link_libraries(deskwrap-config-tests
    PRIVATE
        deskwrap-common
)

if(WIN32)
    target_link_libraries(deskwrap-config-tests
        PRIVATE
            user32
            shell32
            ole32
    )
endif()

target_compile_features(deskwrap-config-tests PRIVATE cxx_std_20)

add_test(NAME config_tests COMMAND deskwrap-config-tests)

add_executable(deskwrap-ipc-tests
    ipc_test.cpp
    ../src/daemon/src/dxgi_capture.cpp
)

target_include_directories(deskwrap-ipc-tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/daemon/src
)

target_link_libraries(deskwrap-ipc-tests
    PRIVATE
        deskwrap-common
)

if(WIN32)
    target_link_libraries(deskwrap-ipc-tests
        PRIVATE
            user32
            shell32
            ole32
            d3d11
            dxgi
    )
endif()

target_compile_features(deskwrap-ipc-tests PRIVATE cxx_std_20)

add_test(NAME ipc_tests COMMAND deskwrap-ipc-tests)

add_executable(deskwrap-cylinder-tests
    cylinder_test.cpp
    ../src/compositor/src/monitor_finder.cpp
)

target_include_directories(deskwrap-cylinder-tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/compositor/src
)

target_link_libraries(deskwrap-cylinder-tests
    PRIVATE
        deskwrap-common
)

if(WIN32)
    target_link_libraries(deskwrap-cylinder-tests
        PRIVATE
            user32
            shell32
            ole32
            d3d11
            dxgi
    )
endif()

target_compile_features(deskwrap-cylinder-tests PRIVATE cxx_std_20)

add_test(NAME cylinder_tests COMMAND deskwrap-cylinder-tests)

