#pragma once
#include "input_backend.h"
#include <Windows.h>
#include <memory>

class WgiBackend final : public IInputBackend
{
public:
	static constexpr const char* Name = "WGI";

	WgiBackend();
	~WgiBackend() override;

	void Init(HWND hwnd) override;
	void Poll() override;
	[[nodiscard]] int GetMaxSlots() const override;
	[[nodiscard]] const GamepadState& GetState(int slot) const override;
	[[nodiscard]] const char* GetName() const override;
	[[nodiscard]] const char* GetSlotDisplayName(int slot) const override;
	void GetSlotDeviceIds(int slot, uint16_t* vendorId, uint16_t* productId) const override;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
