#include "virtual_display.hpp"
#include <setupapi.h>
#include <devguid.h>
#include <iostream>

namespace deskwrap::daemon {

VirtualDisplayManager::~VirtualDisplayManager() {
    restore_display_mode();
    if (is_driver_enabled_) {
        disable_virtual_driver();
    }
}

bool VirtualDisplayManager::enable_virtual_driver() {
    // SetupAPI enable for device with Hardware ID Root\VirtualDisplayDriver
    HDEVINFO dev_info = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (dev_info == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVINFO_DATA dev_data{};
    dev_data.cbSize = sizeof(SP_DEVINFO_DATA);

    bool found = false;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_data); ++i) {
        wchar_t hwid[512] = {};
        if (SetupDiGetDeviceRegistryPropertyW(dev_info, &dev_data, SPDRP_HARDWAREID, nullptr,
                                              reinterpret_cast<PBYTE>(hwid), sizeof(hwid), nullptr)) {
            if (wcsstr(hwid, L"VirtualDisplayDriver") != nullptr) {
                SP_PROPCHANGE_PARAMS pcp{};
                pcp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
                pcp.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
                pcp.StateChange = DICS_ENABLE;
                pcp.Scope = DICS_FLAG_GLOBAL;

                if (SetupDiSetClassInstallParamsW(dev_info, &dev_data, &pcp.ClassInstallHeader, sizeof(pcp)) &&
                    SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, dev_info, &dev_data)) {
                    is_driver_enabled_ = true;
                    found = true;
                    break;
                }
            }
        }
    }

    SetupDiDestroyDeviceInfoList(dev_info);
    return found;
}

bool VirtualDisplayManager::disable_virtual_driver() {
    HDEVINFO dev_info = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (dev_info == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVINFO_DATA dev_data{};
    dev_data.cbSize = sizeof(SP_DEVINFO_DATA);

    bool disabled = false;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_data); ++i) {
        wchar_t hwid[512] = {};
        if (SetupDiGetDeviceRegistryPropertyW(dev_info, &dev_data, SPDRP_HARDWAREID, nullptr,
                                              reinterpret_cast<PBYTE>(hwid), sizeof(hwid), nullptr)) {
            if (wcsstr(hwid, L"VirtualDisplayDriver") != nullptr) {
                SP_PROPCHANGE_PARAMS pcp{};
                pcp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
                pcp.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
                pcp.StateChange = DICS_DISABLE;
                pcp.Scope = DICS_FLAG_GLOBAL;

                if (SetupDiSetClassInstallParamsW(dev_info, &dev_data, &pcp.ClassInstallHeader, sizeof(pcp)) &&
                    SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, dev_info, &dev_data)) {
                    is_driver_enabled_ = false;
                    disabled = true;
                    break;
                }
            }
        }
    }

    SetupDiDestroyDeviceInfoList(dev_info);
    return disabled;
}

bool VirtualDisplayManager::find_virtual_display_device(std::wstring& out_device_name) {
    DISPLAY_DEVICEW disp_dev{};
    disp_dev.cbSize = sizeof(disp_dev);

    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &disp_dev, 0); ++i) {
        if (wcsstr(disp_dev.DeviceString, L"Virtual") != nullptr ||
            wcsstr(disp_dev.DeviceString, L"DeskWrap") != nullptr) {
            out_device_name = disp_dev.DeviceName;
            active_device_name_ = disp_dev.DeviceName;
            return true;
        }
    }
    return false;
}

bool VirtualDisplayManager::apply_display_mode(const DisplayMode& mode) {
    if (active_device_name_.empty() && !find_virtual_display_device(active_device_name_)) {
        return false;
    }

    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    dm.dmPelsWidth = mode.width;
    dm.dmPelsHeight = mode.height;
    dm.dmDisplayFrequency = mode.refresh_rate_hz;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;

    LONG res = ChangeDisplaySettingsExW(active_device_name_.c_str(), &dm, nullptr,
                                         CDS_UPDATEREGISTRY | CDS_GLOBAL, nullptr);
    if (res == DISP_CHANGE_SUCCESSFUL) {
        is_mode_applied_ = true;
        return true;
    }
    return false;
}

bool VirtualDisplayManager::restore_display_mode() {
    if (is_mode_applied_ && !active_device_name_.empty()) {
        ChangeDisplaySettingsExW(active_device_name_.c_str(), nullptr, nullptr, 0, nullptr);
        is_mode_applied_ = false;
        return true;
    }
    return false;
}

} // namespace deskwrap::daemon
