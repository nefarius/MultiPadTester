#include "libwdi_probe.h"

#include <Windows.h>
#include <initguid.h>
#include <SetupAPI.h>
#include <cfgmgr32.h>
#include <devpkey.h>

#include <algorithm>
#include <cwctype>
#include <vector>

namespace
{
	// "USBDevice" — Universal Serial Bus devices (WinUSB, Zadig, etc.)
	// https://learn.microsoft.com/en-us/windows-hardware/drivers/install/system-defined-device-setup-classes-available-to-vendors
	const GUID kGuidDevClassUsbDevice = {
		0x88bae032, 0x5a81, 0x49f0, {0xbc, 0x3d, 0xa4, 0xff, 0x13, 0x82, 0x16, 0xd6}};

	void TrimInPlace(std::wstring& s)
	{
		while (!s.empty() && std::iswspace(static_cast<wint_t>(s.front())))
			s.erase(s.begin());
		while (!s.empty() && std::iswspace(static_cast<wint_t>(s.back())))
			s.pop_back();
	}

	bool IsLibwdiProvider(std::wstring provider)
	{
		TrimInPlace(provider);
		return _wcsicmp(provider.c_str(), L"libwdi") == 0;
	}

	bool ReadProviderString(
		HDEVINFO devInfoSet,
		SP_DEVINFO_DATA& devInfoData,
		std::wstring& out)
	{
		out.clear();

		DEVPROPTYPE propType = 0;
		DWORD required = 0;
		if (!SetupDiGetDevicePropertyW(
			    devInfoSet,
			    &devInfoData,
			    &DEVPKEY_Device_DriverProvider,
			    &propType,
			    nullptr,
			    0,
			    &required,
			    0))
		{
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0)
				return false;
		}

		std::vector<BYTE> buf(required);
		DWORD used = required;
		if (!SetupDiGetDevicePropertyW(
			    devInfoSet,
			    &devInfoData,
			    &DEVPKEY_Device_DriverProvider,
			    &propType,
			    buf.data(),
			    used,
			    &used,
			    0))
			return false;
		if (propType != DEVPROP_TYPE_STRING)
			return false;
		out.assign(reinterpret_cast<const wchar_t*>(buf.data()));
		return true;
	}
}

LibwdiUsbProbeResult ProbeLibwdiUsbDevices()
{
	LibwdiUsbProbeResult result;

	HDEVINFO devInfoSet = SetupDiGetClassDevsW(
		&kGuidDevClassUsbDevice,
		nullptr,
		nullptr,
		DIGCF_PRESENT);
	if (devInfoSet == INVALID_HANDLE_VALUE)
		return result;

	SP_DEVINFO_DATA devInfoData{};
	devInfoData.cbSize = sizeof(devInfoData);

	for (DWORD index = 0; SetupDiEnumDeviceInfo(devInfoSet, index, &devInfoData); ++index)
	{
		std::wstring provider;
		if (!ReadProviderString(devInfoSet, devInfoData, provider))
			continue;
		if (!IsLibwdiProvider(provider))
			continue;

		wchar_t instanceId[MAX_DEVICE_ID_LEN]{};
		if (!SetupDiGetDeviceInstanceIdW(devInfoSet, &devInfoData, instanceId, MAX_DEVICE_ID_LEN, nullptr))
			continue;

		result.instanceIds.emplace_back(instanceId);
	}

	SetupDiDestroyDeviceInfoList(devInfoSet);

	std::sort(result.instanceIds.begin(), result.instanceIds.end());
	return result;
}
