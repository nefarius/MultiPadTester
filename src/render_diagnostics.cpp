#include "render_diagnostics.h"

#include "diagnostic_csv.h"

#include <wil/com.h>
#include <winver.h>

#include <dxgi.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr size_t kMaxSamples = 20000;

using SteadyClock = std::chrono::steady_clock;

struct CaptureState
{
	bool recording = false;
	bool sawMinimized = false;
	bool sawOccluded = false;
	size_t dropped = 0;
	int earlyWakeups = 0;
	bool framePending = false;
	SteadyClock::time_point frameDeadline{};
	SteadyClock::time_point frameRenderStart{};
	int frameEarlyWakeups = 0;
	bool hasPreviousPresent = false;
	SteadyClock::time_point previousPresentStart{};
	SteadyClock::time_point steadyOrigin{};
	LARGE_INTEGER qpcOrigin{};
	LARGE_INTEGER qpcFrequency{};
	std::wstring metadataPath;
	std::wstring csvPath;
	std::string metadata;
	std::vector<DiagnosticCsvFrame> samples;
};

CaptureState g_capture;

int64_t QpcNowUs()
{
	LARGE_INTEGER counter{};
	QueryPerformanceCounter(&counter);
	if (g_capture.qpcFrequency.QuadPart <= 0)
		return 0;
	return (counter.QuadPart - g_capture.qpcOrigin.QuadPart) * 1000000 /
		g_capture.qpcFrequency.QuadPart;
}

int64_t SteadyUs(const SteadyClock::time_point time)
{
	return std::chrono::duration_cast<std::chrono::microseconds>(time - g_capture.steadyOrigin).count();
}

std::string WideToUtf8(const std::wstring_view text)
{
	if (text.empty())
		return {};
	const int bytes = WideCharToMultiByte(
		CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	if (bytes <= 0)
		return {};
	std::string utf8(static_cast<size_t>(bytes), '\0');
	WideCharToMultiByte(
		CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), bytes, nullptr, nullptr);
	return utf8;
}

std::wstring DiagnosticsDirectory()
{
	wchar_t appdata[MAX_PATH]{};
	if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0)
		return {};
	const std::wstring root = std::wstring(appdata) + L"\\MultiPadTester";
	const std::wstring dir = root + L"\\diagnostics";
	CreateDirectoryW(root.c_str(), nullptr);
	CreateDirectoryW(dir.c_str(), nullptr);
	return dir;
}

std::string ExecutableVersion()
{
	wchar_t path[MAX_PATH]{};
	if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
		return "unknown";
	DWORD handle = 0;
	const DWORD verSize = GetFileVersionInfoSizeW(path, &handle);
	if (verSize == 0)
		return "unknown";
	std::vector<BYTE> buffer(verSize);
	if (!GetFileVersionInfoW(path, 0, verSize, buffer.data()))
		return "unknown";
	VS_FIXEDFILEINFO* info = nullptr;
	UINT infoLen = 0;
	if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&info), &infoLen) || !info ||
	    infoLen < sizeof(VS_FIXEDFILEINFO))
		return "unknown";
	const uint32_t ms = info->dwFileVersionMS;
	const uint32_t ls = info->dwFileVersionLS;
	return std::format("{}.{}.{}.{}", HIWORD(ms), LOWORD(ms), HIWORD(ls), LOWORD(ls));
}

const char* SwapEffectName(const DXGI_SWAP_EFFECT effect)
{
	switch (effect)
	{
	case DXGI_SWAP_EFFECT_DISCARD: return "DISCARD";
	case DXGI_SWAP_EFFECT_SEQUENTIAL: return "SEQUENTIAL";
	case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: return "FLIP_SEQUENTIAL";
	case DXGI_SWAP_EFFECT_FLIP_DISCARD: return "FLIP_DISCARD";
	default: return "unknown";
	}
}

