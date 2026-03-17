#pragma once
#include "gamepad_state.h"
#include <cstdint>
#include <Windows.h>

/**
 * Abstract interface for input backends that provide per-slot gamepad state.
 */
 
/**
 * Virtual destructor to allow proper cleanup of derived implementations.
 */

/**
 * Initialize the backend with an optional window handle.
 * @param hwnd Window handle used for backend initialization (may be ignored).
 */

/**
 * Handle a window message.
 * @param msg The window message identifier.
 * @param wParam The message wParam.
 * @param lParam The message lParam.
 * @returns `true` if the message was handled, `false` otherwise.
 */

/**
 * Update the backend's input state for all slots.
 */

/**
 * Get the maximum number of input slots supported by this backend.
 * @returns The maximum number of supported input slots.
 */

/**
 * Get the current gamepad state for a specific slot.
 * @param slot Index of the input slot.
 * @returns A const reference to the GamepadState for the specified slot.
 */

/**
 * Get the name of this backend.
 * @returns A null-terminated C-string identifying the backend.
 */

/**
 * Get a display name for a specific slot, if available.
 * @param slot Index of the input slot.
 * @returns A null-terminated C-string containing the slot display name, or `nullptr` if none is available.
 */

/**
 * Get USB vendor and product ID for a slot, if available (e.g. for layout selection).
 * @param slot Index of the input slot.
 * @param vendorId Output; set to 0 if unknown.
 * @param productId Output; set to 0 if unknown.
 */
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
	[[nodiscard]] virtual const char* GetSlotDisplayName([[maybe_unused]] int slot) const { return nullptr; }
	virtual void GetSlotDeviceIds([[maybe_unused]] int slot, uint16_t* vendorId, uint16_t* productId) const
	{
		if (vendorId) *vendorId = 0;
		if (productId) *productId = 0;
	}
};
