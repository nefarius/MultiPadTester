#pragma once
#include <d3d11.h>
#include <dxgi.h>

struct D3DContext
{
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* deviceCtx = nullptr;
	IDXGISwapChain* swapChain = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;

	[[nodiscard]] bool Create(HWND hwnd, int refreshRatePreferred = 60);
	void CreateRTV();
	void CleanupRTV();
	void Cleanup();
	void Resize(LPARAM lParam);
	void Present(bool vsync) const;
	void SetRefreshRate(int numerator, int denominator);
};
