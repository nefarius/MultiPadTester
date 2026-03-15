#pragma once
#include <d3d11.h>
#include <cstddef>

/**
 * Load a PNG image from memory into a D3D11 texture and return its shader resource view.
 * The returned SRV can be used as ImTextureID with ImGui's DX11 backend.
 * Caller must Release() the returned SRV and the texture when done (see ReleaseControllerTexture).
 *
 * @param device D3D11 device to create the texture on.
 * @param pngData Pointer to PNG file bytes (e.g. from RCDATA).
 * @param pngSize Size of the PNG data in bytes.
 * @param outWidth Optional; receives texture width.
 * @param outHeight Optional; receives texture height.
 * @return ID3D11ShaderResourceView* on success, nullptr on failure. Caller must Release().
 */
ID3D11ShaderResourceView* LoadTextureFromPngMemory(ID3D11Device* device,
                                                  const void* pngData,
                                                  size_t pngSize,
                                                  int* outWidth = nullptr,
                                                  int* outHeight = nullptr);

/**
 * Release the SRV and underlying texture created by LoadTextureFromPngMemory.
 * Pass the same pointer returned by LoadTextureFromPngMemory; safe to call with nullptr.
 */
void ReleaseControllerTexture(ID3D11ShaderResourceView* srv);
