#pragma once

enum class HidHideStatus
{
	NotInstalled,
	InstalledInactive,
	InstalledActive,
	QueryFailed
};

HidHideStatus GetHidHideStatus();
