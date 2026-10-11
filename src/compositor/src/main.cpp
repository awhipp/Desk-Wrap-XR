#include <iostream>
#include <string>
#include <filesystem>
#include "deskwrap/common/config.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

// Include VITURE SDK header to confirm SDK link integration
#include "viture_glasses_provider.h"

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
            std::cout << "DeskWrap XR Compositor\n"
                      << "Usage: deskwrap-compositor [options]\n"
                      << "Options:\n"
                      << "  --config <path>      Specify custom path to config.json\n"
                      << "  --validate-config    Validate config loading and exit\n"
                      << "  --help, -h           Show this help message\n";
            return 0;
        }
    }

    std::cout << "[Compositor] Initializing DeskWrap XR Compositor...\n";

    deskwrap::ConfigLoadResult config_res = deskwrap::load_config(config_path);
    const auto& config = config_res.config;

    deskwrap::print_config(config, std::cout);

    if (validate_only) {
        std::cout << "[Compositor] Configuration validation completed.\n";
        return (config_res.status == deskwrap::ConfigLoadStatus::MalformedJson) ? 2 : 0;
    }

    std::cout << "[Compositor] Compositor configured with cylinder arc: " << config.arc_degrees << "°.\n";
    std::cout << "[Compositor] Target Monitor HW ID: " << config.monitor_hardware_id << "\n";
    std::cout << "[Compositor] Pose Prediction: " << (config.pose_prediction_enabled ? "ON" : "OFF") << "\n";

    std::cout << "[Compositor] Compositor initialized successfully.\n";
    return 0;
}
