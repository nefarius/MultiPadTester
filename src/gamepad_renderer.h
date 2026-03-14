#pragma once
#include "gamepad_state.h"
#include "imgui.h"
#include <algorithm>

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
	                 const char* displayName = nullptr);
} // namespace GamepadRenderer
