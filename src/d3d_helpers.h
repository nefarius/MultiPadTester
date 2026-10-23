#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <wil/com.h>

struct D3DContext
{
	wil::com_ptr<ID3D11Device> device;
	wil::com_ptr<ID3D11DeviceContext> deviceCtx;
	wil::com_ptr<IDXGISwapChain> swapChain;
	wil::com_ptr<ID3D11RenderTargetView> rtv;

	// Windowed creation uses refresh 0/0 so flip-model swap chains are accepted.
	[[nodiscard]] bool Create(HWND hwnd);
	[[nodiscard]] HRESULT CreateRTV();
	void CleanupRTV();
	void Cleanup();
	[[nodiscard]] HRESULT Resize(UINT width, UINT height);
	// flags may be DXGI_PRESENT_TEST; pass vsync false for that test.
	// vsyncInterval is used only when vsync is true (1 = every refresh, 2 = every second refresh).
	[[nodiscard]] HRESULT Present(bool vsync, UINT flags = 0, UINT vsyncInterval = 1) const;
};
