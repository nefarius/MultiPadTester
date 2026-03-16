#include "gameinput_backend.h"
#include "sony_layout.h"
#include "usb_names.h"
#include <GameInput.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <string>
#include <utility>
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
		uint16_t vendorId = 0;
		uint16_t productId = 0;
		// Axis mapping: indices into GetControllerAxisState() array for [leftX, leftY, rightX, rightY, leftTrigger, rightTrigger]
		std::array<int, 6> axisIndex = { -1, -1, -1, -1, -1, -1 };
		std::array<float, 6> axisRest = { 0.5f, 0.5f, 0.5f, 0.5f, 0.f, 0.f };
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
		std::scoped_lock lock(self->mutex);
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
			uint16_t vid = 0, pid = 0;
			std::string displayNameStr = "Controller " + std::to_string(slot);
			if (const GameInputDeviceInfo* info = device->GetDeviceInfo())
			{
				vid = info->vendorId;
				pid = info->productId;
				// displayName is GameInputString const* (UTF-8); often null per documentation
				if (info->displayName && info->displayName->data && info->displayName->sizeInBytes > 0)
				{
					displayNameStr.assign(info->displayName->data, info->displayName->sizeInBytes);
					while (!displayNameStr.empty() && displayNameStr.back() == '\0')
						displayNameStr.pop_back();
				}
				// If still generic, use VID/PID so Sony/Xbox names and UI layout (texture) are correct
				if (displayNameStr.empty() || displayNameStr.find("Controller ") == 0)
				{
					if (const char* friendly = GetFriendlyName(vid, pid))
						displayNameStr = friendly;
				}
			}
			const bool sony = IsSonyGamepad(vid, pid);
			std::array<int, 6> axisIndex = { -1, -1, -1, -1, -1, -1 };
			std::array<float, 6> axisRest = { 0.5f, 0.5f, 0.5f, 0.5f, 0.f, 0.f };
			if (const GameInputDeviceInfo* info = device->GetDeviceInfo(); info && info->controllerAxisInfo)
			{
				// Map controller axes by label (triggers) and legacy DInput index (sticks). DInput order: 0=lX, 1=lY, 2=lZ, 3=lRx, 4=lRy, 5=lRz.
				// Non-Sony: 0,1=left stick; 3,4=right stick; 2,5=triggers. Sony: 0,1=left stick; 2,5=right stick; 3,4=triggers.
				for (uint32_t i = 0; i < info->controllerAxisCount; ++i)
				{
					const auto& ax = info->controllerAxisInfo[i];
					float rest = ax.hasRestValue ? ax.restValue : 0.5f;
					int sem = -1;
					if (ax.label == GameInputLabelXboxLeftTrigger)
						sem = 4;
					else if (ax.label == GameInputLabelXboxRightTrigger)
						sem = 5;
					else
					{
						switch (ax.legacyDInputIndex)
						{
						case 0: sem = 0; break; // leftStickX
						case 1: sem = 1; break; // leftStickY
						case 2: sem = sony ? 2 : 4; break; // Sony: rightStickX; else leftTrigger
						case 3: sem = sony ? 4 : 2; break; // Sony: leftTrigger; else rightStickX
						case 4: sem = sony ? 5 : 3; break; // Sony: rightTrigger; else rightStickY
						case 5: sem = sony ? 3 : 5; break; // Sony: rightStickY; else rightTrigger
						default: break;
						}
					}
					if (sem >= 0 && sem < 6)
					{
						axisIndex[sem] = static_cast<int>(i);
						axisRest[sem] = rest;
					}
				}
			}
			// Fallback when device has no controllerAxisInfo: assume DInput order 0=lX,1=lY,2=lZ,3=lRx,4=lRy,5=lRz
			if (axisIndex[0] < 0)
			{
				axisRest[0] = axisRest[1] = axisRest[2] = axisRest[3] = 0.5f;
				axisRest[4] = axisRest[5] = 0.f;
				if (sony)
				{
					// Sony: left 0,1; right stick 2,5 (lZ,lRz); triggers 3,4 (lRx,lRy)
					axisIndex = { 0, 1, 2, 5, 3, 4 };
				}
				else
				{
					// Non-Sony: left 0,1; right stick 3,4 (lRx,lRy); triggers 2,5 (lZ,lRz)
					axisIndex = { 0, 1, 3, 4, 2, 5 };
				}
			}
			self->devices.push_back({ device, slot, std::move(displayNameStr), vid, pid, axisIndex, axisRest });
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
			if (std::ranges::none_of(self->devices,
			                         [i](const SlotDevice& d) { return d.slot == i; }))
				return i;
		return -1;
	}
};

