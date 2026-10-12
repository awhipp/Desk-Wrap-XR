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

## Developer Guide

### Prerequisites (Windows)

* **Compiler & Toolchain**: Visual Studio 2022 (MSVC v143) with C++ Desktop tools and Windows SDK.
  To install via `winget` in an elevated Administrator prompt:

  ```powershell
  winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
  ```

  *(Note: `--includeRecommended` is required to ensure `cl.exe` (MSVC compiler) and the Windows SDK headers/libraries are installed).*
* **Hardware SDK (VITURE Windows SDK)**:
  Due to licensing, the VITURE SDK binaries and headers are third-party proprietary assets and are excluded from source control via `.gitignore`.
  Developers must download the Windows x86_64 SDK from the VITURE developer portal and unpack it into the repository root:

  ```text
  desk-wrap-xr/
  └── VITURE_XR_Glasses_SDK_for_Windows_x86_64/
      ├── include/      # viture_glasses_provider.h and SDK headers
      └── x86_64/       # glasses.lib, glasses.dll, libusb-1.0.dll, carina_vio.dll, etc.
  ```

  *(Alternatively, point CMake to a custom location using `-DVITURE_SDK_DIR=<path>`)*.

### Building

Configure and build both `deskwrap-daemon` and `deskwrap-compositor` in one step:

```cmd
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

All build artifacts and staged runtime DLLs (`glasses.dll`, `libusb-1.0.dll`, `carina_vio.dll`, `glew32.dll`, `opencv_world4100.dll`) are placed directly into `build/bin/Release/`.

### Configuration & User Settings

Both executables load runtime settings from `%APPDATA%\DeskWrapXR\config.json`:

* **Zero-Setup Defaults**: If no configuration file exists, the system automatically uses sensible defaults (180° cylindrical arc, 180°–360° adjustment range, head-tracking pose prediction enabled, and monitor hardware ID `DISPLAY\VIT0120`).
* **Error Resilience**: If the configuration file contains malformed JSON or invalid types, an informative error is logged to `stderr` and the application safely falls back to defaults without crashing.
* **Testing CLI Options**: Both executables support `--config <path>` to specify an alternative configuration file and `--validate-config` to verify settings and exit without starting background loops.

### Running Tests

Run the automated test suite via CTest:

```cmd
ctest --test-dir build -C Release --output-on-failure
```

### Hardware & Display Setup (VITURE Glasses)

* **Extended Display Requirement**: Windows display topology must be configured to **"Extend these displays"** (press `Win + P` and select **Extend**). In "Duplicate" (mirrored) mode, the compositor's fullscreen window covers the primary display at `(0, 0)`, occluding the desktop captured by DXGI duplication and causing an occlusion blackout loop. The compositor actively detects duplicate topology and warns if not extended.
* **Standard Display Mode**: Ensure the glasses are operating in standard DisplayPort mode (not onboard 3DoF / pinned / anchored mode), as DeskWrap XR manages spatial orientation, cylinder mapping, and head tracking directly.
* **Preview Mode**: Running `deskwrap-compositor.exe --preview` launches a windowed 1280x720 preview on the primary display for desktop testing, regardless of whether glasses are connected.

### Implementation Roadmap & Vertical Slices

1. **Ticket #14 (Current)**: Build foundation, CMake MSVC configuration, DLL staging, and config loading.
2. **Ticket #15**: DXGI Desktop Duplication capture and shared D3D11 texture handle over IPC named pipe.
3. **Ticket #16**: Cylinder projection on the glasses monitor (`DISPLAY\VIT0120`, 180°–360° arc).
4. **Ticket #17**: Head tracking with constant-velocity pose prediction (VITURE SDK bypass mode, ~400 Hz IMU).
5. **Ticket #18**: Virtual display driver integration and seamless mouse wrap (`WH_MOUSE_LL`).
6. **Ticket #19**: Device lifecycle, dual-readiness debounce (USB + Monitor), auto-start, and crash recovery.
7. **Ticket #20**: Global hotkeys (`Ctrl+Alt+R` recenter, `Ctrl+Alt+Plus/Minus` arc adjustment) and tray menu.
8. **Ticket #21**: Automatic audio endpoint routing and restoration (`IMMDeviceEnumerator`).
9. **Ticket #22**: Single Inno Setup installer packaging.
