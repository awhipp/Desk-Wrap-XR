# Desk Wrap XR

Desk Wrap XR transforms your operating system desktop into a continuous, inward-facing 360-degree cylindrical workspace rendered on XR glasses. The system decouples platform-specific display virtualization from the low-latency 3D spatial rendering loop via an inter-process communication (IPC) and shared-GPU-texture architecture.

## Architecture

```txt
┌────────────────────────────────────────────────────────────────────────┐
│ deskwrap-daemon (Platform-Specific Host Service)                       │
│                                                                        │
│ 1. Virtual Display Engine: Spawns ultrawide framebuffer (e.g. 11520x1080)│
│    - Windows: Indirect Display Driver (IDD)                            │
│    - macOS: CGVirtualDisplay / DriverKit                               │
│ 2. Screen Capture: Zero-copy hardware frame acquisition                │
│    - Windows: DXGI Desktop Duplication (Direct3D 11)                   │
│    - macOS: ScreenCaptureKit (IOSurface)                               │
│ 3. Boundary Wrap Daemon: Intercepts and wraps mouse cursor coordinates │
│ 4. Shared Surface Exporter: Broadcasts texture handles via IPC         │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Shared Direct3D/Vulkan Handle / IPC
┌───────────────────────────────────▼────────────────────────────────────┐
│ deskwrap-compositor (Portable Spatial Engine)                          │
│                                                                        │
│ 1. IMU Driver: Ingests up to 1000Hz orientation data via VITURE SDK     │
│ 2. Predictor & Pose Filter: Low-latency orientation prediction          │
│ 3. Mesh Renderer: Maps shared desktop texture to inward cylinder mesh  │
│ 4. Frame Output: Borderless fullscreen display on physical XR glasses  │
└────────────────────────────────────────────────────────────────────────┘

```

## Subsystems

### 1. `deskwrap-daemon`

* **Virtual Display Allocation**: Creates an ultra-wide headless display topology that the operating system treats as a single continuous monitor surface.
* **Zero-Copy Capture**: Extracts rendered frames directly from VRAM into a shared hardware texture resource without round-tripping across system RAM.
* **Cursor Seam Management**: Installs a low-level input hook to detect when the pointer crosses the outer coordinate boundaries (X = 0 and X = Max), automatically warping the pointer to the opposing edge to provide seamless toroidal movement.
* **IPC Transport**: Exposes a named pipe or UNIX domain socket for control messages, passing shared GPU surface handles (`HANDLE` on Windows, `IOSurfaceID` on macOS) to the compositor process.

### 2. `deskwrap-compositor`

* **Telemetry Intake**: Interfaces with the VITURE SDK (`glasses.dll`) over USB HID to stream continuous roll, pitch, and yaw quaternions.
* **Geometry Engine**: Renders an inward-facing parametric cylinder centered at camera origin (0, 0, 0) with texture sampler addressing set to repeat mode (`WRAP`).
* **View Transformation**: Inverts head-pose orientation to align the virtual camera directly with physical gaze direction.
* **Render Targets**: Delivers synchronized 1920x1200 monoscopic or 3840x1200 side-by-side (SBS) frame buffers to the physical glasses output at 60Hz or 120Hz.

## Repository Structure

```txt
desk-wrap-xr/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── docs/
│   ├── architecture.md
│   ├── ipc-protocol.md
│   └── virtual-display-setup.md
├── proto/
│   └── deskwrap_ipc.proto
├── src/
│   ├── common/
│   │   ├── math/
│   │   │   ├── geometry.hpp
│   │   │   └── quaternion.hpp
│   │   └── ipc/
│   │       ├── shared_memory.hpp
│   │       └── socket_client.hpp
│   ├── daemon/
│   │   ├── CMakeLists.txt
│   │   ├── main.cpp
│   │   ├── windows/
│   │   │   ├── idd_manager.cpp
│   │   │   ├── dxgi_capture.cpp
│   │   │   └── mouse_hook.cpp
│   │   └── macos/
│   │       ├── virtual_display.mm
│   │       ├── screencapturekit.mm
│   │       └── cursor_wrap.mm
│   └── compositor/
│       ├── CMakeLists.txt
│       ├── main.cpp
│       ├── hardware/
│       │   ├── viture_sdk_client.cpp
│       │   └── imu_filter.cpp
│       └── render/
│           ├── cylinder_mesh.cpp
│           ├── shader_pipeline.cpp
│           ├── texture_importer.cpp
│           └── d3d11_renderer.cpp
└── third_party/
    ├── viture-sdk/
    └── glm/

```

## System Implementation Matrix

| Subsystem | Windows Target (Phase 1) | macOS Target (Phase 2) |
| --- | --- | --- |
| Virtual Display | Windows Indirect Display Driver (IDD) | `CGVirtualDisplay` CoreGraphics private API |
| Frame Capture | Direct3D 11 DXGI Desktop Duplication | ScreenCaptureKit backed by `IOSurface` |
| Inter-Process Sharing | `IDXGIResource::GetSharedHandle` | `IOSurfaceLookup` |
| Pointer Handling | `WH_MOUSE_LL` hook with `SetCursorPos` | `CGEventTap` with `CGWarpMouseCursorPosition` |
| Compositor Backend | Direct3D 11 or Vulkan | Metal |
| Orientation Input | `glasses.dll` (libusb) | `libglasses.dylib` (IOKit HID) |
