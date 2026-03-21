#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <Windows.h>
#include <Shellapi.h>
#include <tchar.h>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <format>
#include <fstream>
#include <memory>
#include <numeric>
#include <ranges>
#include <string>
#include <vector>

#include "d3d_helpers.h"
#include "input_backend.h"
#include "xinput_backend.h"
#include "rawinput_backend.h"
#include "dinput_backend.h"
#include "hidapi_backend.h"
#include "wgi_backend.h"
#include "gameinput_backend.h"
#include "gamepad_renderer.h"
#include "sony_layout.h"
#include "texture_loader.h"
#include "hidhide_probe.h"
#include "libwdi_probe.h"
#include "resource.h"
#include "update_check.h"

#define IDM_ABOUT 0xF200
#define IDM_PREFERENCES 0xF210

struct AppPrefs
{
	int refreshRate = 60;  // 0 = monitor default
	bool vsync = true;
	int windowX = 0;
	int windowY = 0;
	int windowW = 0;  // 0 = use default position/size
	int windowH = 0;
	int lastTabIndex = 0;  // backend tab index to restore on launch
	/** UTC Unix seconds when the user dismissed the update dialog; 0 = never. Suppresses checks for 24h. */
	int64_t updateDismissedUnix = 0;
};

/**
 * @brief Get the full path to the application's config file in the user's APPDATA folder.
 *
 * Creates a "MultiPadTester" subdirectory inside the user's APPDATA folder if it does not exist,
 * and returns the path to "config.ini" inside that directory.
 *
 * @return std::wstring The full config file path, or an empty string if the APPDATA environment
 *         variable could not be retrieved.
 */
static std::wstring GetConfigPath()
{
	wchar_t appdata[512]{};
	if (GetEnvironmentVariableW(L"APPDATA", appdata, 512) == 0)
		return {};
	std::wstring dir = std::wstring(appdata) + L"\\MultiPadTester";
	CreateDirectoryW(dir.c_str(), nullptr);
	return dir + L"\\config.ini";
}

/**
 * @brief Loads application preferences from the on-disk config file.
 *
 * Reads the config file located in the application's AppData folder and applies recognized keys
 * from the [Settings] section into the provided AppPrefs structure. Supported keys:
 * RefreshRate, VSync, WindowX, WindowY, WindowW, WindowH, LastTabIndex, UpdateDismissedUnix.
 *
 * - If the config file is missing or unreadable, the function leaves prefs unchanged.
 * - RefreshRate is accepted only if it equals 0, 60, 75, 120, or 144; other values are ignored.
 * - Boolean VSync accepts "1", "true", or "yes" as true.
 * - Integer parsing failures are ignored and do not modify the corresponding field.
 * - If the resulting WindowW or WindowH is less than or equal to zero, both are reset to 0.
 *
 * @param prefs Reference to an AppPrefs instance to populate with values from disk.
 */
static void LoadConfig(AppPrefs& prefs)
{
	std::wstring path = GetConfigPath();
	if (path.empty())
		return;
	std::ifstream f(path.c_str());
	if (!f)
		return;
	bool inSettings = false;
	std::string line;
	while (std::getline(f, line))
	{
		auto trim = [](std::string& s) {
			s.erase(0, s.find_first_not_of(" \t\r\n"));
			s.erase(s.find_last_not_of(" \t\r\n") + 1, s.npos);
		};
		trim(line);
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;
		if (line.size() >= 2 && line[0] == '[')
		{
			inSettings = (line.find("[Settings]") == 0);
			continue;
		}
		if (!inSettings)
			continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = line.substr(0, eq);
		std::string val = line.substr(eq + 1);
		trim(key);
		trim(val);
		if (key == "RefreshRate")
		{
			int v = 60;
			try { v = std::stoi(val); } catch (...) {}
			if (v == 0 || v == 60 || v == 75 || v == 120 || v == 144)
				prefs.refreshRate = v;
		}
		else if (key == "VSync")
		{
			prefs.vsync = (val == "1" || val == "true" || val == "yes");
		}
		else if (key == "WindowX")
		{
			try { prefs.windowX = std::stoi(val); } catch (...) {}
		}
		else if (key == "WindowY")
		{
			try { prefs.windowY = std::stoi(val); } catch (...) {}
		}
		else if (key == "WindowW")
		{
			try { prefs.windowW = std::stoi(val); } catch (...) {}
		}
		else if (key == "WindowH")
		{
			try { prefs.windowH = std::stoi(val); } catch (...) {}
		}
		else if (key == "LastTabIndex")
		{
			try { prefs.lastTabIndex = std::stoi(val); } catch (...) {}
		}
		else if (key == "UpdateDismissedUnix")
		{
			try { prefs.updateDismissedUnix = std::stoll(val); } catch (...) {}
		}
	}
	// Treat invalid dimensions as "not set"
	if (prefs.windowW <= 0 || prefs.windowH <= 0)
		prefs.windowW = prefs.windowH = 0;
}

