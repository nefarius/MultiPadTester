#pragma once
#include "gamepad_state.h"
#include <cstdint>
#include <Windows.h>

/**
 * Abstract interface for input backends that provide per-slot gamepad state.
 *
 * Implementations (e.g. XInput, Raw Input) poll devices and expose state via
 * GetState(slot). Optional hooks: Init(hwnd), OnWindowMessage for window-based
 * backends; GetSlotDisplayName and GetSlotDeviceIds for UI/layout selection.
 */
class IInputBackend
{
public:
	/** Virtual destructor for correct cleanup of derived implementations. */
	virtual ~IInputBackend() = default;

	/**
	 * Initialize the backend with an optional window handle.
	 * @param hwnd Window handle for backend setup; may be ignored by the implementation.
	 */
	virtual void Init(HWND /*hwnd*/)
	{
	}

	/**
	 * Process a window message. Used by backends that need Win32 message handling.
	 * @param msg Window message identifier.
	 * @param wParam Message wParam.
	 * @param lParam Message lParam.
	 * @return true if the message was handled, false otherwise.
	 */
	virtual bool OnWindowMessage(UINT /*msg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) { return false; }

	/** Poll and update input state for all slots. Call once per frame. */
	virtual void Poll() = 0;

	/**
	 * Maximum number of input slots this backend supports.
	 * @return Number of slots (valid slot indices are 0 .. GetMaxSlots()-1).
	 */
	[[nodiscard]] virtual int GetMaxSlots() const = 0;

	/**
	 * Current gamepad state for a slot.
	 * @param slot Slot index in [0, GetMaxSlots()).
	 * @return Const reference to the GamepadState for that slot.
	 */
	[[nodiscard]] virtual const GamepadState& GetState(int slot) const = 0;

	/**
	 * Backend identifier (e.g. "XInput", "RawInput").
	 * @return Null-terminated C-string, never nullptr.
	 */
	[[nodiscard]] virtual const char* GetName() const = 0;

	/**
	 * Human-readable name for a slot (e.g. device/product name), if available.
	 * @param slot Slot index.
	 * @return Null-terminated C-string, or nullptr if not available.
	 */
	[[nodiscard]] virtual const char* GetSlotDisplayName([[maybe_unused]] int slot) const { return nullptr; }

	/**
	 * USB vendor and product ID for a slot (e.g. for button layout selection).
	 * @param slot Slot index.
	 * @param vendorId Output; set to 0 if unknown. May be nullptr.
	 * @param productId Output; set to 0 if unknown. May be nullptr.
	 */
	virtual void GetSlotDeviceIds([[maybe_unused]] int slot, uint16_t* vendorId, uint16_t* productId) const
	{
		if (vendorId) *vendorId = 0;
		if (productId) *productId = 0;
	}
};
