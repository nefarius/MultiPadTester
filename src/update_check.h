#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>

/** Posted to `notifyHwnd` when a newer build is available (worker verified HWND is still current). */
constexpr UINT WM_UPDATE_CHECK_READY = WM_APP + 42;

/**
 * Owns the background update worker (`std::jthread`): requests stop and joins on destruction so shutdown
 * does not race `PostMessage` or namespace-scope result state. The constructor swallows thread start failures.
 */
class UpdateCheckSession
{
public:
	explicit UpdateCheckSession(HWND notifyHwnd, int64_t updateDismissedUnix);
	~UpdateCheckSession();

	UpdateCheckSession(const UpdateCheckSession&) = delete;
	UpdateCheckSession& operator=(const UpdateCheckSession&) = delete;
	UpdateCheckSession(UpdateCheckSession&&) = delete;
	UpdateCheckSession& operator=(UpdateCheckSession&&) = delete;

private:
	std::atomic<HWND> hwnd_{nullptr};
	std::optional<std::jthread> worker_;
};

/** Call from the main thread when handling WM_UPDATE_CHECK_READY; copies version strings for the UI. */
bool UpdateCheck_PopResultForUi(std::string& localVersionOut, std::string& remoteVersionOut);

/** HTTPS URL of the latest portable ZIP (build artifact); single source of truth with the update checker. */
const wchar_t* UpdateCheck_GetLatestDownloadUrlW();
