#include "rawinput_backend.h"
#include "sony_layout.h"
#include <algorithm>
#include <ranges>
#include <utility>

// ── public interface ─────────────────────────────────────────

void RawInputBackend::Init(HWND hwnd)
{
	hwnd_ = hwnd;

	RAWINPUTDEVICE rid[2]{};
	rid[0].usUsagePage = 0x01;
	rid[0].usUsage = 0x05; // Gamepad
	rid[0].dwFlags = RIDEV_DEVNOTIFY | RIDEV_INPUTSINK;
	rid[0].hwndTarget = hwnd;
	rid[1].usUsagePage = 0x01;
	rid[1].usUsage = 0x04; // Joystick
	rid[1].dwFlags = RIDEV_DEVNOTIFY | RIDEV_INPUTSINK;
	rid[1].hwndTarget = hwnd;

	RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
	EnumerateExistingDevices();
}

bool RawInputBackend::OnWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_INPUT:
		HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
		return false;
	case WM_INPUT_DEVICE_CHANGE:
		if (wParam == GIDC_ARRIVAL)
			OnDeviceArrival(reinterpret_cast<HANDLE>(lParam));
		else if (wParam == GIDC_REMOVAL)
			OnDeviceRemoval(reinterpret_cast<HANDLE>(lParam));
		return false;
	}
	return false;
}

void RawInputBackend::Poll()
{
}

int RawInputBackend::GetMaxSlots() const { return kMaxDevices; }

const GamepadState& RawInputBackend::GetState(const int slot) const
{
	return states_[slot];
}

const char* RawInputBackend::GetName() const { return Name; }

// ── private helpers ──────────────────────────────────────────

PHIDP_PREPARSED_DATA RawInputBackend::PP(DeviceInfo& d)
{
	return reinterpret_cast<PHIDP_PREPARSED_DATA>(d.preparsedBuf.data());
}

int RawInputBackend::AllocateSlot()
{
	for (int i : std::views::iota(0, kMaxDevices))
		if (std::ranges::none_of(devices_, [i](const auto& kv) { return kv.second.slot == i; }))
			return i;
	return -1;
}

bool RawInputBackend::IsGamepadOrJoystick(HANDLE h)
{
	RID_DEVICE_INFO info{};
	info.cbSize = sizeof(info);
	UINT sz = sizeof(info);
	if (GetRawInputDeviceInfo(h, RIDI_DEVICEINFO, &info, &sz) == static_cast<UINT>(-1))
		return false;
	if (info.dwType != RIM_TYPEHID) return false;
	return info.hid.usUsagePage == 0x01 &&
		(info.hid.usUsage == 0x04 || info.hid.usUsage == 0x05);
}

bool RawInputBackend::SetupDevice(HANDLE h, DeviceInfo& d)
{
	d.handle = h;

	RID_DEVICE_INFO devInfo{};
	devInfo.cbSize = sizeof(devInfo);
	UINT devInfoSz = sizeof(devInfo);
	if (GetRawInputDeviceInfo(h, RIDI_DEVICEINFO, &devInfo, &devInfoSz) != static_cast<UINT>(-1)
	    && devInfo.dwType == RIM_TYPEHID)
	{
		d.vendorId = static_cast<uint16_t>(devInfo.hid.dwVendorId);
		d.productId = static_cast<uint16_t>(devInfo.hid.dwProductId);
	}

	UINT ppSz = 0;
	if (GetRawInputDeviceInfo(h, RIDI_PREPARSEDDATA, nullptr, &ppSz) != 0)
		return false;
	if (ppSz == 0) return false;

	d.preparsedBuf.resize(ppSz);
	if (GetRawInputDeviceInfo(h, RIDI_PREPARSEDDATA,
	                          d.preparsedBuf.data(), &ppSz) == static_cast<UINT>(-1))
		return false;

	auto pp = PP(d);
	if (HidP_GetCaps(pp, &d.caps) != HIDP_STATUS_SUCCESS)
		return false;

	if (d.caps.NumberInputValueCaps > 0)
	{
		USHORT n = d.caps.NumberInputValueCaps;
		d.valueCaps.resize(n);
		if (HidP_GetValueCaps(HidP_Input, d.valueCaps.data(), &n, pp)
			!= HIDP_STATUS_SUCCESS)
			d.valueCaps.clear();
		else
			d.valueCaps.resize(n);
	}
	return true;
}

// ── device lifecycle ─────────────────────────────────────────