/**
 * @brief Persists application preferences to the user's configuration file.
 *
 * Writes the AppPrefs values into the application's INI-style config file under a
 * [Settings] section so they can be restored on next launch.
 *
 * @param prefs Preference values to persist. The following fields are written:
 *              - refreshRate: monitor refresh rate selection (0 means monitor default)
 *              - vsync: vertical sync enabled flag
 *              - windowX, windowY, windowW, windowH: saved window position and size
 *              - lastTabIndex: backend tab index to restore on launch
 *              - updateDismissedUnix: UTC Unix time when update dialog was dismissed
 */
static void SaveConfig(const AppPrefs& prefs)
{
	std::wstring path = GetConfigPath();
	if (path.empty())
		return;
	std::ofstream f(path.c_str());
	if (!f)
		return;
	f << "[Settings]\n";
	f << "RefreshRate=" << prefs.refreshRate << "\n";
	f << "VSync=" << (prefs.vsync ? "1" : "0") << "\n";
	f << "WindowX=" << prefs.windowX << "\n";
	f << "WindowY=" << prefs.windowY << "\n";
	f << "WindowW=" << prefs.windowW << "\n";
	f << "WindowH=" << prefs.windowH << "\n";
	f << "LastTabIndex=" << prefs.lastTabIndex << "\n";
	f << "UpdateDismissedUnix=" << prefs.updateDismissedUnix << "\n";
}

/**
 * @brief Determines whether a proposed window rectangle is plausible for display.
 *
 * Ensures the width and height are within allowed bounds and that the window's
 * top-left point lies on a connected monitor.
 *
 * @param x X coordinate of the window's top-left corner in screen pixels.
 * @param y Y coordinate of the window's top-left corner in screen pixels.
 * @param w Width of the window in pixels.
 * @param h Height of the window in pixels.
 * @return true if the size is within 320..4096 and the top-left point is on a monitor, false otherwise.
 */
static bool IsWindowRectPlausible(int x, int y, int w, int h)
{
	constexpr int minSize = 320, maxSize = 4096;
	if (w < minSize || w > maxSize || h < minSize || h > maxSize)
		return false;
	POINT pt = {x, y};
	if (MonitorFromPoint(pt, MONITOR_DEFAULTTONULL) == nullptr)
		return false;
	return true;
}

static void GetMonitorRefreshRate(HWND hwnd, int& numerator, int& denominator)
{
	numerator = 60;
	denominator = 1;
	HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW mi{};
	mi.cbSize = sizeof(mi);
	if (!GetMonitorInfoW(mon, &mi))
		return;
	DEVMODEW dm{};
	dm.dmSize = sizeof(dm);
	if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) == 0)
		return;
	numerator = dm.dmDisplayFrequency;
	denominator = 1;
}

static D3DContext g_d3d;
static std::vector<std::unique_ptr<IInputBackend>>* g_backends = nullptr;
static bool g_showAbout = false;
static bool g_showPreferences = false;
static bool g_showHidHideWarning = false;
static bool g_showHidHideBlockedWarning = false;
static bool g_showLibwdiUsbWarning = false;
static std::vector<std::string> g_libwdiUsbInstanceIdsUtf8;
/** Non-empty if the Zadig USB driver probe failed (enumeration error); instance ID list is not used in that case. */
static std::string g_libwdiUsbProbeErrorUtf8;
static AppPrefs g_prefs;

static std::atomic<HWND> g_updateCheckHwndSlot{nullptr};
static bool g_showUpdateAvailable = false;
static std::string g_updateLocalVerUtf8;
static std::string g_updateRemoteVerUtf8;

