#pragma once

#include <string>
#include <vector>

/**
 * @brief Result of scanning USBDevice (Provider libwdi) and libusbK devices (Provider libusbK) setup classes (Zadig).
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
 * @brief Enumerate USBDevice (libwdi only) and libusbK devices (libusbK only); class and provider are not mixed.
 *
 * Uses DEVPKEY_Device_DriverProvider via SetupAPI.
 */
LibwdiUsbProbeResult ProbeLibwdiUsbDevices();
