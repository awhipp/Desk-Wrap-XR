#include "monitor_finder.hpp"
#include <algorithm>
#include <iostream>

namespace deskwrap::compositor {

namespace {

std::string normalize_string(const std::string& str) {
    std::string norm = str;
    for (char& c : norm) {
        if (c == '#') {
            c = '\\';
        } else {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return norm;
}

} // namespace

bool is_glasses_monitor_match(
    const std::string& candidate_device_id,
    const std::string& candidate_device_string,
    const std::string& target_hardware_id,
    const std::string& target_edid_name
) {
    std::string cand_id_norm = normalize_string(candidate_device_id);
    std::string cand_str_norm = normalize_string(candidate_device_string);
    std::string target_hw_norm = normalize_string(target_hardware_id);
    std::string target_edid_norm = normalize_string(target_edid_name);

    // 1. Direct target HW ID match or substring
    if (!target_hw_norm.empty() && cand_id_norm.find(target_hw_norm) != std::string::npos) {
        return true;
    }

    // 2. Hardware ID substring for VITURE monitor VID/PID (VIT0120)
    if (cand_id_norm.find("VIT0120") != std::string::npos) {
        return true;
    }

    // 3. EDID friendly name match
    if (!target_edid_norm.empty() && cand_str_norm.find(target_edid_norm) != std::string::npos) {
        return true;
    }

    // 4. Case-insensitive substring match for VITURE Beast
    if (cand_str_norm.find("VITURE BEAST") != std::string::npos) {
        return true;
    }

    return false;
}

#ifdef _WIN32

std::string to_narrow_string(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size, nullptr, nullptr);
    return result;
}

namespace {

struct EnumContext {
    std::string target_hw_id;
    std::string target_edid_name;
    GlassesMonitorInfo result;
};

BOOL CALLBACK enum_mon_proc(HMONITOR hMon, HDC, LPRECT, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumContext*>(lParam);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return TRUE;

    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    DWORD dev_idx = 0;

    while (EnumDisplayDevicesW(mi.szDevice, dev_idx, &dd, 1 /* EDID_NAME */)) {
        std::string s_id = to_narrow_string(dd.DeviceID);
        std::string s_str = to_narrow_string(dd.DeviceString);

        if (is_glasses_monitor_match(s_id, s_str, ctx->target_hw_id, ctx->target_edid_name)) {
            ctx->result.found = true;
            ctx->result.adapter_name = mi.szDevice;
            ctx->result.hardware_id = s_id;
            ctx->result.edid_name = s_str;
            ctx->result.bounds = mi.rcMonitor;
            ctx->result.width = mi.rcMonitor.right - mi.rcMonitor.left;
            ctx->result.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
            return FALSE; // Stop enumeration once found
        }
        dev_idx++;
    }
    return TRUE;
}

} // namespace

GlassesMonitorInfo find_glasses_monitor(
    const std::string& target_hardware_id,
    const std::string& target_edid_name
) {
    EnumContext ctx;
    ctx.target_hw_id = target_hardware_id;
    ctx.target_edid_name = target_edid_name;

    EnumDisplayMonitors(nullptr, nullptr, enum_mon_proc, reinterpret_cast<LPARAM>(&ctx));

    if (!ctx.result.found) {
        DISPLAY_DEVICEW adapter{};
        adapter.cb = sizeof(adapter);
        DWORD a_idx = 0;
        while (EnumDisplayDevicesW(nullptr, a_idx, &adapter, 0)) {
            DISPLAY_DEVICEW mon{};
            mon.cb = sizeof(mon);
            DWORD m_idx = 0;
            while (EnumDisplayDevicesW(adapter.DeviceName, m_idx, &mon, 1)) {
                std::string s_id = to_narrow_string(mon.DeviceID);
                std::string s_str = to_narrow_string(mon.DeviceString);

                if (is_glasses_monitor_match(s_id, s_str, target_hardware_id, target_edid_name)) {
                    DEVMODEW dm{};
                    dm.dmSize = sizeof(dm);
                    if (EnumDisplaySettingsW(adapter.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
                        ctx.result.found = true;
                        ctx.result.adapter_name = adapter.DeviceName;
                        ctx.result.hardware_id = s_id;
                        ctx.result.edid_name = s_str;
                        ctx.result.bounds.left = dm.dmPosition.x;
                        ctx.result.bounds.top = dm.dmPosition.y;
                        ctx.result.bounds.right = dm.dmPosition.x + dm.dmPelsWidth;
                        ctx.result.bounds.bottom = dm.dmPosition.y + dm.dmPelsHeight;
                        ctx.result.width = dm.dmPelsWidth;
                        ctx.result.height = dm.dmPelsHeight;
                        return ctx.result;
                    }
                }
                m_idx++;
            }
            a_idx++;
        }
    }

    return ctx.result;
}

#else

GlassesMonitorInfo find_glasses_monitor(const std::string&, const std::string&) {
    return GlassesMonitorInfo{};
}

#endif

} // namespace deskwrap::compositor
