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
