#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <chrono>
#include <cstdint>

// Main-thread capture of presentation timing. Disabled calls return immediately
// and do not touch the filesystem.
[[nodiscard]] bool RenderDiagnostics_IsRecording();

[[nodiscard]] bool RenderDiagnostics_Start(
	HWND hwnd,
	ID3D11Device* device,
	IDXGISwapChain* swapChain,
	int configuredRefreshHz,
	bool vsync,
	int monitorRefreshHz,
	int64_t scheduledIntervalUs);

void RenderDiagnostics_Stop();
void RenderDiagnostics_OpenFolder();
void RenderDiagnostics_NoteEarlyWakeup();
void RenderDiagnostics_BeginFrame(
	std::chrono::steady_clock::time_point deadline,
	std::chrono::steady_clock::time_point renderStart);
void RenderDiagnostics_NotePresent(
	HRESULT presentHr,
	std::chrono::steady_clock::time_point presentStart,
	std::chrono::steady_clock::time_point presentEnd,
	std::chrono::steady_clock::time_point nextDeadline,
	IDXGISwapChain* swapChain);
void RenderDiagnostics_NoteSettings(int configuredRefreshHz, bool vsync, int64_t scheduledIntervalUs);
void RenderDiagnostics_SetMinimized(bool minimized);
void RenderDiagnostics_SetOccluded(bool occluded);
void RenderDiagnostics_NoteDeviceRecreated(ID3D11Device* device, IDXGISwapChain* swapChain);