std::string SwapChainMetadata(IDXGISwapChain* swapChain)
{
	if (!swapChain)
		return "swap_chain=unavailable\n";
	DXGI_SWAP_CHAIN_DESC desc{};
	if (FAILED(swapChain->GetDesc(&desc)))
		return "swap_chain=GetDesc failed\n";
	return std::format(
		"swap_effect={}\n"
		"buffer_count={}\n"
		"buffer_width={}\n"
		"buffer_height={}\n"
		"format={}\n"
		"swap_flags={}\n"
		"windowed={}\n"
		"refresh_numerator={}\n"
		"refresh_denominator={}\n"
		"sample_count={}\n"
		"sample_quality={}\n",
		SwapEffectName(desc.SwapEffect),
		desc.BufferCount,
		desc.BufferDesc.Width,
		desc.BufferDesc.Height,
		static_cast<unsigned>(desc.BufferDesc.Format),
		desc.Flags,
		desc.Windowed ? 1 : 0,
		desc.BufferDesc.RefreshRate.Numerator,
		desc.BufferDesc.RefreshRate.Denominator,
		desc.SampleDesc.Count,
		desc.SampleDesc.Quality);
}

std::string AdapterMetadata(ID3D11Device* device)
{
	if (!device)
		return "adapter=unavailable\n";
	wil::com_ptr<IDXGIDevice> dxgiDevice;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(dxgiDevice.put()))))
		return "adapter=IDXGIDevice unavailable\n";
	wil::com_ptr<IDXGIAdapter> adapter;
	if (FAILED(dxgiDevice->GetAdapter(adapter.put())))
		return "adapter=GetAdapter failed\n";
	DXGI_ADAPTER_DESC desc{};
	if (FAILED(adapter->GetDesc(&desc)))
		return "adapter=GetDesc failed\n";
	return std::format(
		"adapter_description={}\n"
		"adapter_vendor_id={:#06x}\n"
		"adapter_device_id={:#06x}\n"
		"adapter_revision={}\n"
		"dedicated_video_memory_mb={}\n",
		WideToUtf8(desc.Description),
		desc.VendorId,
		desc.DeviceId,
		desc.Revision,
		desc.DedicatedVideoMemory / (1024u * 1024u));
}

std::string WindowMetadata(const HWND hwnd)
{
	RECT window{};
	GetWindowRect(hwnd, &window);
	HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW info{};
	info.cbSize = sizeof(info);
	const bool haveMonitor = GetMonitorInfoW(monitor, &info) != FALSE;
	std::string text = std::format(
		"window_x={}\nwindow_y={}\nwindow_w={}\nwindow_h={}\n",
		window.left,
		window.top,
		window.right - window.left,
		window.bottom - window.top);
	if (haveMonitor)
	{
		text += std::format(
			"monitor_device={}\nmonitor_x={}\nmonitor_y={}\nmonitor_w={}\nmonitor_h={}\n",
			WideToUtf8(info.szDevice),
			info.rcMonitor.left,
			info.rcMonitor.top,
			info.rcMonitor.right - info.rcMonitor.left,
			info.rcMonitor.bottom - info.rcMonitor.top);
	}
	return text;
}

bool WriteTextFile(const std::wstring& path, const std::string& text, const bool append)
{
	std::ofstream file(path.c_str(), std::ios::binary | (append ? std::ios::app : std::ios::trunc));
	if (!file)
		return false;
	file.write(text.data(), static_cast<std::streamsize>(text.size()));
	return file.good();
}

void WriteMetadataFile()
{
	std::string text = g_capture.metadata;
	text += std::format(
		"recorded_frames={}\ndropped_frames={}\n",
		g_capture.samples.size(),
		g_capture.dropped);
	WriteTextFile(g_capture.metadataPath, text, false);
}

void PushSample(DiagnosticCsvFrame sample)
{
	if (g_capture.samples.size() >= kMaxSamples)
	{
		++g_capture.dropped;
		return;
	}
	g_capture.samples.push_back(std::move(sample));
}

void NoteEvent(const std::string& name)
{
	if (!g_capture.recording)
		return;
	DiagnosticCsvFrame sample;
	const auto now = SteadyClock::now();
	sample.steadyUs = SteadyUs(now);
	sample.qpcUs = QpcNowUs();
	sample.isEvent = true;
	sample.event = name;
	PushSample(std::move(sample));
}
}

bool RenderDiagnostics_IsRecording()
{
	return g_capture.recording;
}

