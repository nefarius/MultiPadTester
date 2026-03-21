#pragma once

#include <Windows.h>

#include <atomic>
#include <memory>
#include <optional>
#include <thread>

#include "hidhide_probe.h"
#include "libwdi_probe.h"

/** Posted when background HidHide probe finished; call HidHideProbe_PopResultForUi from the UI thread. */
constexpr UINT WM_HIDHIDE_PROBE_READY = WM_APP + 43;
/** Posted when background libwdi USB probe finished; call LibwdiProbe_PopResultForUi from the UI thread. */
constexpr UINT WM_LIBWDI_PROBE_READY = WM_APP + 44;

class StartupProbeSession
{
public:
	explicit StartupProbeSession(HWND notifyHwnd);
	~StartupProbeSession();

	StartupProbeSession(const StartupProbeSession&) = delete;
	StartupProbeSession& operator=(const StartupProbeSession&) = delete;
	StartupProbeSession(StartupProbeSession&&) = delete;
	StartupProbeSession& operator=(StartupProbeSession&&) = delete;

private:
	void requestStopAndInvalidateNotify() noexcept;

	std::atomic<HWND> hwnd_{nullptr};
	std::optional<std::jthread> hidThread_;
	std::optional<std::jthread> libwdiThread_;

	friend void StartupProbeSession_ShutdownAsync(std::unique_ptr<StartupProbeSession>&& session);
};

bool HidHideProbe_PopResultForUi(HidHideStatus& out);
bool LibwdiProbe_PopResultForUi(LibwdiUsbProbeResult& out);

/** Clears notify HWND, requests jthread stop, then finishes teardown/join on a worker thread. */
void StartupProbeSession_ShutdownAsync(std::unique_ptr<StartupProbeSession>&& session);
