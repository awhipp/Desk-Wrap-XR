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