static std::string WideToUtf8(const std::wstring_view w)
{
	if (w.empty())
		return {};
	const int n = WideCharToMultiByte(
		CP_UTF8,
		0,
		w.data(),
		static_cast<int>(w.size()),
		nullptr,
		0,
		nullptr,
		nullptr);
	if (n <= 0)
		return {};
	std::string out(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(
		CP_UTF8,
		0,
		w.data(),
		static_cast<int>(w.size()),
		out.data(),
		n,
		nullptr,
		nullptr);
	return out;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
	HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	if (g_backends)
	{
		for (auto& b : *g_backends)
			b->OnWindowMessage(msg, wParam, lParam);
	}

	switch (msg)
	{
	case WM_UPDATE_CHECK_READY:
		{
			std::string loc;
			std::string rem;
			if (UpdateCheck_PopResultForUi(loc, rem))
			{
				g_updateLocalVerUtf8 = std::move(loc);
				g_updateRemoteVerUtf8 = std::move(rem);
				g_showUpdateAvailable = true;
			}
		}
		return 0;
	case WM_SIZE:
		if (g_d3d.device && wParam != SIZE_MINIMIZED)
			g_d3d.Resize(lParam);
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == IDM_ABOUT)
		{
			g_showAbout = true;
			return 0;
		}
		if ((wParam & 0xfff0) == IDM_PREFERENCES)
		{
			g_showPreferences = true;
			return 0;
		}
		if ((wParam & 0xfff0) == SC_KEYMENU)
			return 0;
		break;
	case WM_DESTROY:
		g_updateCheckHwndSlot.store(nullptr, std::memory_order_release);
		{
			RECT r;
			if (GetWindowRect(hWnd, &r))
			{
				g_prefs.windowX = r.left;
				g_prefs.windowY = r.top;
				g_prefs.windowW = r.right - r.left;
				g_prefs.windowH = r.bottom - r.top;
				SaveConfig(g_prefs);
			}
		}
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

/**
 * @brief Initialize application subsystems, run the main event/render loop, and perform shutdown.
 *
 * Sets up DPI awareness, the main window, Direct3D and ImGui backends, and input backends; enters the primary message/poll/render loop that drives the UI and controller visualizations; on exit shuts down ImGui and Direct3D, saves persistent preferences, and unregisters the window class.
 *
 * @param hInstance Handle to the current application instance.
 * @param hPrevInstance Reserved; typically unused.
 * @param lpCmdLine Command line for the application as a Unicode string.
 * @param nCmdShow Controls how the window is to be shown.
 * @return int `0` on normal exit, non-zero on initialization failure (for example, Direct3D creation failure).
 */
int APIENTRY wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE,
	_In_ LPWSTR,
	_In_ int nCmdShow)
{
	ImGui_ImplWin32_EnableDpiAwareness();

	WNDCLASSEX wc{};
	wc.cbSize = sizeof(wc);
	wc.style = CS_CLASSDC;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(hInstance, _T("IDI_APPICON"));
	wc.hIconSm = LoadIcon(hInstance, _T("IDI_APPICON"));
	wc.lpszClassName = _T("MultiPadTesterClass");
	RegisterClassEx(&wc);

	LoadConfig(g_prefs);

	constexpr int defaultX = 100, defaultY = 100, defaultW = 1024, defaultH = 700;
	int x = defaultX, y = defaultY, w = defaultW, h = defaultH;
	if (g_prefs.windowW > 0 && g_prefs.windowH > 0 &&
	    IsWindowRectPlausible(g_prefs.windowX, g_prefs.windowY, g_prefs.windowW, g_prefs.windowH))
	{
		x = g_prefs.windowX;
		y = g_prefs.windowY;
		w = g_prefs.windowW;
		h = g_prefs.windowH;
	}

	HWND hwnd = CreateWindow(
		wc.lpszClassName, _T("MultiPad Tester"),
		WS_OVERLAPPEDWINDOW,
		x, y, w, h,
		nullptr, nullptr, wc.hInstance, nullptr);

	HMENU sysMenu = GetSystemMenu(hwnd, FALSE);
	AppendMenuW(sysMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(sysMenu, MF_STRING, IDM_ABOUT, L"About...");
	AppendMenuW(sysMenu, MF_STRING, IDM_PREFERENCES, L"Preferences...");
	if (!g_d3d.Create(hwnd, g_prefs.refreshRate))
	{
		g_d3d.Cleanup();
		UnregisterClass(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;

	ImGui::StyleColorsDark();

	// Scale font for DPI
	{
		HDC screen = GetDC(nullptr);
		float dpiX = static_cast<float>(GetDeviceCaps(screen, LOGPIXELSX));
		ReleaseDC(nullptr, screen);
		float scale = dpiX / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
		ImFontConfig fc;
		fc.SizePixels = 13.0f * scale;
		io.Fonts->AddFontDefault(&fc);
	}

	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(g_d3d.device.get(), g_d3d.deviceCtx.get());
	switch (GetHidHideStatus())
	{
	case HidHideStatus::InstalledActive:
		g_showHidHideWarning = true;
		break;
	case HidHideStatus::AccessDenied:
		g_showHidHideBlockedWarning = true;
		break;
	default:
		break;
	}

	{
		const LibwdiUsbProbeResult libwdiProbe = ProbeLibwdiUsbDevices();
		if (!libwdiProbe.succeeded)
		{
			g_showLibwdiUsbWarning = true;
			g_libwdiUsbInstanceIdsUtf8.clear();
			g_libwdiUsbProbeErrorUtf8 = WideToUtf8(libwdiProbe.errorMessage);
		}
		else if (!libwdiProbe.instanceIds.empty())
		{
			g_showLibwdiUsbWarning = true;
			g_libwdiUsbProbeErrorUtf8.clear();
			g_libwdiUsbInstanceIdsUtf8.clear();
			g_libwdiUsbInstanceIdsUtf8.reserve(libwdiProbe.instanceIds.size());
			for (const auto& id : libwdiProbe.instanceIds)
				g_libwdiUsbInstanceIdsUtf8.push_back(WideToUtf8(id));
		}
		else
		{
			g_libwdiUsbProbeErrorUtf8.clear();
		}
	}

	wil::com_ptr<ID3D11ShaderResourceView> controllerTextureXbox;
	wil::com_ptr<ID3D11ShaderResourceView> controllerTextureDualSense;
	{
		auto loadBody = [&](int id) -> wil::com_ptr<ID3D11ShaderResourceView> {
			HRSRC hrsrc = FindResourceW(hInstance, MAKEINTRESOURCEW(id), RT_RCDATA);
			if (!hrsrc) return {};
			HGLOBAL hglob = LoadResource(hInstance, hrsrc);
			if (!hglob) return {};
			const void* data = LockResource(hglob);
			const size_t size = SizeofResource(hInstance, hrsrc);
			if (!data || size == 0) return {};
			return LoadTextureFromPngMemory(g_d3d.device.get(), data, size, nullptr, nullptr);
		};
		controllerTextureXbox = loadBody(IDR_XBOX_BODY);
		controllerTextureDualSense = loadBody(IDR_DUALSENSE_BODY);
	}

	std::vector<std::unique_ptr<IInputBackend>> backends;
	backends.push_back(std::make_unique<XInputBackend>());
	backends.push_back(std::make_unique<RawInputBackend>());
	backends.push_back(std::make_unique<DInputBackend>());
	backends.push_back(std::make_unique<HidApiBackend>());
	backends.push_back(std::make_unique<WgiBackend>());
	if (GameInputBackend::IsAvailable())
		backends.push_back(std::make_unique<GameInputBackend>());
	g_backends = &backends;

	for (auto& b : backends)
		b->Init(hwnd);

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	g_updateCheckHwndSlot.store(hwnd, std::memory_order_release);
	StartBackgroundUpdateCheck(hwnd, &g_updateCheckHwndSlot, g_prefs.updateDismissedUnix);

	constexpr float clearColor[4] = {0.06f, 0.06f, 0.07f, 1.0f};

	MSG msg{};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}

		// Poll all backends
		for (auto& b : backends)
			b->Poll();

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::Begin("##Main", nullptr,
		             ImGuiWindowFlags_NoDecoration |
		             ImGuiWindowFlags_NoMove |
		             ImGuiWindowFlags_NoSavedSettings |
		             ImGuiWindowFlags_NoBringToFrontOnFocus);

		if (ImGui::BeginTabBar("##BackendTabs"))
		{
			static bool restoreTabPending = true;
			const int numTabs = static_cast<int>(backends.size());
			const int tabToRestore = (numTabs > 0 && restoreTabPending)
				? std::clamp(g_prefs.lastTabIndex, 0, numTabs - 1) : -1;

			for (int idx = 0; idx < numTabs; ++idx)
			{
				auto& b = backends[idx];
				auto slots_view = std::views::iota(0, b->GetMaxSlots());
				int connected = static_cast<int>(std::ranges::count_if(
					slots_view, [&](int i) { return b->GetState(i).connected; }));

				auto tabLabel = std::format("{} ({})###{}", b->GetName(), connected, b->GetName());

				ImGuiTabItemFlags tabFlags = (idx == tabToRestore) ? ImGuiTabItemFlags_SetSelected : 0;
				bool tabOpen = ImGui::BeginTabItem(tabLabel.c_str(), nullptr, tabFlags);
				if (idx == tabToRestore)
					restoreTabPending = false;

				if (tabOpen)
				{
					g_prefs.lastTabIndex = idx;
					const char* name = b->GetName();
					const char* description =
						(name == XInputBackend::Name)    ? "Extremely simple API; only Xbox-compatible devices will show up here."
						: (name == RawInputBackend::Name) ? "Low-level with medium complexity; many XP-era games use this."
						: (name == DInputBackend::Name)  ? "Legacy API; the oldest available approach; many legacy titles use this."
						: (name == HidApiBackend::Name)  ? "Very verbose but most universal; many modern engines use this."
						: (name == WgiBackend::Name)     ? "Windows Runtime gamepad API; Xbox and compatible devices."
						: (name == GameInputBackend::Name) ? "GDK GameInput; unified controller API, Xbox and HID devices (no Xbox 360)."
						: "";
					ImGui::TextWrapped("%s", description);
					ImGui::Spacing();

					struct SlotInfo
					{
						const GamepadState* state;
						int slotIndex;
						const char* backendName;
						const char* displayName;
						uint16_t vendorId = 0;
						uint16_t productId = 0;
					};
					std::vector<SlotInfo> slots;
					for (int i = 0; i < b->GetMaxSlots(); ++i)
					{
						if (!b->GetState(i).connected)
							continue;
						uint16_t vid = 0, pid = 0;
						b->GetSlotDeviceIds(i, &vid, &pid);
						slots.push_back({&b->GetState(i), i, b->GetName(), b->GetSlotDisplayName(i), vid, pid});
					}

					ImDrawList* dl = ImGui::GetWindowDrawList();
					ImVec2 origin = ImGui::GetCursorScreenPos();
					ImVec2 area = ImGui::GetContentRegionAvail();

					if (slots.empty())
					{
						auto msg = "No controllers detected";
						ImVec2 ts = ImGui::CalcTextSize(msg);
						dl->AddText(
							ImVec2(origin.x + (area.x - ts.x) * 0.5f,
							       origin.y + (area.y - ts.y) * 0.5f),
							IM_COL32(80, 80, 80, 255), msg);
					}
					else
					{
						int total = static_cast<int>(slots.size());
						int cols = (total <= 1) ? 1 : 2;
						int rows = (total + cols - 1) / cols;

						float cellW = area.x / cols;
						float cellH = area.y / rows;
						float pad = 6.0f;

						auto isSonyDevice = [](const char* name) {
							if (!name || !name[0]) return false;
							std::string lower(name);
							for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
							for (const char* sub : {"dualsense", "dualshock", "sony", "playstation"})
								if (lower.find(sub) != std::string::npos) return true;
							return false;
						};

						for (int i = 0; i < total; ++i)
						{
							int col = i % cols;
							int row = i / cols;
							ImVec2 pos(origin.x + col * cellW + pad,
							           origin.y + row * cellH + pad);
							ImVec2 size(cellW - pad * 2, cellH - pad * 2);

							const bool sony = (slots[i].vendorId != 0 || slots[i].productId != 0)
								? IsSonyGamepad(slots[i].vendorId, slots[i].productId)
								: isSonyDevice(slots[i].displayName);
							ImTextureID bodyTex = reinterpret_cast<ImTextureID>((sony ? controllerTextureDualSense : controllerTextureXbox).get());
							GamepadRenderer::LayoutType layoutType = sony ? GamepadRenderer::LayoutType::Sony : GamepadRenderer::LayoutType::Xbox;

							GamepadRenderer::DrawGamepad(dl, pos, size,
							                             *slots[i].state, slots[i].slotIndex,
							                             slots[i].backendName, slots[i].displayName,
							                             bodyTex, ImVec2(400.f, 280.f), layoutType);
						}
					}

					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}

		ImGui::End();

		const char* const kAboutPopupId = "About MultiPad Tester";
		if (g_showAbout)
			ImGui::OpenPopup(kAboutPopupId);

		const bool aboutPopupActive =
			g_showAbout || ImGui::IsPopupOpen(kAboutPopupId, ImGuiPopupFlags_None);
		if (aboutPopupActive)
		{
			const float aboutMinW = 320.f, aboutMinH = 100.f;
			ImGui::SetNextWindowSizeConstraints(ImVec2(aboutMinW, aboutMinH),
			                                    ImVec2(FLT_MAX, FLT_MAX));
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
			                        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		}
		if (ImGui::BeginPopupModal(
			    kAboutPopupId,
			    &g_showAbout,
			    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("MultiPad Tester");
			ImGui::TextWrapped("Gamepad/controller tester and visualizer for Windows, supporting multiple input APIs.");
			ImGui::Spacing();
			ImGui::TextWrapped("MultiPad Tester is a self-contained C++23 Windows desktop application for testing and visualizing gamepad input. It queries four different input backends in parallel and renders a real-time gamepad visualization for every connected controller using Dear ImGui and DirectX 11. The tabbed interface lets you quickly switch between backends and see at a glance how many devices each one detects.");
			ImGui::Spacing();
			ImGui::Text("Copyright (c) 2026 Benjamin Höglinger-Stelzer");
			ImGui::Spacing();
			if (ImGui::Button("Open GitHub repository"))
				ShellExecuteW(nullptr, L"open", L"https://github.com/nefarius/MultiPadTester",
				              nullptr, nullptr, SW_SHOWNORMAL);
			ImGui::EndPopup();
		}

		const char* const kPreferencesPopupId = "Preferences";
		if (g_showPreferences)
			ImGui::OpenPopup(kPreferencesPopupId);

		const bool preferencesPopupActive =
			g_showPreferences || ImGui::IsPopupOpen(kPreferencesPopupId, ImGuiPopupFlags_None);
		if (preferencesPopupActive)
		{
			const float prefsMinW = 320.f, prefsMinH = 120.f;
			ImGui::SetNextWindowSizeConstraints(ImVec2(prefsMinW, prefsMinH),
			                                    ImVec2(FLT_MAX, FLT_MAX));
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
			                        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		}
		if (ImGui::BeginPopupModal(
			    kPreferencesPopupId,
			    &g_showPreferences,
			    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
		{
			static int editRefreshRate = 60;
			static bool editVsync = true;
			if (ImGui::IsWindowAppearing())
			{
				editRefreshRate = g_prefs.refreshRate;
				editVsync = g_prefs.vsync;
			}
			int monitorNum = 60, monitorDen = 1;
			GetMonitorRefreshRate(hwnd, monitorNum, monitorDen);
			static std::string monitorDefaultLabel;
			monitorDefaultLabel = std::format("Monitor default ({} Hz)", monitorNum);
			const char* refreshItems[] = {monitorDefaultLabel.c_str(), "60 Hz", "75 Hz", "120 Hz", "144 Hz"};
			int idx = (editRefreshRate == 0 ? 0 : editRefreshRate == 60 ? 1 : editRefreshRate == 75 ? 2 : editRefreshRate == 120 ? 3 : 4);
			if (ImGui::Combo("Refresh rate", &idx, refreshItems, 5))
				editRefreshRate = (idx == 0 ? 0 : idx == 1 ? 60 : idx == 2 ? 75 : idx == 3 ? 120 : 144);
			ImGui::Checkbox("VSync", &editVsync);
			ImGui::Spacing();
			if (ImGui::Button("OK", ImVec2(80, 0)))
			{
				int num = 60, den = 1;
				if (editRefreshRate == 0)
					GetMonitorRefreshRate(hwnd, num, den);
				else
					num = editRefreshRate;
				g_d3d.SetRefreshRate(num, den);
				g_prefs.refreshRate = editRefreshRate;
				g_prefs.vsync = editVsync;
				SaveConfig(g_prefs);
				ImGui::CloseCurrentPopup();
				g_showPreferences = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80, 0)))
			{
				ImGui::CloseCurrentPopup();
				g_showPreferences = false;
			}
			ImGui::EndPopup();
		}

		const char* const kUpdateAvailablePopupId = "Update available";
		if (g_showUpdateAvailable)
			ImGui::OpenPopup(kUpdateAvailablePopupId);

		const bool updatePopupActive =
			g_showUpdateAvailable || ImGui::IsPopupOpen(kUpdateAvailablePopupId, ImGuiPopupFlags_None);
		if (updatePopupActive)
		{
			const float updateMinW = 400.f, updateMinH = 140.f;
			ImGui::SetNextWindowSizeConstraints(ImVec2(updateMinW, updateMinH),
			                                    ImVec2(FLT_MAX, FLT_MAX));
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
			                        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		}
		if (ImGui::BeginPopupModal(
			    kUpdateAvailablePopupId,
			    &g_showUpdateAvailable,
			    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
		{
			ImGui::TextWrapped("A newer version of MultiPad Tester is available.");
			ImGui::Spacing();
			ImGui::Text("Installed version: %s", g_updateLocalVerUtf8.c_str());
			ImGui::Text("Latest version: %s", g_updateRemoteVerUtf8.c_str());
			ImGui::Spacing();
			if (ImGui::Button("Download update", ImVec2(140, 0)))
			{
				ShellExecuteW(
					nullptr,
					L"open",
					UpdateCheck_GetLatestDownloadUrlW(),
					nullptr,
					nullptr,
					SW_SHOWNORMAL);
			}
			ImGui::SameLine();
			if (ImGui::Button("Not today", ImVec2(130, 0)))
			{
				g_prefs.updateDismissedUnix = static_cast<int64_t>(std::time(nullptr));
				SaveConfig(g_prefs);
				ImGui::CloseCurrentPopup();
				g_showUpdateAvailable = false;
			}
			ImGui::EndPopup();
		}

		const char* const kHidHideActivePopupId = "HidHide Active Warning";
		if (g_showHidHideWarning)
			ImGui::OpenPopup(kHidHideActivePopupId);

		const bool hidHideActivePopupActive =
			g_showHidHideWarning || ImGui::IsPopupOpen(kHidHideActivePopupId, ImGuiPopupFlags_None);
		if (hidHideActivePopupActive)
		{
			const float warningMinW = 460.f, warningMinH = 170.f;
			ImGui::SetNextWindowSizeConstraints(ImVec2(warningMinW, warningMinH),
			                                    ImVec2(FLT_MAX, FLT_MAX));
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
			                        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		}
		if (ImGui::BeginPopupModal(
			    kHidHideActivePopupId,
			    &g_showHidHideWarning,
			    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
		{
			ImGui::TextWrapped("HidHide is installed and currently active on this system.");
			ImGui::Spacing();
			ImGui::TextWrapped("When active, HidHide can hide physical controllers from applications and may skew MultiPad Tester detection and backend comparison results.");
			ImGui::Spacing();
			ImGui::TextWrapped("For most accurate results, disable device hiding in HidHide or uninstall HidHide before testing.");
			ImGui::Spacing();
			if (ImGui::Button("OK", ImVec2(100, 0)))
			{
				ImGui::CloseCurrentPopup();
				g_showHidHideWarning = false;
			}
			ImGui::EndPopup();
		}

		const char* const kHidHideBlockedPopupId = "HidHide Interface Blocked";
		if (g_showHidHideBlockedWarning)
			ImGui::OpenPopup(kHidHideBlockedPopupId);

		const bool hidHideBlockedPopupActive =
			g_showHidHideBlockedWarning || ImGui::IsPopupOpen(kHidHideBlockedPopupId, ImGuiPopupFlags_None);
		if (hidHideBlockedPopupActive)
		{
			const float warningMinW = 500.f, warningMinH = 190.f;
			ImGui::SetNextWindowSizeConstraints(ImVec2(warningMinW, warningMinH),
			                                    ImVec2(FLT_MAX, FLT_MAX));
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
			                        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		}
		if (ImGui::BeginPopupModal(
			    kHidHideBlockedPopupId,
			    &g_showHidHideBlockedWarning,
			    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
		{
			ImGui::TextWrapped("HidHide appears to be installed, but its control interface is currently blocked by another process.");
			ImGui::Spacing();
			ImGui::TextWrapped("HidHide enforces exclusive handle access, so MultiPad Tester could not accurately query whether device hiding is active.");
			ImGui::Spacing();
			ImGui::TextWrapped("For accurate probing and results, close all other applications that may use HidHide (for example the HidHide configuration client) and restart MultiPad Tester.");
			ImGui::Spacing();
			if (ImGui::Button("OK", ImVec2(100, 0)))
			{
				ImGui::CloseCurrentPopup();
				g_showHidHideBlockedWarning = false;
			}
			ImGui::EndPopup();
		}

		// True modal: OpenPopup + BeginPopupModal (ImGuiWindowFlags_Modal on Begin is not a real modal stack)
		const char* const kLibwdiUsbPopupId = "Zadig / libwdi / libusbK / libusb-win32 driver detected";
		if (g_showLibwdiUsbWarning)
			ImGui::OpenPopup(kLibwdiUsbPopupId);

		const bool libwdiPopupActive =
			g_showLibwdiUsbWarning || ImGui::IsPopupOpen(kLibwdiUsbPopupId, ImGuiPopupFlags_None);
		if (libwdiPopupActive)
		{
			const float warningMinW = 520.f, warningMinH = 240.f;
			ImGui::SetNextWindowSizeConstraints(ImVec2(warningMinW, warningMinH),
			                                    ImVec2(FLT_MAX, FLT_MAX));
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
			                        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		}
		if (ImGui::BeginPopupModal(
			    kLibwdiUsbPopupId,
			    &g_showLibwdiUsbWarning,
			    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
		{
			if (!g_libwdiUsbProbeErrorUtf8.empty())
			{
				ImGui::TextWrapped(
					"MultiPad Tester could not enumerate USBDevice / libusbK / libusb-win32 devices setup classes to check for Zadig drivers (expected Provider per class).");
				ImGui::Spacing();
				ImGui::TextUnformatted("Details:");
				ImGui::Spacing();
				ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
				ImGui::TextUnformatted(g_libwdiUsbProbeErrorUtf8.c_str());
				ImGui::PopTextWrapPos();
			}
			else
			{
				ImGui::TextWrapped(
					"At least one matching device was found: USBDevice + libwdi, libusbK devices + libusbk, or libusb-win32 devices + libusb-win32 (Zadig).");
				ImGui::Spacing();
				ImGui::TextWrapped(
					"Those devices are not discoverable by MultiPad Tester through normal gamepad/HID APIs. To have affected controllers detected again, undo the driver replacement in Device Manager (or restore the original driver stack) for those devices.");
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::TextUnformatted("Affected device instance IDs:");
				const float listH = ImGui::GetTextLineHeightWithSpacing() * 8.0f + 8.0f;
				ImGui::BeginChild(
					"##LibwdiInstanceIds",
					ImVec2(0.0f, listH),
					true,
					ImGuiWindowFlags_HorizontalScrollbar);
				for (int i = 0; i < static_cast<int>(g_libwdiUsbInstanceIdsUtf8.size()); ++i)
				{
					ImGui::PushID(i);
					ImGui::Bullet();
					ImGui::SameLine();
					ImGui::TextUnformatted(g_libwdiUsbInstanceIdsUtf8[static_cast<size_t>(i)].c_str());
					ImGui::PopID();
				}
				ImGui::EndChild();
			}
			ImGui::Spacing();
			if (ImGui::Button("OK", ImVec2(100, 0)))
			{
				ImGui::CloseCurrentPopup();
				g_showLibwdiUsbWarning = false;
			}
			ImGui::EndPopup();
		}

		ImGui::Render();
		ID3D11RenderTargetView* rtv = g_d3d.rtv.get();
		g_d3d.deviceCtx->OMSetRenderTargets(1, &rtv, nullptr);
		g_d3d.deviceCtx->ClearRenderTargetView(rtv, clearColor);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		g_d3d.Present(g_prefs.vsync);
	}

	g_backends = nullptr;

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	g_d3d.Cleanup();
	DestroyWindow(hwnd);
	UnregisterClass(wc.lpszClassName, wc.hInstance);

	return 0;
}
