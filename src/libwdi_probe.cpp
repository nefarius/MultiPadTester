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
#include <string_view>
#include <vector>

namespace
{
	std::wstring FormatProbeFailure(const std::wstring_view context, const DWORD err)
	{
		wchar_t* sysBuf = nullptr;
		const DWORD n = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			err,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			reinterpret_cast<LPWSTR>(&sysBuf),
			0,
			nullptr);
		std::wstring msg;
		msg.reserve(context.size() + 64 + (n ? n : 0));
		msg.append(context);
		msg.append(L" (Win32 error ");
		msg.append(std::to_wstring(err));
		msg.append(L")");
		if (n != 0 && sysBuf != nullptr)
		{
			msg.append(L": ");
			msg.append(sysBuf, n);
			LocalFree(sysBuf);
			while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n'))
				msg.pop_back();
		}
		else if (sysBuf != nullptr)
		{
			LocalFree(sysBuf);
		}
		return msg;
	}

	// "USBDevice" — Universal Serial Bus devices (WinUSB, Zadig, etc.)
	// https://learn.microsoft.com/en-us/windows-hardware/drivers/install/system-defined-device-setup-classes-available-to-vendors
	const GUID kGuidDevClassUsbDevice = {
		0x88bae032, 0x5a81, 0x49f0, {0xbc, 0x3d, 0xa4, 0xff, 0x13, 0x82, 0x16, 0xd6}};

	// "libusbK devices" — libusbK-class devices (e.g. per-interface installs, "Wireless Controller (Interface N)").
	const GUID kGuidDevClassLibusbKDevices = {
		0xecfb0cfd, 0x74c4, 0x4f52, {0xbb, 0xf7, 0x34, 0x34, 0x61, 0xcd, 0x72, 0xac}};

	// "libusb-win32 devices"
	const GUID kGuidDevClassLibusbWin32Devices = {
		0xeb781aaf, 0x9c70, 0x4523, {0xa5, 0xdf, 0x64, 0x2a, 0x87, 0xec, 0xa5, 0x67}};

	void TrimInPlace(std::wstring& s)
	{
		while (!s.empty() && std::iswspace(static_cast<wint_t>(s.front())))
			s.erase(s.begin());
		while (!s.empty() && std::iswspace(static_cast<wint_t>(s.back())))
			s.pop_back();
	}

	/** Each setup class has exactly one expected Provider string (case-insensitive). */
	bool ProviderMatchesExpected(std::wstring provider, const wchar_t* expectedProvider)
	{
		TrimInPlace(provider);
		return _wcsicmp(provider.c_str(), expectedProvider) == 0;
	}

	struct ZadigUsbSetupClassEntry
	{
		GUID classGuid;
		const wchar_t* expectedProvider;
	};

	static const ZadigUsbSetupClassEntry kZadigUsbSetupClasses[] = {
		{kGuidDevClassUsbDevice, L"libwdi"},
		{kGuidDevClassLibusbKDevices, L"libusbk"},
		{kGuidDevClassLibusbWin32Devices, L"libusb-win32"},
	};

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

		if (buf.size() % sizeof(wchar_t) != 0)
			return false;

		const auto* const base = reinterpret_cast<const wchar_t*>(buf.data());
		const auto* const end = base + (buf.size() / sizeof(wchar_t));

		const wchar_t* p = base;
		while (p < end)
		{
			if (*p == L'\0')
			{
				// Empty entry terminates the REG_MULTI_SZ list (final double-null).
				break;
			}

			const wchar_t* term = p;
			while (term < end && *term != L'\0')
				++term;
			if (term >= end)
			{
				out.clear();
				return false;
			}

			const size_t len = static_cast<size_t>(term - p);
			const wchar_t* const next = term + 1;
			if (next > end)
			{
				out.clear();
				return false;
			}

			out.emplace_back(p, len);
			p = next;
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

	std::vector<std::wstring> found;

	for (const auto& entry : kZadigUsbSetupClasses)
	{
		HDEVINFO devInfoSet = SetupDiGetClassDevsW(
			&entry.classGuid,
			nullptr,
			nullptr,
			DIGCF_PRESENT);
		if (devInfoSet == INVALID_HANDLE_VALUE)
		{
			result.succeeded = false;
			result.errorMessage = FormatProbeFailure(L"SetupDiGetClassDevsW failed", GetLastError());
			return result;
		}

		SP_DEVINFO_DATA devInfoData{};
		devInfoData.cbSize = sizeof(devInfoData);

		for (DWORD index = 0; SetupDiEnumDeviceInfo(devInfoSet, index, &devInfoData); ++index)
		{
			std::wstring provider;
			if (!ReadProviderString(devInfoSet, devInfoData, provider))
				continue;
			if (!ProviderMatchesExpected(provider, entry.expectedProvider))
				continue;
			if (!IsTargetController(devInfoSet, devInfoData))
				continue;

			wchar_t instanceId[MAX_DEVICE_ID_LEN]{};
			if (!SetupDiGetDeviceInstanceIdW(devInfoSet, &devInfoData, instanceId, MAX_DEVICE_ID_LEN, nullptr))
				continue;

			found.emplace_back(instanceId);
		}

		SetupDiDestroyDeviceInfoList(devInfoSet);
	}

	std::sort(found.begin(), found.end());
	found.erase(std::unique(found.begin(), found.end()), found.end());
	result.instanceIds = std::move(found);
	result.succeeded = true;
	result.errorMessage.clear();
	return result;
}
