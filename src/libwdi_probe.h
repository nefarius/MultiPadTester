#pragma once

#include <string>
#include <vector>

/**
 * @brief Result of scanning for USBDevice-class devices whose driver Provider is libwdi (Zadig).
 */
struct LibwdiUsbProbeResult
{
	/** False if enumeration or required queries failed; true if the scan completed (instanceIds may still be empty). */
	bool succeeded = false;
	/** Present when succeeded is false; describes the failure (e.g. Setup API error). */
	std::wstring errorMessage;
	/** Device instance IDs (PnP) for matching devices, sorted for stable UI. Valid only when succeeded is true. */
	std::vector<std::wstring> instanceIds;
};

/**
 * @brief Enumerate present devices in setup class USBDevice and collect those with Provider "libwdi".
 *
 * Uses DEVPKEY_Device_DriverProvider via SetupAPI.
 */
LibwdiUsbProbeResult ProbeLibwdiUsbDevices();
