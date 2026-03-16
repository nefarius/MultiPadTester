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

	[[nodiscard]] bool Create(HWND hwnd, int refreshRatePreferred = 60);
	void CreateRTV();
	void CleanupRTV();
	void Cleanup();
	void Resize(LPARAM lParam);
	void Present(bool vsync) const;
	void SetRefreshRate(int numerator, int denominator);
};
