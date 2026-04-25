#include "wgi_backend.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using namespace winrt;
	using namespace Windows::Gaming::Input;
	using namespace Windows::Foundation;

	/**
	 * @brief Converts a WinRT GamepadButtons bitmask into the internal 16-bit button mask.
	 *
	 * @param wgi Bitfield of WinRT GamepadButtons flags to map.
	 * @return uint16_t Bitmask where each set bit corresponds to the internal Button enum value for a pressed button.
	 */
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

	/**
	 * @brief Obtains a human-friendly display name for a gamepad, falling back to a numbered default.
	 *
	 * Attempts to retrieve the controller's DisplayName from its RawGameController representation.
	 * If a display name is unavailable or retrieval fails, returns "Gamepad N" where N is
	 * slotIndex + 1.
	 *
	 * @param pad WinRT Gamepad instance to query.
	 * @param slotIndex Zero-based slot index used to generate the fallback name.
	 * @return std::string The resolved display name or a fallback like "Gamepad 1".
	 */
	std::string GetDisplayNameForGamepad(const Gamepad& pad, int slotIndex)
	{
		try
		{
			auto raw = RawGameController::FromGameController(pad);
			if (raw)
			{
				auto name = raw.DisplayName();
				if (!name.empty())
					return to_string(name);
			}
		}
		catch (...)
		{
		}
		return "Gamepad " + std::to_string(slotIndex + 1);
	}

	/**
	 * @brief Retrieve the hardware vendor and product IDs for a gamepad.
	 *
	 * Attempts to obtain the controller's hardware vendor and product identifiers
	 * and writes them to the provided pointers. If retrieval fails or the
	 * identifiers are unavailable, writes 0 to the corresponding outputs.
	 * If both output pointers are null, the function does nothing.
	 *
	 * @param pad The gamepad to query.
	 * @param vendorId Pointer to receive the vendor ID, or nullptr to skip.
	 * @param productId Pointer to receive the product ID, or nullptr to skip.
	 */
	void GetDeviceIdsForGamepad(const Gamepad& pad, uint16_t* vendorId, uint16_t* productId)
	{
		if (!vendorId && !productId) return;
		try
		{
			auto raw = RawGameController::FromGameController(pad);
			if (raw)
			{
				if (vendorId) *vendorId = raw.HardwareVendorId();
				if (productId) *productId = raw.HardwareProductId();
				return;
			}
		}
		catch (...)
		{
		}
		if (vendorId) *vendorId = 0;
		if (productId) *productId = 0;
	}
}

struct WgiBackend::Impl
{
	mutable std::mutex mutex;
	std::vector<std::optional<Gamepad>> slotGamepads;
	std::vector<GamepadState> states;
	std::vector<std::string> slotDisplayNames;
	std::vector<uint16_t> slotVendorIds;
	std::vector<uint16_t> slotProductIds;

	event_token addedToken;
	event_token removedToken;

	/**
	 * @brief Re-enumerate the system gamepad list and rebuild the slot arrays.
	 *
	 * Must be called with mutex held.
	 */
	void ResyncFromSystemLocked()
	{
		auto gamepads = Gamepad::Gamepads();
		const size_t count = static_cast<size_t>(gamepads.Size());

		slotGamepads.assign(count, std::nullopt);
		states.assign(count, GamepadState{});
		slotDisplayNames.assign(count, std::string{});
		slotVendorIds.assign(count, 0);
		slotProductIds.assign(count, 0);

		for (size_t i = 0; i < count; ++i)
		{
			try
			{
				auto pad = gamepads.GetAt(static_cast<uint32_t>(i));
				slotGamepads[i] = pad;
				slotDisplayNames[i] = GetDisplayNameForGamepad(pad, static_cast<int>(i));
				GetDeviceIdsForGamepad(pad, &slotVendorIds[i], &slotProductIds[i]);
			}
			catch (...)
			{
				// If enumeration fails mid-way, leave remaining entries as empty/default.
				break;
			}
		}
	}

	/**
	 * @brief Handles a gamepad add event by re-syncing the system list.
	 */
	void OnGamepadAdded(const IInspectable&, const Gamepad&)
	{
		std::lock_guard lock(mutex);
		ResyncFromSystemLocked();
	}

	/**
	 * @brief Handles a gamepad remove event by re-syncing the system list.
	 */
	void OnGamepadRemoved(const IInspectable&, const Gamepad&)
	{
		std::lock_guard lock(mutex);
		ResyncFromSystemLocked();
	}
};

WgiBackend::WgiBackend() : impl_(std::make_unique<Impl>())
{
}

WgiBackend::~WgiBackend()
{
	if (impl_)
	{
		Gamepad::GamepadAdded(impl_->addedToken);
		Gamepad::GamepadRemoved(impl_->removedToken);
	}
}

