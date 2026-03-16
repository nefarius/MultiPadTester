#include "gameinput_backend.h"
#include <GameInput.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <string>
#include <vector>

namespace
{
	uint16_t MapGamepadButtons(uint64_t giButtons)
	{
		uint16_t b = 0;
		using enum Button;
		// GameInputGamepadButtons (bit flags)
		if (giButtons & GameInputGamepadMenu) b |= std::to_underlying(Start);
		if (giButtons & GameInputGamepadView) b |= std::to_underlying(Back);
		if (giButtons & GameInputGamepadA) b |= std::to_underlying(A);
		if (giButtons & GameInputGamepadB) b |= std::to_underlying(B);
		if (giButtons & GameInputGamepadX) b |= std::to_underlying(X);
		if (giButtons & GameInputGamepadY) b |= std::to_underlying(Y);
		if (giButtons & GameInputGamepadDPadUp) b |= std::to_underlying(DPadUp);
		if (giButtons & GameInputGamepadDPadDown) b |= std::to_underlying(DPadDown);
		if (giButtons & GameInputGamepadDPadLeft) b |= std::to_underlying(DPadLeft);
		if (giButtons & GameInputGamepadDPadRight) b |= std::to_underlying(DPadRight);
		if (giButtons & GameInputGamepadLeftShoulder) b |= std::to_underlying(LeftBumper);
		if (giButtons & GameInputGamepadRightShoulder) b |= std::to_underlying(RightBumper);
		if (giButtons & GameInputGamepadLeftThumbstick) b |= std::to_underlying(LeftThumb);
		if (giButtons & GameInputGamepadRightThumbstick) b |= std::to_underlying(RightThumb);
		return b;
	}
}

struct GameInputBackend::Impl
{
	mutable std::mutex mutex;
	IGameInput* input = nullptr;
	IGameInputDispatcher* dispatcher = nullptr;
	GameInputCallbackToken callbackToken = 0;

	struct SlotDevice
	{
		IGameInputDevice* device = nullptr;
		int slot = -1;
		std::string displayName;
	};
	std::vector<SlotDevice> devices;
	std::array<GamepadState, GameInputBackend::kMaxDevices> states{};
	std::array<std::string, GameInputBackend::kMaxDevices> slotDisplayNames;

	static void CALLBACK DeviceCallback(
		GameInputCallbackToken,
		void* context,
		IGameInputDevice* device,
		uint64_t,
		GameInputDeviceStatus currentStatus,
		GameInputDeviceStatus)
	{
		auto* self = static_cast<Impl*>(context);
		std::lock_guard lock(self->mutex);
		auto it = std::ranges::find_if(self->devices,
			[device](const SlotDevice& d) { return d.device == device; });
		if (currentStatus & GameInputDeviceConnected)
		{
			if (it != self->devices.end())
				return;
			int slot = AllocateSlot(self);
			if (slot < 0)
				return;
			device->AddRef();
			std::string displayNameStr = "Controller " + std::to_string(slot);
			if (const GameInputDeviceInfo* info = device->GetDeviceInfo())
			{
				// displayName is GameInputString const* (UTF-8); often null per documentation
				if (info->displayName && info->displayName->data && info->displayName->sizeInBytes > 0)
				{
					displayNameStr.assign(info->displayName->data, info->displayName->sizeInBytes);
					while (!displayNameStr.empty() && displayNameStr.back() == '\0')
						displayNameStr.pop_back();
				}
			}
			self->devices.push_back({ device, slot, std::move(displayNameStr) });
			self->slotDisplayNames[static_cast<size_t>(slot)] = self->devices.back().displayName;
		}
		else
		{
			if (it != self->devices.end())
			{
				int slot = it->slot;
				it->device->Release();
				self->devices.erase(it);
				self->states[static_cast<size_t>(slot)] = GamepadState{};
				self->slotDisplayNames[static_cast<size_t>(slot)].clear();
			}
		}
	}

	static int AllocateSlot(const Impl* self)
	{
		for (int i = 0; i < GameInputBackend::kMaxDevices; ++i)
			if (std::none_of(self->devices.begin(), self->devices.end(),
				[i](const SlotDevice& d) { return d.slot == i; }))
				return i;
		return -1;
	}
};

GameInputBackend::GameInputBackend() : impl_(std::make_unique<Impl>()) {}

GameInputBackend::~GameInputBackend()
{
	if (impl_)
	{
		std::lock_guard lock(impl_->mutex);
		if (impl_->input && impl_->callbackToken)
			impl_->input->UnregisterCallback(impl_->callbackToken, 0);
		for (auto& d : impl_->devices)
			if (d.device)
				d.device->Release();
		impl_->devices.clear();
		if (impl_->dispatcher)
		{
			impl_->dispatcher->Release();
			impl_->dispatcher = nullptr;
		}
		if (impl_->input)
		{
			impl_->input->Release();
			impl_->input = nullptr;
		}
	}
}

