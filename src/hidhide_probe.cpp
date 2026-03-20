#include "hidhide_probe.h"

#include <Windows.h>
#include <winioctl.h>

/**
 * @brief Probe the HidHide driver and report whether it is installed and active.
 *
 * Queries the HidHide device interface ("\\.\HidHide") using an IOCTL to determine
 * if the driver is present and currently active. Maps device-open and IOCTL outcomes
 * to a `HidHideStatus` value.
 *
 * @return HidHideStatus One of:
 * - `HidHideStatus::NotInstalled` if the device path does not exist.
 * - `HidHideStatus::AccessDenied` if opening the device was denied.
 * - `HidHideStatus::QueryFailed` if opening the device or issuing the IOCTL failed
 *   for any other reason or returned an unexpected byte count.
 * - `HidHideStatus::InstalledActive` if the driver is installed and reports active.
 * - `HidHideStatus::InstalledInactive` if the driver is installed and reports inactive.
 */
HidHideStatus GetHidHideStatus()
{
	// Matches HidHideCLI constants from official HidHide source.
	constexpr ULONG ioControlDeviceType = 32769u;
	constexpr DWORD IOCTL_GET_ACTIVE = CTL_CODE(ioControlDeviceType, 2052, METHOD_BUFFERED, FILE_READ_DATA);

	const HANDLE device = CreateFileW(
		L"\\\\.\\HidHide",
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);

	if (device == INVALID_HANDLE_VALUE)
	{
		const DWORD err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
			return HidHideStatus::NotInstalled;
		if (err == ERROR_ACCESS_DENIED)
			return HidHideStatus::AccessDenied;
		return HidHideStatus::QueryFailed;
	}

	BOOLEAN active = FALSE;
	DWORD bytesReturned = 0;
	const BOOL ok = DeviceIoControl(
		device,
		IOCTL_GET_ACTIVE,
		nullptr,
		0,
		&active,
		sizeof(active),
		&bytesReturned,
		nullptr);
	CloseHandle(device);

	if (!ok || bytesReturned != sizeof(active))
		return HidHideStatus::QueryFailed;

	return active ? HidHideStatus::InstalledActive : HidHideStatus::InstalledInactive;
}
