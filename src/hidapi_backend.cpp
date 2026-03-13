#include "hidapi_backend.h"
#include "sony_layout.h"
#include <algorithm>
#include <ranges>
#include <utility>

// ── lifecycle ────────────────────────────────────────────────

HidApiBackend::~HidApiBackend()
{
	for (auto& d : devices_)
		CloseDevice(*d);
	devices_.clear();
}

void HidApiBackend::CloseDevice(DeviceInfo& dev)
{
	if (dev.handle != INVALID_HANDLE_VALUE)
	{
		if (dev.readPending)
			CancelIo(dev.handle);
		CloseHandle(dev.handle);
		dev.handle = INVALID_HANDLE_VALUE;
	}
	if (dev.overlapped.hEvent)
	{
		CloseHandle(dev.overlapped.hEvent);
		dev.overlapped.hEvent = nullptr;
	}
	dev.readPending = false;
	if (dev.preparsed)
	{
		HidD_FreePreparsedData(dev.preparsed);
		dev.preparsed = nullptr;
	}
	dev.readBuf.clear();
	if (dev.slot >= 0 && dev.slot < kMaxDevices)
		states_[dev.slot] = GamepadState{};
}

// ── public interface ─────────────────────────────────────────

void HidApiBackend::Poll()
{
	if (firstPoll_)
	{
		firstPoll_ = false;
		EnumerateDevices();
	}

	if (++pollCounter_ >= 120)
	{
		pollCounter_ = 0;
		EnumerateDevices();
	}

	for (auto it = devices_.begin(); it != devices_.end();)
	{
		auto& dev = **it;
		if (dev.handle == INVALID_HANDLE_VALUE)
		{
			++it;
			continue;
		}

		if (!dev.readPending)
		{
			StartRead(dev);
			++it;
			continue;
		}

		DWORD bytesRead = 0;
		BOOL ok = GetOverlappedResult(dev.handle, &dev.overlapped,
		                              &bytesRead, FALSE);
		if (ok)
		{
			dev.readPending = false;
			ParseReport(dev, bytesRead);
			StartRead(dev);
			++it;
		}
		else
		{
			DWORD err = GetLastError();
			if (err == ERROR_IO_INCOMPLETE)
			{
				++it;
			}
			else
			{
				dev.readPending = false;
				CloseDevice(dev);
				it = devices_.erase(it);
			}
		}
	}
}

int HidApiBackend::GetMaxSlots() const { return kMaxDevices; }

const GamepadState& HidApiBackend::GetState(const int slot) const
{
	return states_[slot];
}

const char* HidApiBackend::GetName() const { return Name; }

// ── device enumeration ───────────────────────────────────────

void HidApiBackend::EnumerateDevices()
{
	for (auto& d : devices_) d->found = false;

	GUID hidGuid;
	HidD_GetHidGuid(&hidGuid);

	HDEVINFO devInfoSet = SetupDiGetClassDevsW(
		&hidGuid, nullptr, nullptr,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (devInfoSet == INVALID_HANDLE_VALUE)
		return;

	SP_DEVICE_INTERFACE_DATA ifData{};
	ifData.cbSize = sizeof(ifData);

	for (DWORD idx = 0;
	     SetupDiEnumDeviceInterfaces(devInfoSet, nullptr, &hidGuid,
	                                 idx, &ifData);
	     ++idx)
	{
		DWORD requiredSize = 0;
		SetupDiGetDeviceInterfaceDetailW(devInfoSet, &ifData,
		                                 nullptr, 0, &requiredSize, nullptr);
		if (requiredSize == 0) continue;

		std::vector<BYTE> detailBuf(requiredSize);
		auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
			detailBuf.data());
		detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

		if (!SetupDiGetDeviceInterfaceDetailW(devInfoSet, &ifData,
		                                      detail, requiredSize, nullptr, nullptr))
			continue;

		const wchar_t* path = detail->DevicePath;

		bool alreadyKnown = false;
		for (auto& d : devices_)
		{
			if (_wcsicmp(d->path.c_str(), path) == 0)
			{
				d->found = true;
				alreadyKnown = true;
				break;
			}
		}
		if (alreadyKnown) continue;

		auto info = std::make_unique<DeviceInfo>();
		if (OpenAndSetup(path, *info))
		{
			info->found = true;
			devices_.push_back(std::move(info));
		}
	}

	SetupDiDestroyDeviceInfoList(devInfoSet);

	std::erase_if(devices_, [this](const std::unique_ptr<DeviceInfo>& d)
	{
		if (d->found) return false;
		CloseDevice(*d);
		return true;
	});
}

