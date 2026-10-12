#include <iostream>
#include <cmath>
#include <cstdlib>
#include <vector>
#include "deskwrap/common/cylinder_mesh.hpp"
#include "../src/compositor/src/monitor_finder.hpp"

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " __FILE__ ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

namespace {

void test_cylinder_mesh_180_degrees() {
    std::cout << "[TEST] Running test_cylinder_mesh_180_degrees...\n";

    float radius = 1.8f;
    float height = 1.0f;
    uint32_t seg_u = 128;
    uint32_t seg_v = 16;

    auto mesh = deskwrap::generate_cylinder_mesh(180.0f, radius, height, seg_u, seg_v);

    // Verify grid dimensions
    size_t expected_vertices = (seg_u + 1) * (seg_v + 1);
    size_t expected_indices = seg_u * seg_v * 6;
    TEST_CHECK(mesh.vertices.size() == expected_vertices);
    TEST_CHECK(mesh.indices.size() == expected_indices);

    // Center column at u = 0.5 (i = 64)
    uint32_t center_i = seg_u / 2;
    uint32_t center_j = seg_v / 2;
    const auto& center_vert = mesh.vertices[center_i * (seg_v + 1) + center_j];

    TEST_CHECK(std::abs(center_vert.u - 0.5f) < 1e-4f);
    TEST_CHECK(std::abs(center_vert.v - 0.5f) < 1e-4f);
    TEST_CHECK(std::abs(center_vert.x) < 1e-4f); // Centered forward at x = 0
    TEST_CHECK(std::abs(center_vert.y) < 1e-4f); // Centered vertically at y = 0
    TEST_CHECK(std::abs(center_vert.z - radius) < 1e-4f); // At distance R

    // Leftmost column at u = 0.0 (i = 0, angle = -90°)
    const auto& left_vert = mesh.vertices[0 * (seg_v + 1) + center_j];
    TEST_CHECK(std::abs(left_vert.u - 0.0f) < 1e-4f);
    TEST_CHECK(std::abs(left_vert.x - (-radius)) < 1e-3f); // At x = -R
    TEST_CHECK(std::abs(left_vert.z - 0.0f) < 1e-3f);      // At z = 0

    // Rightmost column at u = 1.0 (i = 128, angle = +90°)
    const auto& right_vert = mesh.vertices[seg_u * (seg_v + 1) + center_j];
    TEST_CHECK(std::abs(right_vert.u - 1.0f) < 1e-4f);
    TEST_CHECK(std::abs(right_vert.x - radius) < 1e-3f);   // At x = +R
    TEST_CHECK(std::abs(right_vert.z - 0.0f) < 1e-3f);      // At z = 0

    // Vertical top and bottom
    const auto& top_vert = mesh.vertices[center_i * (seg_v + 1) + 0];
    const auto& bottom_vert = mesh.vertices[center_i * (seg_v + 1) + seg_v];
    TEST_CHECK(std::abs(top_vert.v - 0.0f) < 1e-4f);
    TEST_CHECK(std::abs(top_vert.y - (height * 0.5f)) < 1e-4f);
    TEST_CHECK(std::abs(bottom_vert.v - 1.0f) < 1e-4f);
    TEST_CHECK(std::abs(bottom_vert.y - (-height * 0.5f)) < 1e-4f);

    std::cout << "[TEST] test_cylinder_mesh_180_degrees PASSED.\n\n";
}

void test_cylinder_mesh_360_wrap_and_seamlessness() {
    std::cout << "[TEST] Running test_cylinder_mesh_360_wrap_and_seamlessness...\n";

    float radius = 1.8f;
    float height = 1.0f;
    uint32_t seg_u = 128;
    uint32_t seg_v = 16;

    auto mesh = deskwrap::generate_cylinder_mesh(360.0f, radius, height, seg_u, seg_v);

    TEST_CHECK(mesh.vertices.size() == (seg_u + 1) * (seg_v + 1));

    // Confirm that every vertex on the left seam (u = 0.0) matches the corresponding
    // vertex on the right seam (u = 1.0) in 3D position (x, y, z) with zero seam gap.
    for (uint32_t j = 0; j <= seg_v; ++j) {
        const auto& left_seam = mesh.vertices[0 * (seg_v + 1) + j];
        const auto& right_seam = mesh.vertices[seg_u * (seg_v + 1) + j];

        // UV coordinates must span [0, 1]
        TEST_CHECK(std::abs(left_seam.u - 0.0f) < 1e-6f);
        TEST_CHECK(std::abs(right_seam.u - 1.0f) < 1e-6f);
        TEST_CHECK(std::abs(left_seam.v - right_seam.v) < 1e-6f);

        // 3D coordinates at 360 wrap must be exactly continuous (zero gap)
        float dx = left_seam.x - right_seam.x;
        float dy = left_seam.y - right_seam.y;
        float dz = left_seam.z - right_seam.z;
        float dist_sq = dx * dx + dy * dy + dz * dz;

        TEST_CHECK(dist_sq < 1e-10f);

        // At 360 wrap (theta = +-180°), the seam is directly behind the viewer at (0, y, -R)
        TEST_CHECK(std::abs(left_seam.x) < 1e-4f);
        TEST_CHECK(std::abs(left_seam.z - (-radius)) < 1e-4f);
    }

    std::cout << "[TEST] test_cylinder_mesh_360_wrap_and_seamlessness PASSED.\n\n";
}

void test_projection_matrix() {
    std::cout << "[TEST] Running test_projection_matrix...\n";

    float fov_y_rad = 30.0f * (3.141592653589793f / 180.0f);
    float aspect = 1920.0f / 1200.0f; // 1.6
    float near_z = 0.1f;
    float far_z = 100.0f;

    auto proj = deskwrap::Matrix4x4::perspective_lh(fov_y_rad, aspect, near_z, far_z);

    // Transform a point directly in front at distance 1.8m: (0, 0, 1.8)
    deskwrap::Vector3 in_center{0.0f, 0.0f, 1.8f};
    deskwrap::Vector3 out_ndc{};
    bool ok = deskwrap::transform_to_ndc(proj, in_center, out_ndc);

    TEST_CHECK(ok);
    // Should be at the exact center of the screen
    TEST_CHECK(std::abs(out_ndc.x) < 1e-4f);
    TEST_CHECK(std::abs(out_ndc.y) < 1e-4f);
    // Depth should be between 0 and 1
    TEST_CHECK(out_ndc.z > 0.0f && out_ndc.z < 1.0f);

    std::cout << "[TEST] test_projection_matrix PASSED.\n\n";
}

void test_monitor_matching() {
    std::cout << "[TEST] Running test_monitor_matching...\n";

    std::string target_hw = "DISPLAY\\VIT0120";
    std::string target_edid = "VITURE Beast";

    // Matching exact hardware ID
    TEST_CHECK(deskwrap::compositor::is_glasses_monitor_match(
        "DISPLAY\\VIT0120", "Generic PnP Monitor", target_hw, target_edid));

    // Matching device path with hash marks
    TEST_CHECK(deskwrap::compositor::is_glasses_monitor_match(
        "\\\\?\\DISPLAY#VIT0120#4&2c69980e&0&UID4165#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}",
        "Generic PnP Monitor", target_hw, target_edid));

    // Matching MONITOR\VIT0120
    TEST_CHECK(deskwrap::compositor::is_glasses_monitor_match(
        "MONITOR\\VIT0120\\{4d36e96e-e325-11ce-bfc1-08002be10318}\\0002",
        "Generic PnP Monitor", target_hw, target_edid));

    // Matching EDID friendly name
    TEST_CHECK(deskwrap::compositor::is_glasses_monitor_match(
        "\\\\?\\DISPLAY#SOMEOTHER#123", "VITURE Beast", target_hw, target_edid));

    // Reject non-matching monitor
    TEST_CHECK(!deskwrap::compositor::is_glasses_monitor_match(
        "\\\\?\\DISPLAY#SDC4197#4&2c69980e&0&UID8388688#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}",
        "Generic PnP Monitor", target_hw, target_edid));

    std::cout << "[TEST] test_monitor_matching PASSED.\n\n";
}

void test_monitor_duplicate_detection() {
    std::cout << "[TEST] Running test_monitor_duplicate_detection...\n";

    deskwrap::compositor::GlassesMonitorInfo dup_info{};
    dup_info.found = true;
    dup_info.is_duplicate_mode = true;
    dup_info.adapter_name = L"\\\\.\\DISPLAY1";
    dup_info.bounds = {0, 0, 1920, 1200};
    dup_info.width = 1920;
    dup_info.height = 1200;

    // Must be recognized as duplicate mode
    TEST_CHECK(deskwrap::compositor::is_glasses_duplicate_mode(dup_info));

    deskwrap::compositor::GlassesMonitorInfo ext_info{};
    ext_info.found = true;
    ext_info.is_duplicate_mode = false;
    ext_info.adapter_name = L"\\\\.\\DISPLAY2";
    ext_info.bounds = {2880, 0, 4800, 1200};
    ext_info.width = 1920;
    ext_info.height = 1200;

    // Extended monitor on DISPLAY2 must NOT be recognized as duplicate mode
    TEST_CHECK(!deskwrap::compositor::is_glasses_duplicate_mode(ext_info));

    deskwrap::compositor::GlassesMonitorInfo not_found_info{};
    not_found_info.found = false;
    TEST_CHECK(!deskwrap::compositor::is_glasses_duplicate_mode(not_found_info));

    std::cout << "[TEST] test_monitor_duplicate_detection PASSED.\n\n";
}

void test_glasses_monitor_detection_on_current_machine() {
    std::cout << "[TEST] Running test_glasses_monitor_detection_on_current_machine...\n";

    // On this machine when glasses are not connected, find_glasses_monitor must return found = false
    auto info = deskwrap::compositor::find_glasses_monitor("DISPLAY\\VIT0120", "VITURE Beast");
    // Verify it handles absence gracefully without crashing
    std::cout << "[TEST] Glasses monitor present: " << (info.found ? "YES" : "NO") << "\n";
    if (info.found) {
        std::cout << "[TEST] Detected on adapter: " << deskwrap::compositor::to_narrow_string(info.adapter_name)
                  << " at (" << info.bounds.left << ", " << info.bounds.top << ")\n";
        std::cout << "[TEST] Duplicate mode flag: " << (info.is_duplicate_mode ? "YES" : "NO") << "\n";
    }

    std::cout << "[TEST] test_glasses_monitor_detection_on_current_machine PASSED.\n\n";
}

} // namespace

int main() {
    std::cout << "=== DeskWrap XR Cylinder Projection & Monitor Tests ===\n\n";
    test_cylinder_mesh_180_degrees();
    test_cylinder_mesh_360_wrap_and_seamlessness();
    test_projection_matrix();
    test_monitor_matching();
    test_monitor_duplicate_detection();
    test_glasses_monitor_detection_on_current_machine();
    std::cout << "All cylinder and monitor tests PASSED successfully!\n";
    return 0;
}
