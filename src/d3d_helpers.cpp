#include "d3d_helpers.h"
#include <array>

static void GetMonitorRefreshRate(HWND hwnd, UINT& numerator, UINT& denominator)
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

bool D3DContext::Create(const HWND hwnd, const int refreshRatePreferred)
{
	UINT num = 60, den = 1;
	if (refreshRatePreferred == 0)
		GetMonitorRefreshRate(hwnd, num, den);
	else
		num = static_cast<UINT>(refreshRatePreferred);

	DXGI_SWAP_CHAIN_DESC sd{};
	sd.BufferCount = 2;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = num;
	sd.BufferDesc.RefreshRate.Denominator = den;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hwnd;
	sd.SampleDesc.Count = 1;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	constexpr std::array levels = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
	D3D_FEATURE_LEVEL obtained;

	if (D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		levels.data(), levels.size(), D3D11_SDK_VERSION,
		&sd, swapChain.put(), device.put(), &obtained, deviceCtx.put()) != S_OK)
		return false;

	CreateRTV();
	return true;
}

void D3DContext::CreateRTV()
{
	wil::com_ptr<ID3D11Texture2D> back;
	swapChain->GetBuffer(0, IID_PPV_ARGS(back.put()));
	device->CreateRenderTargetView(back.get(), nullptr, rtv.put());
}

void D3DContext::CleanupRTV()
{
	rtv.reset();
}

void D3DContext::Cleanup()
{
	CleanupRTV();
	swapChain.reset();
	deviceCtx.reset();
	device.reset();
}

void D3DContext::Resize(LPARAM lParam)
{
	CleanupRTV();
	swapChain->ResizeBuffers(0,
	                         LOWORD(lParam),
	                         HIWORD(lParam),
	                         DXGI_FORMAT_UNKNOWN, 0);
	CreateRTV();
}

void D3DContext::Present(const bool vsync) const
{
	swapChain->Present(vsync ? 1 : 0, 0);
}

void D3DContext::SetRefreshRate(const int numerator, const int denominator)
{
	DXGI_SWAP_CHAIN_DESC desc{};
	if (swapChain->GetDesc(&desc) != S_OK)
		return;
	DXGI_MODE_DESC mode{};
	mode.Width = desc.BufferDesc.Width;
	mode.Height = desc.BufferDesc.Height;
	mode.RefreshRate.Numerator = numerator;
	mode.RefreshRate.Denominator = denominator;
	mode.Format = desc.BufferDesc.Format;
	mode.ScanlineOrdering = desc.BufferDesc.ScanlineOrdering;
	mode.Scaling = desc.BufferDesc.Scaling;
	swapChain->ResizeTarget(&mode);
}
