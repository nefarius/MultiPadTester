#pragma once
#include "input_backend.h"

#include <cstdint>
#include <winternl.h>
#include <hidusage.h>
#include <hidpi.h>
#include <vector>
#include <unordered_map>

class RawInputBackend final : public IInputBackend
{
public:
	static constexpr const char* Name = "RawInput";
	static constexpr int kMaxDevices = 16;

	void Init(HWND hwnd) override;
	bool OnWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam) override;
	/**
	 * Poll input backend and update internal per-slot gamepad states.
	 *
	 * This performs a single polling pass to process pending input events and refresh
	 * stored GamepadState for each active slot.
	 */
	 
	/**
	 * Return the maximum number of device slots supported by this backend.
	 *
	 * @returns The maximum number of slots available for gamepad devices.
	 */
	 
	/**
	 * Return the current GamepadState for a given slot.
	 *
	 * @param slot Index of the slot to query (0-based).
	 * @returns Reference to the stored GamepadState for the specified slot.
	 */
	 
	/**
	 * Return the backend's human-readable name.
	 *
	 * @returns A null-terminated string containing the backend name.
	 */
	 
	/**
	 * Return the display name for a specific slot.
	 *
	 * @param slot Index of the slot to query (0-based).
	 * @returns A null-terminated string containing the display name for the slot, or an empty string if none.
	 */
	 
	/**
	 * Information tracked for a single raw input device.
	 *
	 * Contains the device handle, cached preparsed HID data and capability structures,
	 * the assigned slot index, and vendor/product identifiers.
	 */
	void Poll() override;
	[[nodiscard]] int GetMaxSlots() const override;
	[[nodiscard]] const GamepadState& GetState(int slot) const override;
	[[nodiscard]] const char* GetName() const override;
	[[nodiscard]] const char* GetSlotDisplayName(int slot) const override;

private:
	struct DeviceInfo
	{
		HANDLE handle = nullptr;
		std::vector<BYTE> preparsedBuf;
		HIDP_CAPS caps{};
		std::vector<HIDP_VALUE_CAPS> valueCaps;
		int slot = -1;
		uint16_t vendorId = 0;
		uint16_t productId = 0;
	};

	HWND hwnd_ = nullptr;
	std::unordered_map<HANDLE, DeviceInfo> devices_;
	GamepadState states_[kMaxDevices]{};
	std::vector<BYTE> rawBuf_;
	std::vector<USAGE> usageBuf_;

	[[nodiscard]] static PHIDP_PREPARSED_DATA PP(DeviceInfo& d);
	[[nodiscard]] int AllocateSlot();
	[[nodiscard]] static bool SetupDevice(HANDLE h, DeviceInfo& d);

	void EnumerateExistingDevices();
	void OnDeviceArrival(HANDLE h);
	void OnDeviceRemoval(HANDLE h);

	void HandleRawInput(HRAWINPUT hri);
	void ParseReport(DeviceInfo& dev, RAWHID& hid);

	[[nodiscard]] static bool IsGamepadOrJoystick(HANDLE h);
	[[nodiscard]] static uint16_t MapButton(USAGE u);
	[[nodiscard]] static uint16_t MapHat(ULONG val, const HIDP_VALUE_CAPS& vc);
	[[nodiscard]] static LONG ToSigned(ULONG raw, const HIDP_VALUE_CAPS& vc);
	[[nodiscard]] static float NormStick(ULONG raw, const HIDP_VALUE_CAPS& vc);
	[[nodiscard]] static float NormTrigger(ULONG raw, const HIDP_VALUE_CAPS& vc);
};
