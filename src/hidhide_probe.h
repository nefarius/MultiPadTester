#pragma once

/**
 * @brief Represents the installation and access state of HidHide.
 *
 * @details
 * - NotInstalled: HidHide is not present on the system.
 * - InstalledInactive: HidHide is installed but not currently active.
 * - InstalledActive: HidHide is installed and actively hiding devices.
 * - AccessDenied: The process lacks permission to query HidHide status.
 * - QueryFailed: The status check could not be completed due to an error.
 */
enum class HidHideStatus
{
	NotInstalled,
	InstalledInactive,
	InstalledActive,
	AccessDenied,
	QueryFailed
};

HidHideStatus GetHidHideStatus();
