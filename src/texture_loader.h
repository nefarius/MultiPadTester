#pragma once
#include <d3d11.h>
#include <cstddef>
#include <wil/com.h>

/**
 * Load a PNG image from memory into a D3D11 texture and return its shader resource view.
 * The returned SRV can be used as ImTextureID with ImGui's DX11 backend.
 * Caller owns the returned com_ptr; no separate release needed.
 *
 * @param device D3D11 device to create the texture on.
 * @param pngData Pointer to PNG file bytes (e.g. from RCDATA).
 * @param pngSize Size of the PNG data in bytes.
 * @param outWidth Optional; receives texture width.
 * @param outHeight Optional; receives texture height.
 * @return wil::com_ptr<ID3D11ShaderResourceView> on success, empty on failure.
 */
wil::com_ptr<ID3D11ShaderResourceView> LoadTextureFromPngMemory(ID3D11Device* device,
                                                                const void* pngData,
                                                                size_t pngSize,
                                                                int* outWidth = nullptr,
                                                                int* outHeight = nullptr);
