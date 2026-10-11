#include <iostream>
#include <string>
#include <filesystem>
#include "deskwrap/common/config.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
    std::filesystem::path config_path;
    bool validate_only = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--validate-config") {
            validate_only = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "DeskWrap XR Daemon\n"
                      << "Usage: deskwrap-daemon [options]\n"
                      << "Options:\n"
                      << "  --config <path>      Specify custom path to config.json\n"
                      << "  --validate-config    Validate config loading and exit\n"
                      << "  --help, -h           Show this help message\n";
            return 0;
        }
    }

    std::cout << "[Daemon] Initializing DeskWrap XR Daemon...\n";

    deskwrap::ConfigLoadResult config_res = deskwrap::load_config(config_path);
    const auto& config = config_res.config;

    deskwrap::print_config(config, std::cout);

    if (validate_only) {
        std::cout << "[Daemon] Configuration validation completed.\n";
        return (config_res.status == deskwrap::ConfigLoadStatus::MalformedJson) ? 2 : 0;
    }

    std::cout << "[Daemon] Daemon running with arc: " << config.arc_degrees << "°.\n";

#ifdef _WIN32
    // Minimal message loop for background daemon presence
    MSG msg{};
    // When running interactively without a window, exit if Ctrl+C or quit requested
    std::cout << "[Daemon] Press Ctrl+C to terminate daemon.\n";
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
#endif

    std::cout << "[Daemon] Shutting down.\n";
    return 0;
}