void GameInputBackend::Init(HWND)
{
	std::lock_guard lock(impl_->mutex);
	if (impl_->input)
		return;
	if (FAILED(GameInputCreate(&impl_->input)))
		return;
	if (FAILED(impl_->input->CreateDispatcher(&impl_->dispatcher)))
	{
		impl_->input->Release();
		impl_->input = nullptr;
		return;
	}
	impl_->input->RegisterDeviceCallback(
		nullptr,
		GameInputKindController,
		GameInputDeviceAnyStatus,
		GameInputBlockingEnumeration,
		impl_.get(),
		&Impl::DeviceCallback,
		&impl_->callbackToken);
}

void GameInputBackend::Poll()
{
	if (!impl_->dispatcher)
		return;
	// Do not hold mutex across Dispatch(0) — device callback takes the same mutex.
	impl_->dispatcher->Dispatch(0);

	std::lock_guard lock(impl_->mutex);
	for (auto& slotDev : impl_->devices)
	{
		int slot = slotDev.slot;
		IGameInputReading* reading = nullptr;
		HRESULT hr = impl_->input->GetCurrentReading(GameInputKindGamepad, slotDev.device, &reading);
		if (SUCCEEDED(hr) && reading)
		{
			GameInputGamepadState gistate{};
			if (SUCCEEDED(reading->GetGamepadState(&gistate)))
			{
				auto& gs = impl_->states[static_cast<size_t>(slot)];
				gs.connected = true;
				gs.leftStickX = gistate.leftThumbstickX;
				gs.leftStickY = gistate.leftThumbstickY;
				gs.rightStickX = gistate.rightThumbstickX;
				gs.rightStickY = gistate.rightThumbstickY;
				gs.leftTrigger = gistate.leftTrigger;
				gs.rightTrigger = gistate.rightTrigger;
				gs.buttons = MapGamepadButtons(gistate.buttons);
			}
			reading->Release();
			continue;
		}
		reading = nullptr;
		hr = impl_->input->GetCurrentReading(GameInputKindController, slotDev.device, &reading);
		if (SUCCEEDED(hr) && reading)
		{
			constexpr int kMaxAxes = 16;
			constexpr int kMaxButtons = 32;
			constexpr int kMaxSwitches = 8;
			float axes[kMaxAxes]{};
			bool buttons[kMaxButtons]{};
			GameInputSwitchPosition switches[kMaxSwitches]{};
			reading->GetControllerAxisState(kMaxAxes, axes);
			reading->GetControllerButtonState(kMaxButtons, buttons);
			reading->GetControllerSwitchState(kMaxSwitches, switches);
			auto& gs = impl_->states[static_cast<size_t>(slot)];
			gs.connected = true;
			gs.leftStickX = (kMaxAxes > 0) ? axes[0] : 0.0f;
			gs.leftStickY = (kMaxAxes > 1) ? axes[1] : 0.0f;
			gs.rightStickX = (kMaxAxes > 2) ? axes[2] : 0.0f;
			gs.rightStickY = (kMaxAxes > 3) ? axes[3] : 0.0f;
			gs.leftTrigger = (kMaxAxes > 4) ? (axes[4] * 0.5f + 0.5f) : 0.0f;
			gs.rightTrigger = (kMaxAxes > 5) ? (axes[5] * 0.5f + 0.5f) : 0.0f;
			uint16_t b = 0;
			using enum Button;
			if (kMaxButtons > 0 && buttons[0]) b |= std::to_underlying(A);
			if (kMaxButtons > 1 && buttons[1]) b |= std::to_underlying(B);
			if (kMaxButtons > 2 && buttons[2]) b |= std::to_underlying(X);
			if (kMaxButtons > 3 && buttons[3]) b |= std::to_underlying(Y);
			if (kMaxSwitches > 0)
			{
				GameInputSwitchPosition hat = switches[0];
				if (hat == GameInputSwitchUp) b |= std::to_underlying(DPadUp);
				else if (hat == GameInputSwitchDown) b |= std::to_underlying(DPadDown);
				else if (hat == GameInputSwitchLeft) b |= std::to_underlying(DPadLeft);
				else if (hat == GameInputSwitchRight) b |= std::to_underlying(DPadRight);
			}
			gs.buttons = b;
			reading->Release();
		}
		else
		{
			impl_->states[static_cast<size_t>(slot)].connected = false;
		}
	}
}

int GameInputBackend::GetMaxSlots() const { return kMaxDevices; }

const GamepadState& GameInputBackend::GetState(int slot) const
{
	if (slot < 0 || slot >= kMaxDevices)
	{
		static const GamepadState empty{};
		return empty;
	}
	return impl_->states[static_cast<size_t>(slot)];
}

const char* GameInputBackend::GetName() const { return Name; }

const char* GameInputBackend::GetSlotDisplayName(int slot) const
{
	if (slot < 0 || slot >= kMaxDevices)
		return nullptr;
	const std::string& s = impl_->slotDisplayNames[static_cast<size_t>(slot)];
	return s.empty() ? nullptr : s.c_str();
}
