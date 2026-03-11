#pragma once
#include "gamepad_state.h"
#include <Windows.h>

class IInputBackend
{
public:
	virtual ~IInputBackend() = default;

	virtual void Init(HWND /*hwnd*/)
	{
	}

	virtual bool OnWindowMessage(UINT /*msg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) { return false; }
	virtual void Poll() = 0;
	[[nodiscard]] virtual int GetMaxSlots() const = 0;
	[[nodiscard]] virtual const GamepadState& GetState(int slot) const = 0;
	[[nodiscard]] virtual const char* GetName() const = 0;
};
