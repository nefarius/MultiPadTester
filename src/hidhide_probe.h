#pragma once

enum class HidHideStatus
{
	NotInstalled,
	InstalledInactive,
	InstalledActive,
	AccessDenied,
	QueryFailed
};

HidHideStatus GetHidHideStatus();
