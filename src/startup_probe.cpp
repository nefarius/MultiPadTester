#include "startup_probe.h"

#include <mutex>

namespace
{
	std::mutex g_hidMutex;
	bool g_hidHavePending = false;
	HidHideStatus g_hidPending{};

	std::mutex g_libwdiMutex;
	bool g_libwdiHavePending = false;
	LibwdiUsbProbeResult g_libwdiPending{};

	void ClearHidPending()
	{
		std::lock_guard lock(g_hidMutex);
		g_hidHavePending = false;
	}

	void ClearLibwdiPending()
	{
		std::lock_guard lock(g_libwdiMutex);
		g_libwdiHavePending = false;
		g_libwdiPending = {};
	}

	void RunHidHideWorker(std::stop_token st, std::atomic<HWND>& hwndSlot, HWND expectedHwnd)
	{
		if (st.stop_requested())
			return;
		const HidHideStatus status = GetHidHideStatus();
		if (st.stop_requested())
			return;
		const HWND slot = hwndSlot.load(std::memory_order_acquire);
		if (slot != expectedHwnd || !IsWindow(expectedHwnd))
			return;
		{
			std::lock_guard lock(g_hidMutex);
			g_hidPending = status;
			g_hidHavePending = true;
		}
		if (st.stop_requested())
		{
			ClearHidPending();
			return;
		}
		const HWND slot2 = hwndSlot.load(std::memory_order_acquire);
		if (slot2 != expectedHwnd || !IsWindow(expectedHwnd))
		{
			ClearHidPending();
			return;
		}
		if (st.stop_requested())
		{
			ClearHidPending();
			return;
		}
		PostMessageW(expectedHwnd, WM_HIDHIDE_PROBE_READY, 0, 0);
	}

	void RunLibwdiWorker(std::stop_token st, std::atomic<HWND>& hwndSlot, HWND expectedHwnd)
	{
		if (st.stop_requested())
			return;
		LibwdiUsbProbeResult result = ProbeLibwdiUsbDevices();
		if (st.stop_requested())
			return;
		const HWND slot = hwndSlot.load(std::memory_order_acquire);
		if (slot != expectedHwnd || !IsWindow(expectedHwnd))
			return;
		{
			std::lock_guard lock(g_libwdiMutex);
			g_libwdiPending = std::move(result);
			g_libwdiHavePending = true;
		}
		if (st.stop_requested())
		{
			ClearLibwdiPending();
			return;
		}
		const HWND slot2 = hwndSlot.load(std::memory_order_acquire);
		if (slot2 != expectedHwnd || !IsWindow(expectedHwnd))
		{
			ClearLibwdiPending();
			return;
		}
		if (st.stop_requested())
		{
			ClearLibwdiPending();
			return;
		}
		PostMessageW(expectedHwnd, WM_LIBWDI_PROBE_READY, 0, 0);
	}
}  // namespace

StartupProbeSession::StartupProbeSession(HWND notifyHwnd)
{
	hwnd_.store(notifyHwnd, std::memory_order_release);
	try
	{
		hidThread_.emplace([this, notifyHwnd](std::stop_token st) {
			RunHidHideWorker(st, hwnd_, notifyHwnd);
		});
		libwdiThread_.emplace([this, notifyHwnd](std::stop_token st) {
			RunLibwdiWorker(st, hwnd_, notifyHwnd);
		});
	}
	catch (...)
	{
		hidThread_.reset();
		libwdiThread_.reset();
		hwnd_.store(nullptr, std::memory_order_release);
	}
}

StartupProbeSession::~StartupProbeSession()
{
	hwnd_.store(nullptr, std::memory_order_release);
	hidThread_.reset();
	libwdiThread_.reset();
}

bool HidHideProbe_PopResultForUi(HidHideStatus& out)
{
	std::lock_guard lock(g_hidMutex);
	if (!g_hidHavePending)
		return false;
	out = g_hidPending;
	g_hidHavePending = false;
	return true;
}

bool LibwdiProbe_PopResultForUi(LibwdiUsbProbeResult& out)
{
	std::lock_guard lock(g_libwdiMutex);
	if (!g_libwdiHavePending)
		return false;
	out = std::move(g_libwdiPending);
	g_libwdiHavePending = false;
	g_libwdiPending = {};
	return true;
}
