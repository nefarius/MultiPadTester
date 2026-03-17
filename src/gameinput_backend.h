#pragma once
#include "input_backend.h"

#include <memory>

/**
 * Backend using Microsoft GameInput API (GDK).
 * Requires Microsoft.GameInput NuGet (GameInput.h, GameInput.lib).
 * Implementation is in .cpp; this header does not include GameInput.h.
 */
class GameInputBackend final : public IInputBackend
{
public:
	static constexpr const char* Name = "GameInput";
	static constexpr int kMaxDevices = 16;

	/** Returns true if the GameInput redistributable is available (DLL present and working). */
	static bool IsAvailable();

	GameInputBackend();
	~GameInputBackend() override;

	void Init(HWND hwnd) override;
	void Poll() override;
	[[nodiscard]] int GetMaxSlots() const override;
	[[nodiscard]] const GamepadState& GetState(int slot) const override;
	[[nodiscard]] const char* GetName() const override;
	[[nodiscard]] const char* GetSlotDisplayName(int slot) const override;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
