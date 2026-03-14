#pragma once
#include "input_backend.h"

#include <cstdint>
#include <winternl.h>
#include <hidusage.h>
#include <hidpi.h>
#include <SetupAPI.h>
#include <hidsdi.h>
#include <memory>
#include <vector>
#include <string>

class HidApiBackend final : public IInputBackend
{
public:
	static constexpr const char* Name = "HIDAPI";
	static constexpr int kMaxDevices = 16;

	~HidApiBackend() override;
	/**
	 * Poll HID devices, process incoming reports, and update internal gamepad states.
	 */
	 
	/**
	 * Return the maximum number of device slots supported by this backend.
	 * @returns The maximum number of slots.
	 */
	 
	/**
	 * Retrieve the current gamepad state for a given slot.
	 * @param slot Index of the slot to query; valid range is 0 .. GetMaxSlots()-1.
	 * @returns Reference to the GamepadState for the specified slot.
	 */
	 
	/**
	 * Get the backend name identifier.
	 * @returns Null-terminated string identifying this backend.
	 */
	 
	/**
	 * Get a human-readable display name for the device assigned to a slot.
	 * @param slot Index of the slot to query; valid range is 0 .. GetMaxSlots()-1.
	 * @returns A null-terminated display name for the slot, or nullptr when no name is available.
	 */
	 
	/**
	 * Per-device runtime information and resources used to manage a HID device.
	 *
	 * Contains device path, OS handle and overlapped I/O state, read buffer,
	 * HID preparsed data and capability descriptors, assigned slot index, flags
	 * tracking discovery and read state, and the device's vendor/product IDs.
	 */
	void Poll() override;
	[[nodiscard]] int GetMaxSlots() const override;
	[[nodiscard]] const GamepadState& GetState(int slot) const override;
	[[nodiscard]] const char* GetName() const override;
	[[nodiscard]] const char* GetSlotDisplayName(int slot) const override;

private:
	struct DeviceInfo
	{
		std::wstring path;
		HANDLE handle = INVALID_HANDLE_VALUE;
		OVERLAPPED overlapped{};
		std::vector<BYTE> readBuf;
		PHIDP_PREPARSED_DATA preparsed = nullptr;
		HIDP_CAPS caps{};
		std::vector<HIDP_VALUE_CAPS> valueCaps;
		int slot = -1;
		bool found = false;
		bool readPending = false;
		uint16_t vendorId = 0;
		uint16_t productId = 0;
	};

	std::vector<std::unique_ptr<DeviceInfo>> devices_;
	GamepadState states_[kMaxDevices]{};
	int pollCounter_ = 0;
	std::vector<USAGE> usageBuf_;
	bool firstPoll_ = true;

	void EnumerateDevices();
	[[nodiscard]] bool OpenAndSetup(const wchar_t* path, DeviceInfo& info);
	void StartRead(DeviceInfo& dev);
	void ParseReport(DeviceInfo& dev, DWORD bytesRead);
	void CloseDevice(DeviceInfo& dev);
	[[nodiscard]] int AllocateSlot();

	[[nodiscard]] static bool IsGamepadOrJoystick(const HIDP_CAPS& caps);
	[[nodiscard]] static uint16_t MapButton(USAGE u);
	[[nodiscard]] static uint16_t MapHat(ULONG val, const HIDP_VALUE_CAPS& vc);
	[[nodiscard]] static LONG ToSigned(ULONG raw, const HIDP_VALUE_CAPS& vc);
	[[nodiscard]] static float NormStick(ULONG raw, const HIDP_VALUE_CAPS& vc);
	[[nodiscard]] static float NormTrigger(ULONG raw, const HIDP_VALUE_CAPS& vc);
};
