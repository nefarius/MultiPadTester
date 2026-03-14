#pragma once
#include "gamepad_state.h"
#include "imgui.h"
#include <algorithm>

/**
	 * Layout for mapping logical gamepad coordinates to screen space.
	 *
	 * Encapsulates an origin and separate X/Y scale factors used to transform
	 * positions and sizes from the layout's logical coordinate system into
	 * screen coordinates.
	 */
	 
	/**
	 * Transform a logical point into screen coordinates by applying the layout origin and scaling.
	 * @param x X coordinate in the layout's logical coordinate space.
	 * @param y Y coordinate in the layout's logical coordinate space.
	 * @returns The transformed point in screen coordinates.
	 */
	 
	/**
	 * Scale a scalar value using the layout's uniform scale (the smaller of sx and sy).
	 * @param v Value in the layout's logical units.
	 * @returns The value scaled into screen units.
	 */
	namespace GamepadRenderer
{
	struct Layout
	{
		ImVec2 origin;
		float sx, sy;

		[[nodiscard]] ImVec2 P(float x, float y) const
		{
			return ImVec2(origin.x + x * sx, origin.y + y * sy);
		}

		[[nodiscard]] float S(float v) const { return v * std::min(sx, sy); }
	};

	void DrawGamepad(ImDrawList* dl, ImVec2 panelPos, ImVec2 panelSize,
	                 const GamepadState& gs, int slotIndex, const char* backendName,
	                 const char* displayName = nullptr,
	                 ImTextureID bodyTexture = nullptr,
	                 ImVec2 textureSizeLogical = ImVec2(400.f, 280.f));
} // namespace GamepadRenderer
