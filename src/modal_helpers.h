#pragma once

#include "imgui.h"

/** Centered modal with min size; call OpenPopup first when showing. */
bool BeginCenteredModal(
	const char* popupId,
	bool* p_open,
	float minW,
	float minH,
	ImGuiWindowFlags extra = 0);
