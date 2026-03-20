#include "libwdi_probe.h"

#include <Windows.h>
#include <initguid.h>
#include <SetupAPI.h>
#include <cfgmgr32.h>
#include <devpkey.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <string>
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

	std::wstring ToUpper(std::wstring s)
	{
		std::ranges::transform(s, s.begin(), [](const wchar_t ch) {
			return static_cast<wchar_t>(std::towupper(static_cast<wint_t>(ch)));
		});
		return s;
	}

	bool ReadRegistryStringProperty(
		HDEVINFO devInfoSet,
		SP_DEVINFO_DATA& devInfoData,
		DWORD prop,
		std::wstring& out)
	{
		out.clear();
		DWORD regType = 0;
		DWORD required = 0;
		if (!SetupDiGetDeviceRegistryPropertyW(
			    devInfoSet,
			    &devInfoData,
			    prop,
			    &regType,
			    nullptr,
			    0,
			    &required))
		{
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0)
				return false;
		}

		std::vector<BYTE> buf(required);
		if (!SetupDiGetDeviceRegistryPropertyW(
			    devInfoSet,
			    &devInfoData,
			    prop,
			    &regType,
			    buf.data(),
			    required,
			    &required))
			return false;
		if (regType != REG_SZ && regType != REG_EXPAND_SZ)
			return false;
		out.assign(reinterpret_cast<const wchar_t*>(buf.data()));
		return true;
	}

	bool ReadRegistryMultiSzProperty(
		HDEVINFO devInfoSet,
		SP_DEVINFO_DATA& devInfoData,
		DWORD prop,
		std::vector<std::wstring>& out)
	{
		out.clear();
		DWORD regType = 0;
		DWORD required = 0;
		if (!SetupDiGetDeviceRegistryPropertyW(
			    devInfoSet,
			    &devInfoData,
			    prop,
			    &regType,
			    nullptr,
			    0,
			    &required))
		{
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0)
				return false;
		}

		std::vector<BYTE> buf(required);
		if (!SetupDiGetDeviceRegistryPropertyW(
			    devInfoSet,
			    &devInfoData,
			    prop,
			    &regType,
			    buf.data(),
			    required,
			    &required))
			return false;
		if (regType != REG_MULTI_SZ)
			return false;

		const auto* p = reinterpret_cast<const wchar_t*>(buf.data());
		while (*p != L'\0')
		{
			out.emplace_back(p);
			p += out.back().size() + 1;
		}
		return !out.empty();
	}

	bool LooksLikeControllerName(const std::wstring& name)
	{
		if (name.empty())
			return false;
		const std::wstring upper = ToUpper(name);
		constexpr std::array controllerKeywords{
			L"CONTROLLER",
			L"GAMEPAD",
			L"JOYSTICK",
			L"XBOX",
			L"DUALSENSE",
			L"DUALSHOCK",
			L"PLAYSTATION",
			L"NINTENDO"
		};
		return std::ranges::any_of(controllerKeywords, [&](const wchar_t* keyword) {
			return upper.find(keyword) != std::wstring::npos;
		});
	}

	bool IsTargetController(
		HDEVINFO devInfoSet,
		SP_DEVINFO_DATA& devInfoData)
	{
		// First pass: hardware IDs should identify known controller vendors.
		std::vector<std::wstring> hardwareIds;
		if (ReadRegistryMultiSzProperty(devInfoSet, devInfoData, SPDRP_HARDWAREID, hardwareIds))
		{
			constexpr std::array controllerVendorTokens{
				L"VID_045E", // Microsoft / Xbox
				L"VID_054C", // Sony
				L"VID_057E", // Nintendo
				L"VID_28DE", // Valve
				L"VID_0F0D", // Hori
				L"VID_0E6F", // PDP
				L"VID_046D"  // Logitech
			};
			for (const auto& id : hardwareIds)
			{
				const std::wstring upperId = ToUpper(id);
				for (const wchar_t* token : controllerVendorTokens)
				{
					if (upperId.find(token) != std::wstring::npos)
						return true;
				}
			}
		}

		// Fallback: friendly/device description keyword heuristics.
		std::wstring name;
		if (ReadRegistryStringProperty(devInfoSet, devInfoData, SPDRP_FRIENDLYNAME, name) &&
		    LooksLikeControllerName(name))
			return true;
		if (ReadRegistryStringProperty(devInfoSet, devInfoData, SPDRP_DEVICEDESC, name) &&
		    LooksLikeControllerName(name))
			return true;

		return false;
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
		if (!IsTargetController(devInfoSet, devInfoData))
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