void RawInputBackend::EnumerateExistingDevices()
{
	UINT n = 0;
	GetRawInputDeviceList(nullptr, &n, sizeof(RAWINPUTDEVICELIST));
	if (n == 0) return;

	std::vector<RAWINPUTDEVICELIST> list(n);
	if (GetRawInputDeviceList(list.data(), &n, sizeof(RAWINPUTDEVICELIST))
		== static_cast<UINT>(-1))
		return;

	for (UINT i = 0; i < n; ++i)
		if (list[i].dwType == RIM_TYPEHID)
			OnDeviceArrival(list[i].hDevice);
}

void RawInputBackend::OnDeviceArrival(HANDLE h)
{
	if (devices_.contains(h)) return;
	if (!IsGamepadOrJoystick(h)) return;

	DeviceInfo d;
	if (!SetupDevice(h, d)) return;

	int s = AllocateSlot();
	if (s < 0) return;

	d.slot = s;
	states_[s] = GamepadState{};
	states_[s].connected = true;
	devices_[h] = std::move(d);
}

void RawInputBackend::OnDeviceRemoval(HANDLE h)
{
	const auto it = devices_.find(h);
	if (it == devices_.end()) return;
	const int s = it->second.slot;
	if (s >= 0 && s < kMaxDevices)
		states_[s] = GamepadState{};
	devices_.erase(it);
}

// ── input parsing ────────────────────────────────────────────

void RawInputBackend::HandleRawInput(const HRAWINPUT hri)
{
	UINT sz = 0;
	GetRawInputData(hri, RID_INPUT, nullptr, &sz, sizeof(RAWINPUTHEADER));
	if (sz == 0) return;

	rawBuf_.resize(sz);
	if (GetRawInputData(hri, RID_INPUT, rawBuf_.data(), &sz,
	                    sizeof(RAWINPUTHEADER)) != sz)
		return;

	auto* raw = reinterpret_cast<RAWINPUT*>(rawBuf_.data());
	if (raw->header.dwType != RIM_TYPEHID) return;

	auto it = devices_.find(raw->header.hDevice);
	if (it == devices_.end()) return;

	auto& dev = it->second;
	if (dev.slot < 0 || dev.slot >= kMaxDevices) return;

	ParseReport(dev, raw->data.hid);
}

void RawInputBackend::ParseReport(DeviceInfo& dev, RAWHID& hid)
{
	const auto pp = PP(dev);
	auto& gs = states_[dev.slot];
	gs.connected = true;

	for (DWORD i = 0; i < hid.dwCount; ++i)
	{
		auto report = reinterpret_cast<PCHAR>(hid.bRawData + i * hid.dwSizeHid);
		ULONG rLen = hid.dwSizeHid;

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

			const USAGE uMin = vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage;
			const USAGE uMax = vc.IsRange ? vc.Range.UsageMax : vc.NotRange.Usage;

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
}

// ── mapping helpers ──────────────────────────────────────────

uint16_t RawInputBackend::MapButton(const USAGE u)
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

uint16_t RawInputBackend::MapHat(const ULONG val, const HIDP_VALUE_CAPS& vc)
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

// ── axis normalization ───────────────────────────────────────

LONG RawInputBackend::ToSigned(const ULONG raw, const HIDP_VALUE_CAPS& vc)
{
	if (vc.LogicalMin >= 0) return static_cast<LONG>(raw);
	USHORT bits = vc.BitSize;
	if (bits == 0 || bits >= 32) return static_cast<LONG>(raw);
	ULONG sign = 1UL << (bits - 1);
	if (raw & sign)
		return static_cast<LONG>(raw | (~0UL << bits));
	return static_cast<LONG>(raw);
}

float RawInputBackend::NormStick(const ULONG raw, const HIDP_VALUE_CAPS& vc)
{
	LONG lo = vc.LogicalMin, hi = vc.LogicalMax;
	if (lo >= hi) return 0.0f;
	float v = static_cast<float>(ToSigned(raw, vc));
	float mid = (lo + hi) / 2.0f;
	float half = (hi - lo) / 2.0f;
	return std::clamp((v - mid) / half, -1.0f, 1.0f);
}

float RawInputBackend::NormTrigger(const ULONG raw, const HIDP_VALUE_CAPS& vc)
{
	LONG lo = vc.LogicalMin, hi = vc.LogicalMax;
	if (lo >= hi) return 0.0f;
	float v = static_cast<float>(ToSigned(raw, vc));
	return std::clamp((v - lo) / static_cast<float>(hi - lo), 0.0f, 1.0f);
}
