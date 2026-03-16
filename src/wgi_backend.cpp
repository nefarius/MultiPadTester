#include "wgi_backend.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <array>
#include <mutex>
#include <optional>
#include <string>

namespace
{
	using namespace winrt;
	using namespace Windows::Gaming::Input;
	using namespace Windows::Foundation;

	constexpr int kMaxSlots = 4;

	uint16_t MapGamepadButtons(GamepadButtons wgi)
	{
		uint16_t b = 0;
		using enum Button;
		if ((wgi & GamepadButtons::Menu) != GamepadButtons::None) b |= std::to_underlying(Start);
		if ((wgi & GamepadButtons::View) != GamepadButtons::None) b |= std::to_underlying(Back);
		if ((wgi & GamepadButtons::A) != GamepadButtons::None) b |= std::to_underlying(A);
		if ((wgi & GamepadButtons::B) != GamepadButtons::None) b |= std::to_underlying(B);
		if ((wgi & GamepadButtons::X) != GamepadButtons::None) b |= std::to_underlying(X);
		if ((wgi & GamepadButtons::Y) != GamepadButtons::None) b |= std::to_underlying(Y);
		if ((wgi & GamepadButtons::DPadUp) != GamepadButtons::None) b |= std::to_underlying(DPadUp);
		if ((wgi & GamepadButtons::DPadDown) != GamepadButtons::None) b |= std::to_underlying(DPadDown);
		if ((wgi & GamepadButtons::DPadLeft) != GamepadButtons::None) b |= std::to_underlying(DPadLeft);
		if ((wgi & GamepadButtons::DPadRight) != GamepadButtons::None) b |= std::to_underlying(DPadRight);
		if ((wgi & GamepadButtons::LeftShoulder) != GamepadButtons::None) b |= std::to_underlying(LeftBumper);
		if ((wgi & GamepadButtons::RightShoulder) != GamepadButtons::None) b |= std::to_underlying(RightBumper);
		if ((wgi & GamepadButtons::LeftThumbstick) != GamepadButtons::None) b |= std::to_underlying(LeftThumb);
		if ((wgi & GamepadButtons::RightThumbstick) != GamepadButtons::None) b |= std::to_underlying(RightThumb);
		return b;
	}
}

struct WgiBackend::Impl
{
	mutable std::mutex mutex;
	std::array<std::optional<Gamepad>, kMaxSlots> slotGamepads{};
	GamepadState states[kMaxSlots]{};
	std::array<std::string, kMaxSlots> slotDisplayNames;

	winrt::event_token addedToken;
	winrt::event_token removedToken;

	void OnGamepadAdded(IInspectable const&, Gamepad const& pad)
	{
		std::lock_guard lock(mutex);
		for (int i = 0; i < kMaxSlots; ++i)
		{
			if (!slotGamepads[i])
			{
				slotGamepads[i] = pad;
				slotDisplayNames[i] = "Gamepad " + std::to_string(i);
				break;
			}
		}
	}

	void OnGamepadRemoved(IInspectable const&, Gamepad const& pad)
	{
		std::lock_guard lock(mutex);
		for (int i = 0; i < kMaxSlots; ++i)
		{
			if (slotGamepads[i] && slotGamepads[i] == pad)
			{
				slotGamepads[i].reset();
				break;
			}
		}
	}
};

WgiBackend::WgiBackend() : impl_(std::make_unique<Impl>()) {}

WgiBackend::~WgiBackend()
{
	if (impl_)
	{
		Gamepad::GamepadAdded(impl_->addedToken);
		Gamepad::GamepadRemoved(impl_->removedToken);
	}
}

void WgiBackend::Init(HWND)
{
	thread_local static bool winrtInitialized = false;
	if (!winrtInitialized)
	{
		winrt::init_apartment();
		winrtInitialized = true;
	}

	std::lock_guard lock(impl_->mutex);
	// Enumerate already-connected gamepads (GetAt in loop avoids IVectorView::Size() auto return type issue with MSVC)
	auto gamepads = Gamepad::Gamepads();
	for (int i = 0; i < kMaxSlots; ++i)
	{
		try
		{
			impl_->slotGamepads[i] = gamepads.GetAt(static_cast<uint32_t>(i));
			impl_->slotDisplayNames[i] = "Gamepad " + std::to_string(i);
		}
		catch (winrt::hresult_out_of_bounds const&)
		{
			break;
		}
	}
	impl_->addedToken = Gamepad::GamepadAdded([this](IInspectable const& s, Gamepad const& p) { impl_->OnGamepadAdded(s, p); });
	impl_->removedToken = Gamepad::GamepadRemoved([this](IInspectable const& s, Gamepad const& p) { impl_->OnGamepadRemoved(s, p); });
}

void WgiBackend::Poll()
{
	std::lock_guard lock(impl_->mutex);
	for (int i = 0; i < kMaxSlots; ++i)
	{
		auto& gs = impl_->states[i];
		if (!impl_->slotGamepads[i])
		{
			gs = GamepadState{};
			continue;
		}
		try
		{
			auto reading = impl_->slotGamepads[i]->GetCurrentReading();
			gs.connected = true;
			gs.buttons = MapGamepadButtons(reading.Buttons);
			gs.leftTrigger = static_cast<float>(reading.LeftTrigger);
			gs.rightTrigger = static_cast<float>(reading.RightTrigger);
			gs.leftStickX = static_cast<float>(reading.LeftThumbstickX);
			gs.leftStickY = static_cast<float>(reading.LeftThumbstickY);
			gs.rightStickX = static_cast<float>(reading.RightThumbstickX);
			gs.rightStickY = static_cast<float>(reading.RightThumbstickY);
		}
		catch (...)
		{
			gs = GamepadState{};
		}
	}
}

int WgiBackend::GetMaxSlots() const { return kMaxSlots; }

const GamepadState& WgiBackend::GetState(int slot) const
{
	static GamepadState const empty{};
	if (slot < 0 || slot >= kMaxSlots)
		return empty;
	return impl_->states[slot];
}

const char* WgiBackend::GetName() const { return Name; }

const char* WgiBackend::GetSlotDisplayName(int slot) const
{
	if (slot < 0 || slot >= kMaxSlots) return nullptr;
	std::lock_guard lock(impl_->mutex);
	if (!impl_->slotGamepads[slot]) return nullptr;
	thread_local static std::string result;
	result = impl_->slotDisplayNames[slot];
	return result.c_str();
}