bool RenderDiagnostics_Start(
	const HWND hwnd,
	ID3D11Device* device,
	IDXGISwapChain* swapChain,
	const int configuredRefreshHz,
	const bool vsync,
	const int vsyncInterval,
	const int monitorRefreshHz,
	const int64_t scheduledIntervalUs)
{
	if (g_capture.recording)
		RenderDiagnostics_Stop();

	const std::wstring directory = DiagnosticsDirectory();
	if (directory.empty())
		return false;

	SYSTEMTIME local{};
	GetLocalTime(&local);
	wchar_t stamp[64]{};
	swprintf_s(
		stamp, L"%04u%02u%02u-%02u%02u%02u-%03u",
		local.wYear, local.wMonth, local.wDay,
		local.wHour, local.wMinute, local.wSecond, local.wMilliseconds);
	g_capture = {};
	g_capture.metadataPath = directory + L"\\render-" + stamp + L".txt";
	g_capture.csvPath = directory + L"\\render-" + stamp + L".csv";
	g_capture.steadyOrigin = SteadyClock::now();
	QueryPerformanceFrequency(&g_capture.qpcFrequency);
	QueryPerformanceCounter(&g_capture.qpcOrigin);
	g_capture.samples.reserve(4096);

	std::time_t utc = std::time(nullptr);
	std::tm utcTm{};
	gmtime_s(&utcTm, &utc);
	g_capture.metadata = std::format(
		"capture_started_utc={:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z\n"
		"executable_version={}\n"
		"qpc_frequency={}\n"
		"configured_refresh_hz={}\n"
		"monitor_refresh_hz={}\n"
		"vsync={}\n"
		"vsync_interval={}\n"
		"scheduled_interval_us={}\n"
		"qpc_sample=immediately_after_present\n"
		"steady_clock_us=microseconds_since_capture_start\n",
		utcTm.tm_year + 1900, utcTm.tm_mon + 1, utcTm.tm_mday,
		utcTm.tm_hour, utcTm.tm_min, utcTm.tm_sec,
		ExecutableVersion(),
		g_capture.qpcFrequency.QuadPart,
		configuredRefreshHz,
		monitorRefreshHz,
		vsync ? 1 : 0,
		vsyncInterval,
		scheduledIntervalUs);
	g_capture.metadata += WindowMetadata(hwnd);
	g_capture.metadata += AdapterMetadata(device);
	g_capture.metadata += SwapChainMetadata(swapChain);
	g_capture.recording = true;
	if (!WriteTextFile(g_capture.metadataPath, g_capture.metadata, false))
	{
		g_capture = {};
		return false;
	}
	return true;
}

void RenderDiagnostics_Stop()
{
	if (!g_capture.recording)
		return;
	g_capture.recording = false;

	std::string csv = DiagnosticCsvHeader();
	csv.push_back('\n');
	for (const DiagnosticCsvFrame& sample : g_capture.samples)
	{
		csv += DiagnosticCsvRow(sample);
		csv.push_back('\n');
	}
	WriteTextFile(g_capture.csvPath, csv, false);
	WriteMetadataFile();
	g_capture = {};
}

