#include "d3d_helpers.h"

#include <Windows.h>
#include <dxgi1_4.h>

#include <array>
#include <format>
#include <string>

namespace
{
	const char* SwapEffectName(DXGI_SWAP_EFFECT effect)
	{
		switch (effect)
		{
		case DXGI_SWAP_EFFECT_FLIP_DISCARD: return "FLIP_DISCARD";
		case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL: return "FLIP_SEQUENTIAL";
		case DXGI_SWAP_EFFECT_DISCARD: return "DISCARD";
		default: return "unknown";
		}
	}

	void LogD3d(const std::string& message)
	{
		OutputDebugStringA(message.c_str());
	}
}

bool D3DContext::Create(const HWND hwnd)
{
	// Flip model (Windows 8+) is tried first; legacy DISCARD remains the fallback.
	// A non-zero refresh rate makes FLIP_DISCARD and FLIP_SEQUENTIAL creation fail,
	// and 0/0 is also the correct windowed value for the DISCARD fallback.
	// ALLOW_MODE_SWITCH is omitted because the app never enters exclusive fullscreen
	// and ResizeBuffers is called with flags 0.
	DXGI_SWAP_CHAIN_DESC sd{};
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 0;
	sd.BufferDesc.RefreshRate.Denominator = 0;
	sd.Flags = 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hwnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;

	constexpr std::array effects = {
		DXGI_SWAP_EFFECT_FLIP_DISCARD,
		DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
		DXGI_SWAP_EFFECT_DISCARD,
	};
	constexpr std::array levels = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
	constexpr UINT levelCount = static_cast<UINT>(levels.size());

	HRESULT hr = E_FAIL;
	DXGI_SWAP_EFFECT chosen = DXGI_SWAP_EFFECT_DISCARD;
	for (const DXGI_SWAP_EFFECT effect : effects)
	{
		sd.SwapEffect = effect;
		D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;
		hr = D3D11CreateDeviceAndSwapChain(
			nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			levels.data(), levelCount, D3D11_SDK_VERSION,
			&sd, swapChain.put(), device.put(), &obtained, deviceCtx.put());
		if (hr == DXGI_ERROR_UNSUPPORTED)
		{
			hr = D3D11CreateDeviceAndSwapChain(
				nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
				levels.data(), levelCount, D3D11_SDK_VERSION,
				&sd, swapChain.put(), device.put(), &obtained, deviceCtx.put());
		}
		if (hr == S_OK)
		{
			chosen = effect;
			break;
		}

		// A failed call should already null the outputs; drop anything leftover before the next try.
		Cleanup();
	}

	if (hr != S_OK)
	{
		LogD3d(std::format(
			"D3D11CreateDeviceAndSwapChain failed: {:#010x}\n",
			static_cast<unsigned>(hr)));
		return false;
	}

	hr = CreateRTV();
	if (FAILED(hr))
	{
		LogD3d(std::format(
			"CreateRenderTargetView failed: {:#010x}\n",
			static_cast<unsigned>(hr)));
		Cleanup();
		return false;
	}

	LogD3d(std::format("Created D3D11 swap chain with SwapEffect {}\n", SwapEffectName(chosen)));
	return true;
}

HRESULT D3DContext::CreateRTV()
{
	rtv.reset();
	if (!swapChain || !device)
		return E_FAIL;

	wil::com_ptr<ID3D11Texture2D> back;
	const HRESULT bufferHr = swapChain->GetBuffer(0, IID_PPV_ARGS(back.put()));
	if (FAILED(bufferHr))
		return bufferHr;
	return device->CreateRenderTargetView(back.get(), nullptr, rtv.put());
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

HRESULT D3DContext::Resize(const UINT width, const UINT height)
{
	if (!swapChain || width == 0 || height == 0)
		return E_INVALIDARG;

	CleanupRTV();
	const HRESULT hr = swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr))
		return hr;
	return CreateRTV();
}

HRESULT D3DContext::Present(const bool vsync, const UINT flags) const
{
	if (!swapChain)
		return E_FAIL;
	return swapChain->Present(vsync ? 1u : 0u, flags);
}