bool HidApiBackend::OpenAndSetup(const wchar_t* path, DeviceInfo& info)
{
	info.path = path;

	HANDLE h = CreateFileW(path,
	                       GENERIC_READ | GENERIC_WRITE,
	                       FILE_SHARE_READ | FILE_SHARE_WRITE,
	                       nullptr, OPEN_EXISTING,
	                       FILE_FLAG_OVERLAPPED, nullptr);

	if (h == INVALID_HANDLE_VALUE)
	{
		h = CreateFileW(path,
		                GENERIC_READ,
		                FILE_SHARE_READ | FILE_SHARE_WRITE,
		                nullptr, OPEN_EXISTING,
		                FILE_FLAG_OVERLAPPED, nullptr);
	}
	if (h == INVALID_HANDLE_VALUE)
		return false;

	info.handle = h;

	HIDD_ATTRIBUTES attrs{};
	attrs.Size = sizeof(attrs);
	if (HidD_GetAttributes(h, &attrs))
	{
		info.vendorId = attrs.VendorID;
		info.productId = attrs.ProductID;
	}

	PHIDP_PREPARSED_DATA pp = nullptr;
	if (!HidD_GetPreparsedData(h, &pp))
	{
		CloseHandle(h);
		info.handle = INVALID_HANDLE_VALUE;
		return false;
	}
	info.preparsed = pp;

	if (HidP_GetCaps(pp, &info.caps) != HIDP_STATUS_SUCCESS)
	{
		HidD_FreePreparsedData(pp);
		info.preparsed = nullptr;
		CloseHandle(h);
		info.handle = INVALID_HANDLE_VALUE;
		return false;
	}

	if (!IsGamepadOrJoystick(info.caps))
	{
		HidD_FreePreparsedData(pp);
		info.preparsed = nullptr;
		CloseHandle(h);
		info.handle = INVALID_HANDLE_VALUE;
		return false;
	}

	if (info.caps.NumberInputValueCaps > 0)
	{
		USHORT n = info.caps.NumberInputValueCaps;
		info.valueCaps.resize(n);
		if (HidP_GetValueCaps(HidP_Input, info.valueCaps.data(), &n, pp)
			!= HIDP_STATUS_SUCCESS)
			info.valueCaps.clear();
		else
			info.valueCaps.resize(n);
	}

	int s = AllocateSlot();
	if (s < 0)
	{
		HidD_FreePreparsedData(pp);
		info.preparsed = nullptr;
		CloseHandle(h);
		info.handle = INVALID_HANDLE_VALUE;
		return false;
	}

	info.slot = s;
	states_[s] = GamepadState{};
	states_[s].connected = true;

	info.readBuf.resize(info.caps.InputReportByteLength);

	info.overlapped = {};
	info.overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

	StartRead(info);
	return true;
}

// ── async I/O ────────────────────────────────────────────────

void HidApiBackend::StartRead(DeviceInfo& dev)
{
	if (dev.handle == INVALID_HANDLE_VALUE) return;

	constexpr int kMaxSyncReads = 16;
	for (int attempt = 0; attempt < kMaxSyncReads; ++attempt)
	{
		ResetEvent(dev.overlapped.hEvent);
		DWORD bytesRead = 0;
		BOOL ok = ReadFile(dev.handle, dev.readBuf.data(),
		                   static_cast<DWORD>(dev.readBuf.size()),
		                   &bytesRead, &dev.overlapped);
		if (ok)
		{
			ParseReport(dev, bytesRead);
			continue;
		}

		DWORD err = GetLastError();
		if (err == ERROR_IO_PENDING)
		{
			dev.readPending = true;
		}
		else
		{
			dev.readPending = false;
		}
		return;
	}

	dev.readPending = false;
}

// ── report parsing ───────────────────────────────────────────

void HidApiBackend::ParseReport(DeviceInfo& dev, DWORD bytesRead)
{
	if (bytesRead == 0) return;
	if (dev.slot < 0 || dev.slot >= kMaxDevices) return;

	PHIDP_PREPARSED_DATA pp = dev.preparsed;
	if (!pp) return;

	auto& gs = states_[dev.slot];
	gs.connected = true;

	auto report = reinterpret_cast<PCHAR>(dev.readBuf.data());
	ULONG rLen = bytesRead;

	uint16_t btns = 0;
	const bool sony = IsSonyGamepad(dev.vendorId, dev.productId);

	ULONG maxU = HidP_MaxUsageListLength(HidP_Input, 0x09, pp);
	if (maxU > 0)
	{
		usageBuf_.resize(maxU);
		ULONG cnt = maxU;
		if (HidP_GetUsages(HidP_Input, 0x09, 0, usageBuf_.data(),
		                   &cnt, pp, report, rLen) == HIDP_STATUS_SUCCESS)
		{
			for (ULONG j = 0; j < cnt; ++j)
				btns |= sony ? MapSonyHidButton(usageBuf_[j]) : MapButton(usageBuf_[j]);
		}
	}

	for (const auto& vc : dev.valueCaps)
	{
		if (vc.UsagePage != 0x01) continue;

		USAGE uMin = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
		USAGE uMax = vc.IsRange ? vc.Range.UsageMax : vc.NotRange.Usage;

		for (USAGE u = uMin; u <= uMax; ++u)
		{
			ULONG val = 0;
			if (HidP_GetUsageValue(HidP_Input, 0x01, 0, u,
			                       &val, pp, report, rLen) != HIDP_STATUS_SUCCESS)
				continue;

			if (sony)
			{
				switch (u)
				{
				case 0x30: gs.leftStickX = NormStick(val, vc);
					break;
				case 0x31: gs.leftStickY = -NormStick(val, vc);
					break;
				case 0x32: gs.rightStickX = NormStick(val, vc);
					break;
				case 0x33: gs.leftTrigger = NormTrigger(val, vc);
					break;
				case 0x34: gs.rightTrigger = NormTrigger(val, vc);
					break;
				case 0x35: gs.rightStickY = -NormStick(val, vc);
					break;
				case 0x39: btns |= MapHat(val, vc);
					break;
				}
			}
			else
			{
				switch (u)
				{
				case 0x30: gs.leftStickX = NormStick(val, vc);
					break;
				case 0x31: gs.leftStickY = -NormStick(val, vc);
					break;
				case 0x32: gs.leftTrigger = NormTrigger(val, vc);
					break;
				case 0x33: gs.rightStickX = NormStick(val, vc);
					break;
				case 0x34: gs.rightStickY = -NormStick(val, vc);
					break;
				case 0x35: gs.rightTrigger = NormTrigger(val, vc);
					break;
				case 0x39: btns |= MapHat(val, vc);
					break;
				}
			}
		}
	}

	gs.buttons = btns;
}

