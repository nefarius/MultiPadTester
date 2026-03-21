#include "modal_helpers.h"

#include <cfloat>

bool BeginCenteredModal(
	const char* popupId,
	bool* p_open,
	float minW,
	float minH,
	ImGuiWindowFlags extra)
{
	const bool active =
		(p_open ? *p_open : false) || ImGui::IsPopupOpen(popupId, ImGuiPopupFlags_None);
	if (active)
	{
		ImGui::SetNextWindowSizeConstraints(ImVec2(minW, minH), ImVec2(FLT_MAX, FLT_MAX));
		ImGui::SetNextWindowPos(
			ImGui::GetMainViewport()->GetCenter(),
			ImGuiCond_Appearing,
			ImVec2(0.5f, 0.5f));
	}
	return ImGui::BeginPopupModal(
		popupId,
		p_open,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | extra);
}
