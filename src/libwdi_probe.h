#pragma once

#include <string>
#include <vector>

/**
 * @brief Result of scanning USBDevice (libwdi), libusbK (libusbk), libusb-win32 (libusb-win32) setup classes (Zadig).
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
 * @brief Enumerate the above classes; each class only matches its own Provider string.
 *
 * Uses DEVPKEY_Device_DriverProvider via SetupAPI.
 */
LibwdiUsbProbeResult ProbeLibwdiUsbDevices();
