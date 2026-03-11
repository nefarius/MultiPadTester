#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <Windows.h>
#include <Shellapi.h>
#include <tchar.h>
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
#include "gamepad_renderer.h"

#define IDM_ABOUT 0xF200
#define IDM_PREFERENCES 0xF210

struct AppPrefs
{
	int refreshRate = 60;  // 0 = monitor default
	bool vsync = true;
};

static std::wstring GetConfigPath()
{
	wchar_t appdata[512]{};
	if (GetEnvironmentVariableW(L"APPDATA", appdata, 512) == 0)
		return {};
	std::wstring dir = std::wstring(appdata) + L"\\MultiPadTester";
	CreateDirectoryW(dir.c_str(), nullptr);
	return dir + L"\\config.ini";
}

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
	}
}

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
static AppPrefs g_prefs;

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
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE,
	_In_ LPWSTR,
	_In_ int)
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

	HWND hwnd = CreateWindow(
		wc.lpszClassName, _T("MultiPad Tester"),
		WS_OVERLAPPEDWINDOW,
		100, 100, 1024, 700,
		nullptr, nullptr, wc.hInstance, nullptr);

	HMENU sysMenu = GetSystemMenu(hwnd, FALSE);
	AppendMenuW(sysMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(sysMenu, MF_STRING, IDM_ABOUT, L"About...");
	AppendMenuW(sysMenu, MF_STRING, IDM_PREFERENCES, L"Preferences...");

	LoadConfig(g_prefs);
	if (!g_d3d.Create(hwnd, g_prefs.refreshRate))
	{
		g_d3d.Cleanup();
		UnregisterClass(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	ShowWindow(hwnd, SW_SHOWDEFAULT);
	UpdateWindow(hwnd);

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
	ImGui_ImplDX11_Init(g_d3d.device, g_d3d.deviceCtx);

	std::vector<std::unique_ptr<IInputBackend>> backends;
	backends.push_back(std::make_unique<XInputBackend>());
	backends.push_back(std::make_unique<RawInputBackend>());
	backends.push_back(std::make_unique<DInputBackend>());
	backends.push_back(std::make_unique<HidApiBackend>());
	g_backends = &backends;

	for (auto& b : backends)
		b->Init(hwnd);

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
			for (auto& b : backends)
			{
				auto slots_view = std::views::iota(0, b->GetMaxSlots());
				int connected = static_cast<int>(std::ranges::count_if(
					slots_view, [&](int i) { return b->GetState(i).connected; }));

				auto tabLabel = std::format("{} ({})###{}", b->GetName(), connected, b->GetName());

				bool tabOpen = ImGui::BeginTabItem(tabLabel.c_str());

				if (tabOpen)
				{
					const char* name = b->GetName();
					const char* description =
						(name == XInputBackend::Name)    ? "Only Xbox-compatible devices will show up here."
						: (name == RawInputBackend::Name) ? "Low-level with medium complexity; many XP-era games use this."
						: (name == DInputBackend::Name)  ? "Legacy API; the oldest available approach; many legacy titles use this."
						: (name == HidApiBackend::Name)  ? "Very verbose but most universal; many modern engines use this."
						: "";
					ImGui::TextWrapped("%s", description);
					ImGui::Spacing();

					struct SlotInfo
					{
						const GamepadState* state;
						int slotIndex;
						const char* backendName;
					};
					std::vector<SlotInfo> slots;
					for (int i = 0; i < b->GetMaxSlots(); ++i)
						if (b->GetState(i).connected)
							slots.push_back({&b->GetState(i), i, b->GetName()});

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

						for (int i = 0; i < total; ++i)
						{
							int col = i % cols;
							int row = i / cols;
							ImVec2 pos(origin.x + col * cellW + pad,
							           origin.y + row * cellH + pad);
							ImVec2 size(cellW - pad * 2, cellH - pad * 2);

							GamepadRenderer::DrawGamepad(dl, pos, size,
							                             *slots[i].state, slots[i].slotIndex,
							                             slots[i].backendName);
						}
					}

					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}

		ImGui::End();

		if (g_showAbout)
		{
			const float aboutMinW = 320.f, aboutMinH = 100.f;
			ImGui::SetNextWindowSizeConstraints(ImVec2(aboutMinW, aboutMinH),
			                                   ImVec2(FLT_MAX, FLT_MAX));
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
			                       ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (ImGui::Begin("About MultiPad Tester", &g_showAbout,
			                 ImGuiWindowFlags_Modal | ImGuiWindowFlags_AlwaysAutoResize |
			                     ImGuiWindowFlags_NoResize))
			{
				ImGui::Text("MultiPad Tester");
				ImGui::Spacing();
				if (ImGui::Button("Open GitHub repository"))
					ShellExecuteW(nullptr, L"open", L"https://github.com/nefarius/MultiPadTester",
					             nullptr, nullptr, SW_SHOWNORMAL);
			}
			ImGui::End();
		}

		if (g_showPreferences)
		{
			const float prefsMinW = 320.f, prefsMinH = 120.f;
			ImGui::SetNextWindowSizeConstraints(ImVec2(prefsMinW, prefsMinH),
			                                    ImVec2(FLT_MAX, FLT_MAX));
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
			                       ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (ImGui::Begin("Preferences", &g_showPreferences,
			                 ImGuiWindowFlags_Modal | ImGuiWindowFlags_AlwaysAutoResize |
			                     ImGuiWindowFlags_NoResize))
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
					g_showPreferences = false;
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(80, 0)))
					g_showPreferences = false;
			}
			ImGui::End();
		}

		ImGui::Render();
		g_d3d.deviceCtx->OMSetRenderTargets(1, &g_d3d.rtv, nullptr);
		g_d3d.deviceCtx->ClearRenderTargetView(g_d3d.rtv, clearColor);
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
