#pragma once
#include "gamepad_state.h"
#include "imgui.h"
#include <algorithm>

namespace GamepadRenderer
{
	/**
	 * Layout for mapping logical gamepad coordinates to screen space.
	 *
	 * The layout stores a screen-space origin and per-axis scale factors used to
	 * convert logical coordinates into pixels for rendering controller artwork.
	 */
	enum class LayoutType { Xbox, Sony };

	/** Sentinel for "no body texture" (valid for both pointer and integer ImTextureID backends). */
	inline constexpr ImTextureID kNoBodyTexture = static_cast<ImTextureID>(0);

	struct Layout
	{
		ImVec2 origin;
		float sx, sy;

		/**
		 * Transform a logical point into screen coordinates.
		 *
		 * @param x X coordinate in logical layout units.
		 * @param y Y coordinate in logical layout units.
		 * @return Screen-space position after scaling and origin offset.
		 */
		[[nodiscard]] ImVec2 P(float x, float y) const
		{
			return ImVec2(origin.x + x * sx, origin.y + y * sy);
		}

		/**
		 * Scale a scalar using the smaller of the layout's axis scales.
		 *
		 * @param v Value in logical units.
		 * @return Scaled value in screen units.
		 */
		[[nodiscard]] float S(float v) const { return v * std::min(sx, sy); }
	};

	/**
	 * Draw a gamepad representation in the given ImGui draw list.
	 *
	 * @param dl Target draw list.
	 * @param panelPos Top-left position of the drawing panel.
	 * @param panelSize Size of the drawing panel.
	 * @param gs Current gamepad state to visualize.
	 * @param slotIndex Controller slot index shown in the overlay.
	 * @param backendName Backend name shown in the overlay.
	 * @param displayName Optional human-readable device name.
	 * @param bodyTexture Optional body texture for the controller.
	 * @param textureSizeLogical Logical texture size used for layout scaling.
	 * @param layoutType Controller layout to use.
	 */
	void DrawGamepad(ImDrawList* dl, ImVec2 panelPos, ImVec2 panelSize,
	                 const GamepadState& gs, int slotIndex, const char* backendName,
	                 const char* displayName = nullptr,
	                 ImTextureID bodyTexture = kNoBodyTexture,
	                 ImVec2 textureSizeLogical = ImVec2(400.f, 280.f),
	                 LayoutType layoutType = LayoutType::Xbox);
} // namespace GamepadRenderer