bool GameInputBackend::IsAvailable()
{
	__try
	{
		IGameInput* input = nullptr;
		if (FAILED(GameInputCreate(&input)))
			return false;
		if (input)
			input->Release();
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

GameInputBackend::GameInputBackend() : impl_(std::make_unique<Impl>()) {}

GameInputBackend::~GameInputBackend()
{
	if (impl_)
	{
		std::scoped_lock lock(impl_->mutex);
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
	{
		std::scoped_lock lock(impl_->mutex);
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
	}
	// RegisterDeviceCallback may synchronously invoke DeviceCallback (e.g. with
	// GameInputBlockingEnumeration); do not hold impl_->mutex across this call.
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

	std::scoped_lock lock(impl_->mutex);
	for (auto& slotDev : impl_->devices)
	{
		int slot = slotDev.slot;
		IGameInputReading* reading = nullptr;
		HRESULT hr = impl_->input->GetCurrentReading(GameInputKindGamepad, slotDev.device, &reading);
		if (SUCCEEDED(hr) && reading)
		{
			GameInputGamepadState giState{};
			if (SUCCEEDED(reading->GetGamepadState(&giState)))
			{
				auto& gs = impl_->states[static_cast<size_t>(slot)];
				gs.connected = true;
				gs.leftStickX = giState.leftThumbstickX;
				gs.leftStickY = giState.leftThumbstickY;
				gs.rightStickX = giState.rightThumbstickX;
				gs.rightStickY = giState.rightThumbstickY;
				gs.leftTrigger = giState.leftTrigger;
				gs.rightTrigger = giState.rightTrigger;
				gs.buttons = MapGamepadButtons(giState.buttons);
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
			const uint32_t axisCount = reading->GetControllerAxisCount();
			auto axisVal = [&](int sem) -> float {
				int idx = slotDev.axisIndex[sem];
				if (idx < 0 || std::cmp_greater_equal(idx, axisCount))
					return (sem >= 4) ? 0.f : 0.f; // triggers 0, sticks 0 (center)
				float raw = axes[idx];
				float rest = slotDev.axisRest[sem];
				if (sem >= 4) // triggers: 0..1
					return std::clamp(raw, 0.f, 1.f);
				// sticks: normalize from rest (e.g. 0.5) to -1..1
				return std::clamp((raw - rest) * 2.f, -1.f, 1.f);
			};
			gs.leftStickX = axisVal(0);
			gs.leftStickY = axisVal(1);
			gs.rightStickX = axisVal(2);
			gs.rightStickY = axisVal(3);
			gs.leftTrigger = axisVal(4);
			gs.rightTrigger = axisVal(5);
			const bool sony = IsSonyGamepad(slotDev.vendorId, slotDev.productId);
			uint16_t b = 0;
			using enum Button;
			if (sony)
			{
				for (int i = 0; i < 13 && i < kMaxButtons; ++i)
					if (buttons[i])
						b |= MapButtonSonyDInput(i);
			}
			else
			{
				if (kMaxButtons > 0 && buttons[0]) b |= std::to_underlying(A);
				if (kMaxButtons > 1 && buttons[1]) b |= std::to_underlying(B);
				if (kMaxButtons > 2 && buttons[2]) b |= std::to_underlying(X);
				if (kMaxButtons > 3 && buttons[3]) b |= std::to_underlying(Y);
				// Common bumper/thumb/start/back indices when present
				if (kMaxButtons > 4 && buttons[4]) b |= std::to_underlying(LeftBumper);
				if (kMaxButtons > 5 && buttons[5]) b |= std::to_underlying(RightBumper);
				if (kMaxButtons > 6 && buttons[6]) b |= std::to_underlying(Back);
				if (kMaxButtons > 7 && buttons[7]) b |= std::to_underlying(Start);
				if (kMaxButtons > 8 && buttons[8]) b |= std::to_underlying(LeftThumb);
				if (kMaxButtons > 9 && buttons[9]) b |= std::to_underlying(RightThumb);
			}
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
		static constexpr GamepadState empty{};
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
