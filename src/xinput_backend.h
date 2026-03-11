#pragma once
#include "input_backend.h"
#include <Windows.h>
#include <Xinput.h>
#include <chrono>

using XInputGetStateEx_t = DWORD(WINAPI*)(DWORD dwUserIndex, XINPUT_STATE* pState);

class XInputBackend final : public IInputBackend
{
public:
	static constexpr const char* Name = "XInput";

	void Init(HWND hwnd) override;
	void Poll() override;
	[[nodiscard]] int GetMaxSlots() const override;
	[[nodiscard]] const GamepadState& GetState(int slot) const override;
	[[nodiscard]] const char* GetName() const override;

private:
	static constexpr auto kReprobeInterval = std::chrono::milliseconds(1500);

	HMODULE xinputModule_ = nullptr;
	XInputGetStateEx_t getStateEx_ = nullptr;

	GamepadState states_[XUSER_MAX_COUNT]{};
	std::chrono::steady_clock::time_point lastProbe_[XUSER_MAX_COUNT]{};

	[[nodiscard]] static float NormalizeAxis(SHORT value, SHORT deadzone);
};