// ── slot management ──────────────────────────────────────────

int HidApiBackend::AllocateSlot()
{
	for (int i : std::views::iota(0, kMaxDevices))
		if (std::ranges::none_of(devices_, [i](const auto& d) { return d->slot == i; }))
			return i;
	return -1;
}

// ── mapping helpers ──────────────────────────────────────────

bool HidApiBackend::IsGamepadOrJoystick(const HIDP_CAPS& caps)
{
	return caps.UsagePage == 0x01 &&
		(caps.Usage == 0x04 || caps.Usage == 0x05);
}

uint16_t HidApiBackend::MapButton(USAGE u)
{
	using enum Button;
	switch (u)
	{
	case 1: return std::to_underlying(A);
	case 2: return std::to_underlying(B);
	case 3: return std::to_underlying(X);
	case 4: return std::to_underlying(Y);
	case 5: return std::to_underlying(LeftBumper);
	case 6: return std::to_underlying(RightBumper);
	case 7: return std::to_underlying(Back);
	case 8: return std::to_underlying(Start);
	case 9: return std::to_underlying(LeftThumb);
	case 10: return std::to_underlying(RightThumb);
	default: return 0;
	}
}

uint16_t HidApiBackend::MapHat(const ULONG val, const HIDP_VALUE_CAPS& vc)
{
	using enum Button;
	LONG v = static_cast<LONG>(val);
	if (v < vc.LogicalMin || v > vc.LogicalMax) return 0;

	int dir = v - vc.LogicalMin;
	int range = vc.LogicalMax - vc.LogicalMin + 1;

	constexpr uint16_t U = std::to_underlying(DPadUp);
	constexpr uint16_t D = std::to_underlying(DPadDown);
	constexpr uint16_t L = std::to_underlying(DPadLeft);
	constexpr uint16_t R = std::to_underlying(DPadRight);

	if (range == 8)
	{
		constexpr uint16_t t[] = {U, U | R, R, D | R, D, D | L, L, U | L};
		return (dir >= 0 && dir < 8) ? t[dir] : 0;
	}
	if (range == 4)
	{
		constexpr uint16_t t[] = {U, R, D, L};
		return (dir >= 0 && dir < 4) ? t[dir] : 0;
	}
	return 0;
}

LONG HidApiBackend::ToSigned(const ULONG raw, const HIDP_VALUE_CAPS& vc)
{
	if (vc.LogicalMin >= 0) return static_cast<LONG>(raw);
	USHORT bits = vc.BitSize;
	if (bits == 0 || bits >= 32) return static_cast<LONG>(raw);
	ULONG sign = 1UL << (bits - 1);
	if (raw & sign)
		return static_cast<LONG>(raw | (~0UL << bits));
	return static_cast<LONG>(raw);
}

float HidApiBackend::NormStick(const ULONG raw, const HIDP_VALUE_CAPS& vc)
{
	LONG lo = vc.LogicalMin, hi = vc.LogicalMax;
	if (lo >= hi) return 0.0f;
	float v = static_cast<float>(ToSigned(raw, vc));
	float mid = (lo + hi) / 2.0f;
	float half = (hi - lo) / 2.0f;
	return std::clamp((v - mid) / half, -1.0f, 1.0f);
}

float HidApiBackend::NormTrigger(const ULONG raw, const HIDP_VALUE_CAPS& vc)
{
	const LONG lo = vc.LogicalMin;
	const LONG hi = vc.LogicalMax;
	if (lo >= hi) return 0.0f;
	float v = static_cast<float>(ToSigned(raw, vc));
	return std::clamp((v - lo) / static_cast<float>(hi - lo), 0.0f, 1.0f);
}