/**
 * @brief Initialize the WinRT gamepad backend, enumerate currently connected gamepads, and subscribe to add/remove events.
 *
 * Initializes the WinRT apartment once per thread, acquires the internal mutex, populates slots with already-connected gamepads (storing each slot's handle, display name, and vendor/product IDs), and registers handlers for future GamepadAdded and GamepadRemoved events, saving their subscription tokens.
 *
 * @param hwnd Window handle provided for initialization context; currently accepted for API compatibility and not otherwise used.
 */
void WgiBackend::Init(HWND)
{
	thread_local bool winrtInitialized = false;
	if (!winrtInitialized)
	{
		init_apartment();
		winrtInitialized = true;
	}

	std::lock_guard lock(impl_->mutex);
	impl_->ResyncFromSystemLocked();
	impl_->addedToken = Gamepad::GamepadAdded([this](const IInspectable& s, const Gamepad& p)
	{
		impl_->OnGamepadAdded(s, p);
	});
	impl_->removedToken = Gamepad::GamepadRemoved([this](const IInspectable& s, const Gamepad& p)
	{
		impl_->OnGamepadRemoved(s, p);
	});
}

void WgiBackend::Poll()
{
	std::lock_guard lock(impl_->mutex);
	const size_t count = impl_->states.size();
	for (size_t i = 0; i < count; ++i)
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

int WgiBackend::GetMaxSlots() const
{
	std::lock_guard lock(impl_->mutex);
	return static_cast<int>(impl_->states.size());
}

const GamepadState& WgiBackend::GetState(int slot) const
{
	static constexpr GamepadState empty{};
	if (slot < 0)
		return empty;

	std::lock_guard lock(impl_->mutex);
	if (static_cast<size_t>(slot) >= impl_->states.size())
		return empty;

	// Return a stable reference even if slots resize on another thread.
	thread_local std::vector<GamepadState> slotResultStates;
	if (slotResultStates.size() < impl_->states.size())
		slotResultStates.resize(impl_->states.size());
	slotResultStates[static_cast<size_t>(slot)] = impl_->states[static_cast<size_t>(slot)];
	return slotResultStates[static_cast<size_t>(slot)];
}

const char* WgiBackend::GetName() const { return Name; }

/**
 * @brief Retrieves the human-readable display name for a gamepad slot.
 *
 * @param slot Zero-based slot index.
 * @return const char* Pointer to a null-terminated C string containing the display name for the given slot, or `nullptr` if the slot index is out of range or no gamepad is present in that slot. The returned pointer is valid until the next call to GetSlotDisplayName on the same thread.
 */
const char* WgiBackend::GetSlotDisplayName(int slot) const
{
	if (slot < 0) return nullptr;
	std::lock_guard lock(impl_->mutex);
	if (static_cast<size_t>(slot) >= impl_->slotGamepads.size()) return nullptr;
	if (!impl_->slotGamepads[static_cast<size_t>(slot)]) return nullptr;
	thread_local std::vector<std::string> slotResultBuffers;
	if (slotResultBuffers.size() < impl_->slotDisplayNames.size())
		slotResultBuffers.resize(impl_->slotDisplayNames.size());
	slotResultBuffers[static_cast<size_t>(slot)] = impl_->slotDisplayNames[static_cast<size_t>(slot)];
	return slotResultBuffers[static_cast<size_t>(slot)].c_str();
}

/**
 * @brief Retrieve the vendor and product identifiers for a slot's gamepad.
 *
 * Writes the 16-bit vendor and product IDs for the gamepad in the given slot into the provided output pointers. If the slot index is out of range or no gamepad is present in that slot, `0` is written for each ID. Passing a `nullptr` for either output pointer suppresses writing that value.
 *
 * @param slot Slot index to query.
 * @param vendorId Pointer that receives the vendor ID, or `nullptr` to ignore.
 * @param productId Pointer that receives the product ID, or `nullptr` to ignore.
 */
void WgiBackend::GetSlotDeviceIds(int slot, uint16_t* vendorId, uint16_t* productId) const
{
	if (slot < 0)
	{
		if (vendorId) *vendorId = 0;
		if (productId) *productId = 0;
		return;
	}
	std::lock_guard lock(impl_->mutex);
	if (static_cast<size_t>(slot) >= impl_->slotGamepads.size())
	{
		if (vendorId) *vendorId = 0;
		if (productId) *productId = 0;
		return;
	}
	uint16_t vid = impl_->slotGamepads[static_cast<size_t>(slot)] ? impl_->slotVendorIds[static_cast<size_t>(slot)] : 0;
	uint16_t pid = impl_->slotGamepads[static_cast<size_t>(slot)] ? impl_->slotProductIds[static_cast<size_t>(slot)] : 0;
	if (vendorId) *vendorId = vid;
	if (productId) *productId = pid;
}