void RenderDiagnostics_OpenFolder()
{
	const std::wstring directory = DiagnosticsDirectory();
	if (!directory.empty())
		ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void RenderDiagnostics_NoteEarlyWakeup()
{
	if (!g_capture.recording)
		return;
	++g_capture.earlyWakeups;
}

void RenderDiagnostics_BeginFrame(
	const SteadyClock::time_point deadline,
	const SteadyClock::time_point renderStart)
{
	if (!g_capture.recording)
		return;
	g_capture.framePending = true;
	g_capture.frameDeadline = deadline;
	g_capture.frameRenderStart = renderStart;
	g_capture.frameEarlyWakeups = g_capture.earlyWakeups;
	g_capture.earlyWakeups = 0;
}

void RenderDiagnostics_NotePresent(
	const HRESULT presentHr,
	const SteadyClock::time_point presentStart,
	const SteadyClock::time_point presentEnd,
	const SteadyClock::time_point nextDeadline,
	IDXGISwapChain* swapChain)
{
	if (!g_capture.recording)
		return;

	DiagnosticCsvFrame sample;
	sample.steadyUs = SteadyUs(presentEnd);
	sample.qpcUs = QpcNowUs();
	sample.event = "present";
	sample.hasPresentStart = true;
	sample.presentStartUs = SteadyUs(presentStart);
	sample.hasPresentEnd = true;
	sample.presentEndUs = SteadyUs(presentEnd);
	sample.hasNextDeadline = true;
	sample.nextDeadlineUs = SteadyUs(nextDeadline);
	sample.hasPresentBlock = true;
	sample.presentBlockUs = std::chrono::duration_cast<std::chrono::microseconds>(presentEnd - presentStart).count();
	sample.hasPresentHr = true;
	sample.presentHr = static_cast<uint32_t>(presentHr);
	if (g_capture.framePending)
	{
		sample.hasDeadline = true;
		sample.deadlineUs = SteadyUs(g_capture.frameDeadline);
		sample.hasRenderStart = true;
		sample.renderStartUs = SteadyUs(g_capture.frameRenderStart);
		sample.hasRender = true;
		sample.renderUs = std::chrono::duration_cast<std::chrono::microseconds>(
			presentStart - g_capture.frameRenderStart).count();
		sample.hasLateness = true;
		sample.deadlineLatenessUs = presentStart > g_capture.frameDeadline
			? std::chrono::duration_cast<std::chrono::microseconds>(presentStart - g_capture.frameDeadline).count()
			: 0;
		sample.hasEarlyWakeups = true;
		sample.earlyWakeups = g_capture.frameEarlyWakeups;
	}
	if (g_capture.hasPreviousPresent)
	{
		sample.hasInterPresent = true;
		sample.interPresentUs = std::chrono::duration_cast<std::chrono::microseconds>(
			presentStart - g_capture.previousPresentStart).count();
	}
	g_capture.previousPresentStart = presentStart;
	g_capture.hasPreviousPresent = true;
	g_capture.framePending = false;

	if (swapChain)
	{
		UINT lastCount = 0;
		if (SUCCEEDED(swapChain->GetLastPresentCount(&lastCount)))
		{
			sample.hasLastPresentCount = true;
			sample.lastPresentCount = lastCount;
		}
		DXGI_FRAME_STATISTICS stats{};
		if (SUCCEEDED(swapChain->GetFrameStatistics(&stats)))
		{
			sample.statsValid = true;
			sample.statsPresentCount = stats.PresentCount;
			sample.statsPresentRefreshCount = stats.PresentRefreshCount;
			sample.statsSyncRefreshCount = stats.SyncRefreshCount;
			sample.statsSyncQpc = stats.SyncQPCTime.QuadPart;
		}
	}
	PushSample(std::move(sample));
}

void RenderDiagnostics_NoteSettings(
	const int configuredRefreshHz,
	const bool vsync,
	const int vsyncInterval,
	const int64_t scheduledIntervalUs)
{
	NoteEvent(std::format(
		"settings refresh={} vsync={} vsync_interval={} interval_us={}",
		configuredRefreshHz, vsync ? 1 : 0, vsyncInterval, scheduledIntervalUs));
	if (!g_capture.recording)
		return;
	g_capture.metadata += std::format(
		"settings_changed_qpc_us={} configured_refresh_hz={} vsync={} vsync_interval={} scheduled_interval_us={}\n",
		QpcNowUs(), configuredRefreshHz, vsync ? 1 : 0, vsyncInterval, scheduledIntervalUs);
}

void RenderDiagnostics_SetMinimized(const bool minimized)
{
	if (!g_capture.recording || g_capture.sawMinimized == minimized)
		return;
	g_capture.sawMinimized = minimized;
	g_capture.earlyWakeups = 0;
	NoteEvent(minimized ? "minimized" : "restored");
}

void RenderDiagnostics_SetOccluded(const bool occluded)
{
	if (!g_capture.recording || g_capture.sawOccluded == occluded)
		return;
	g_capture.sawOccluded = occluded;
	g_capture.earlyWakeups = 0;
	NoteEvent(occluded ? "occluded" : "visible");
}

void RenderDiagnostics_NoteDeviceRecreated(ID3D11Device* device, IDXGISwapChain* swapChain)
{
	if (!g_capture.recording)
		return;
	NoteEvent("device_recreated");
	g_capture.metadata += std::format("--- device_recreated qpc_us={}\n", QpcNowUs());
	g_capture.metadata += AdapterMetadata(device);
	g_capture.metadata += SwapChainMetadata(swapChain);
	WriteMetadataFile();
}
