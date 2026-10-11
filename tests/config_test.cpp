#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <cmath>
#include "deskwrap/common/config.hpp"

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

namespace {

void test_missing_config_file() {
    std::cout << "[TEST] Running test_missing_config_file...\n";
    std::filesystem::path non_existent_path = "non_existent_config_12345.json";
    if (std::filesystem::exists(non_existent_path)) {
        std::filesystem::remove(non_existent_path);
    }

    deskwrap::ConfigLoadResult res = deskwrap::load_config(non_existent_path);

    TEST_CHECK(res.status == deskwrap::ConfigLoadStatus::FileNotFound);
    // Confirm default values, including 180° arc
    TEST_CHECK(std::abs(res.config.arc_degrees - 180.0f) < 0.001f);
    TEST_CHECK(std::abs(res.config.min_arc_degrees - 180.0f) < 0.001f);
    TEST_CHECK(std::abs(res.config.max_arc_degrees - 360.0f) < 0.001f);
    TEST_CHECK(res.config.pose_prediction_enabled == true);
    TEST_CHECK(res.config.monitor_hardware_id == "DISPLAY\\VIT0120");
    TEST_CHECK(res.config.monitor_edid_name == "VITURE Beast");

    std::cout << "[TEST] test_missing_config_file PASSED.\n\n";
}

void test_malformed_config_file() {
    std::cout << "[TEST] Running test_malformed_config_file...\n";
    std::filesystem::path malformed_path = "test_malformed_config.json";

    // Write invalid JSON with syntax error
    {
        std::ofstream out(malformed_path, std::ios::trunc);
        out << "{\n"
            << "  \"arc_degrees\": 240.0,\n"
            << "  \"invalid_syntax\": [\n"
            << "}\n";
    }

    deskwrap::ConfigLoadResult res = deskwrap::load_config(malformed_path);

    // Confirm that malformed JSON status is flagged and falls back to default 180° arc
    TEST_CHECK(res.status == deskwrap::ConfigLoadStatus::MalformedJson);
    TEST_CHECK(std::abs(res.config.arc_degrees - 180.0f) < 0.001f);
    TEST_CHECK(res.config.pose_prediction_enabled == true);

    std::filesystem::remove(malformed_path);
    std::cout << "[TEST] test_malformed_config_file PASSED.\n\n";
}

void test_valid_config_file() {
    std::cout << "[TEST] Running test_valid_config_file...\n";
    std::filesystem::path valid_path = "test_valid_config.json";

    {
        std::ofstream out(valid_path, std::ios::trunc);
        out << "{\n"
            << "  \"arc_degrees\": 270.0,\n"
            << "  \"min_arc_degrees\": 180.0,\n"
            << "  \"max_arc_degrees\": 360.0,\n"
            << "  \"radius\": 2.0,\n"
            << "  \"pose_prediction_enabled\": false,\n"
            << "  \"monitor_hardware_id\": \"DISPLAY\\\\VIT0120\",\n"
            << "  \"monitor_edid_name\": \"VITURE Beast\"\n"
            << "}\n";
    }

    deskwrap::ConfigLoadResult res = deskwrap::load_config(valid_path);

    TEST_CHECK(res.status == deskwrap::ConfigLoadStatus::Success);
    TEST_CHECK(std::abs(res.config.arc_degrees - 270.0f) < 0.001f);
    TEST_CHECK(std::abs(res.config.radius - 2.0f) < 0.001f);
    TEST_CHECK(res.config.pose_prediction_enabled == false);
    TEST_CHECK(res.config.monitor_hardware_id == "DISPLAY\\VIT0120");

    std::filesystem::remove(valid_path);
    std::cout << "[TEST] test_valid_config_file PASSED.\n\n";
}

void test_save_and_reload_config() {
    std::cout << "[TEST] Running test_save_and_reload_config...\n";
    std::filesystem::path roundtrip_path = "test_roundtrip_config.json";

    deskwrap::DeskWrapConfig original;
    original.arc_degrees = 220.0f;
    original.pose_prediction_enabled = false;
    original.radius = 1.5f;

    bool saved = deskwrap::save_config(original, roundtrip_path);
    TEST_CHECK(saved);

    deskwrap::ConfigLoadResult res = deskwrap::load_config(roundtrip_path);
    TEST_CHECK(res.status == deskwrap::ConfigLoadStatus::Success);
    TEST_CHECK(std::abs(res.config.arc_degrees - 220.0f) < 0.001f);
    TEST_CHECK(res.config.pose_prediction_enabled == false);
    TEST_CHECK(std::abs(res.config.radius - 1.5f) < 0.001f);

    std::filesystem::remove(roundtrip_path);
    std::cout << "[TEST] test_save_and_reload_config PASSED.\n\n";
}

} // namespace

int main() {
    std::cout << "=== DeskWrap XR Config Unit Tests ===\n\n";
    test_missing_config_file();
    test_malformed_config_file();
    test_valid_config_file();
    test_save_and_reload_config();
    std::cout << "All config tests PASSED successfully!\n";
    return 0;
}
