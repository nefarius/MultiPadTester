#pragma once

#include <string>
#include <vector>

/**
 * @brief Result of scanning for USBDevice-class devices whose driver Provider is libwdi (Zadig).
 */
struct LibwdiUsbProbeResult
{
	/** Device instance IDs (PnP) for matching devices, sorted for stable UI. */
	std::vector<std::wstring> instanceIds;
};

/**
 * @brief Enumerate present devices in setup class USBDevice and collect those with Provider "libwdi".
 *
 * Uses DEVPKEY_Device_DriverProvider via SetupAPI.
 */
LibwdiUsbProbeResult ProbeLibwdiUsbDevices();
