#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <string>

/** Posted to `notifyHwnd` when a newer build is available (worker verified HWND is still current). */
constexpr UINT WM_UPDATE_CHECK_READY = WM_APP + 42;

/**
 * Starts a detached background thread: optional 24h suppression, HTTPS GET of build metadata,
 * compare remote FileVersion to local EXE version. On success only, posts WM_UPDATE_CHECK_READY.
 * Any failure or "no update" is silent.
 *
 * @param notifyHwnd Window to receive WM_UPDATE_CHECK_READY.
 * @param hwndSlot If non-null, PostMessage is skipped unless this equals notifyHwnd (cleared on WM_DESTROY).
 * @param updateDismissedUnix UTC Unix seconds when user dismissed the dialog; 0 = never.
 */
void StartBackgroundUpdateCheck(
	HWND notifyHwnd,
	std::atomic<HWND>* hwndSlot,
	int64_t updateDismissedUnix);

/** Call from the main thread when handling WM_UPDATE_CHECK_READY; copies version strings for the UI. */
bool UpdateCheck_PopResultForUi(std::string& localVersionOut, std::string& remoteVersionOut);
