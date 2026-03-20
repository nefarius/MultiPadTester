#include "hidhide_probe.h"

#include <Windows.h>
#include <winioctl.h>

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
