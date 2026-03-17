#include "gameinput_backend.h"
#include "sony_layout.h"
#include "usb_names.h"
#include <gameinput.h>

using namespace GameInput::v3;

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
		std::array<int, 6> axisIndex = {-1, -1, -1, -1, -1, -1};
		std::array<float, 6> axisRest = {0.5f, 0.5f, 0.5f, 0.5f, 0.f, 0.f};
	};

	std::vector<SlotDevice> devices;
	std::array<GamepadState, kMaxDevices> states{};
	std::array<std::string, kMaxDevices> slotDisplayNames;

	static void CALLBACK DeviceCallback(
		[[maybe_unused]] GameInputCallbackToken token,
		void* context,
		IGameInputDevice* device,
		[[maybe_unused]] uint64_t timestamp,
		GameInputDeviceStatus currentStatus,
		[[maybe_unused]] GameInputDeviceStatus previousStatus)
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
			const GameInputDeviceInfo* infoPtr = nullptr;
			if (SUCCEEDED(device->GetDeviceInfo(&infoPtr)) && infoPtr)
			{
				const GameInputDeviceInfo* info = infoPtr;
				vid = info->vendorId;
				pid = info->productId;
				// v3: displayName is const char* (UTF-8); often null or generic (e.g. "Wireless Controller")
				if (info->displayName && info->displayName[0])
					displayNameStr = info->displayName;
				// Use VID/PID-based name only when API left it generic
				if (displayNameStr.empty() || displayNameStr.find("Controller ") == 0)
				{
					if (const char* friendly = GetFriendlyName(vid, pid))
						displayNameStr = friendly;
				}
			}
			const bool sony = IsSonyGamepad(vid, pid);
			std::array<int, 6> axisIndex = {-1, -1, -1, -1, -1, -1};
			std::array<float, 6> axisRest = {0.5f, 0.5f, 0.5f, 0.5f, 0.f, 0.f};
			// v3: use controllerInfo (controllerAxisCount + controllerAxisLabels); map triggers by label only
			if (infoPtr && infoPtr->controllerInfo)
			{
				const GameInputControllerInfo* ctrl = infoPtr->controllerInfo;
				for (uint32_t i = 0; i < ctrl->controllerAxisCount && i < 6; ++i)
				{
					GameInputLabel lab = ctrl->controllerAxisLabels[i];
					int sem = -1;
					if (lab == GameInputLabelXboxLeftTrigger)
						sem = 4;
					else if (lab == GameInputLabelXboxRightTrigger)
						sem = 5;
					if (sem >= 0 && sem < 6)
					{
						axisIndex[sem] = static_cast<int>(i);
						axisRest[sem] = (sem == 4 || sem == 5) ? 0.f : 0.5f;
					}
				}
			}
			// Fallback when device has no controllerInfo or unmapped axes: assume DInput order 0=lX,1=lY,2=lZ,3=lRx,4=lRy,5=lRz
			if (axisIndex[0] < 0)
			{
				axisRest[0] = axisRest[1] = axisRest[2] = axisRest[3] = 0.5f;
				axisRest[4] = axisRest[5] = 0.f;
				if (sony)
				{
					// Sony: left 0,1; right stick 2,5 (lZ,lRz); triggers 3,4 (lRx,lRy)
					axisIndex = {0, 1, 2, 5, 3, 4};
				}
				else
				{
					// Non-Sony: left 0,1; right stick 3,4 (lRx,lRy); triggers 2,5 (lZ,lRz)
					axisIndex = {0, 1, 3, 4, 2, 5};
				}
			}
			self->devices.push_back({device, slot, std::move(displayNameStr), vid, pid, axisIndex, axisRest});
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
		for (int i = 0; i < kMaxDevices; ++i)
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

GameInputBackend::GameInputBackend() : impl_(std::make_unique<Impl>())
{
}

GameInputBackend::~GameInputBackend()
{
	if (impl_)
	{
		IGameInput* input = nullptr;
		GameInputCallbackToken token = 0;
		{
			std::scoped_lock lock(impl_->mutex);
			input = impl_->input;
			token = impl_->callbackToken;
			impl_->input = nullptr;
			impl_->callbackToken = 0;
		}
		if (input && token)
		{
#if defined(GAMEINPUT_API_VERSION) && GAMEINPUT_API_VERSION >= 1
			input->UnregisterCallback(token);
#else
			input->UnregisterCallback(token, 500000); // 0.5s timeout to allow in-flight DeviceCallback to finish
#endif
		}
		{
			std::scoped_lock lock(impl_->mutex);
			for (auto& d : impl_->devices)
				if (d.device)
					d.device->Release();
			impl_->devices.clear();
			if (impl_->dispatcher)
			{
				impl_->dispatcher->Release();
				impl_->dispatcher = nullptr;
			}
			if (input)
			{
				input->Release();
			}
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
	HRESULT hr = impl_->input->RegisterDeviceCallback(
		nullptr,
		GameInputKindController,
		GameInputDeviceAnyStatus,
		GameInputBlockingEnumeration,
		impl_.get(),
		&Impl::DeviceCallback,
		&impl_->callbackToken);
	if (FAILED(hr))
	{
		if (impl_->dispatcher)
		{
			impl_->dispatcher->Release();
			impl_->dispatcher = nullptr;
		}
		impl_->input->Release();
		impl_->input = nullptr;
	}
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
			auto axisVal = [&](int sem) -> float
			{
				int idx = slotDev.axisIndex[sem];
				if (idx < 0 || idx >= kMaxAxes || std::cmp_greater_equal(idx, axisCount))
					return 0.f;
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
				switch (hat)
				{
				case GameInputSwitchUp: b |= std::to_underlying(DPadUp);
					break;
				case GameInputSwitchUpRight: b |= std::to_underlying(DPadUp) | std::to_underlying(DPadRight);
					break;
				case GameInputSwitchRight: b |= std::to_underlying(DPadRight);
					break;
				case GameInputSwitchDownRight: b |= std::to_underlying(DPadDown) | std::to_underlying(DPadRight);
					break;
				case GameInputSwitchDown: b |= std::to_underlying(DPadDown);
					break;
				case GameInputSwitchDownLeft: b |= std::to_underlying(DPadDown) | std::to_underlying(DPadLeft);
					break;
				case GameInputSwitchLeft: b |= std::to_underlying(DPadLeft);
					break;
				case GameInputSwitchUpLeft: b |= std::to_underlying(DPadUp) | std::to_underlying(DPadLeft);
					break;
				default: break;
				}
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

void GameInputBackend::GetSlotDeviceIds(int slot, uint16_t* vendorId, uint16_t* productId) const
{
	if (vendorId) *vendorId = 0;
	if (productId) *productId = 0;
	if (slot < 0 || slot >= kMaxDevices)
		return;
	std::scoped_lock lock(impl_->mutex);
	auto it = std::ranges::find_if(impl_->devices, [slot](const Impl::SlotDevice& d) { return d.slot == slot; });
	if (it != impl_->devices.end())
	{
		if (vendorId) *vendorId = it->vendorId;
		if (productId) *productId = it->productId;
	}
}
