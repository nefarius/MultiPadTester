#pragma once
#include "input_backend.h"

#include <cstdint>
#include <string>
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <vector>

class DInputBackend final : public IInputBackend
{
public:
	static constexpr const char* Name = "DirectInput";
	static constexpr int kMaxDevices = 16;

	~DInputBackend() override;
	void Init(HWND hwnd) override;
	void Poll() override;
	[[nodiscard]] int GetMaxSlots() const override;
	[[nodiscard]] const GamepadState& GetState(int slot) const override;
	[[nodiscard]] const char* GetName() const override;
	[[nodiscard]] const char* GetSlotDisplayName(int slot) const override;

private:
	struct DeviceInfo
	{
		GUID instanceGuid{};
		IDirectInputDevice8W* device = nullptr;
		int slot = -1;
		bool found = false;
		uint16_t vendorId = 0;
		uint16_t productId = 0;
		std::wstring productName;
	};

	HWND hwnd_ = nullptr;
	IDirectInput8W* di_ = nullptr;
	std::vector<DeviceInfo> devices_;
	GamepadState states_[kMaxDevices]{};
	int pollCounter_ = 0;

	void EnumerateDevices();
	[[nodiscard]] bool SetupDevice(const DIDEVICEINSTANCEW& inst, DeviceInfo& info);
	[[nodiscard]] int AllocateSlot();
	void ReleaseDevices();

	static BOOL CALLBACK EnumCallback(const DIDEVICEINSTANCEW* inst, VOID* ctx);
	[[nodiscard]] static float NormStick(LONG value);
	[[nodiscard]] static float NormTrigger(LONG value);
	[[nodiscard]] static uint16_t MapButton(int index);
	[[nodiscard]] static uint16_t MapPOV(DWORD pov);
	[[nodiscard]] static bool IsSonyByProductName(const std::wstring& name);
};
