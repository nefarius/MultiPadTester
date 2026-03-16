#include "texture_loader.h"
#include <objbase.h>
#include <wincodec.h>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace {

/**
 * @brief Decodes PNG data from memory into a contiguous 32-bit RGBA pixel buffer.
 *
 * Decodes the PNG image contained in the provided memory block and writes pixel data
 * as 8-bit-per-channel RGBA into outRgba. Also outputs the decoded image width and height.
 *
 * @param pngData Pointer to the PNG data in memory.
 * @param pngSize Size in bytes of the PNG data.
 * @param[out] outRgba Receives the decoded image pixels in row-major order (RGBA8). Resized to hold width * height * 4 bytes.
 * @param[out] outWidth Receives the decoded image width in pixels.
 * @param[out] outHeight Receives the decoded image height in pixels.
 * @return true if decoding succeeded and outputs were populated, false otherwise.
 */
bool DecodePngToRgba(const void* pngData, size_t pngSize,
                     std::vector<uint8_t>& outRgba, int& outWidth, int& outHeight)
{
    wil::com_ptr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.put()));
    if (FAILED(hr) || !factory)
        return false;

    wil::com_ptr<IWICStream> stream;
    hr = factory->CreateStream(stream.put());
    if (FAILED(hr) || !stream)
        return false;

    hr = stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(pngData)),
                                      static_cast<DWORD>(pngSize));
    if (FAILED(hr))
        return false;

    wil::com_ptr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.put());
    if (FAILED(hr) || !decoder)
        return false;

    wil::com_ptr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.put());
    if (FAILED(hr) || !frame)
        return false;

    UINT width = 0, height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0)
        return false;

    wil::com_ptr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.put());
    if (FAILED(hr) || !converter)
        return false;

    hr = converter->Initialize(frame.get(), GUID_WICPixelFormat32bppRGBA,
                               WICBitmapDitherTypeNone, nullptr, 0.f,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
        return false;

    const size_t stride = static_cast<size_t>(width) * 4u;
    const size_t totalBytes = stride * static_cast<size_t>(height);
    outRgba.resize(totalBytes);

    hr = converter->CopyPixels(nullptr, static_cast<UINT>(stride),
                                static_cast<UINT>(totalBytes), outRgba.data());
    if (FAILED(hr))
        return false;

    outWidth = static_cast<int>(width);
    outHeight = static_cast<int>(height);
    return true;
}

}

wil::com_ptr<ID3D11ShaderResourceView> LoadTextureFromPngMemory(ID3D11Device* device,
                                                               const void* pngData,
                                                               size_t pngSize,
                                                               int* outWidth,
                                                               int* outHeight)
{
    wil::com_ptr<ID3D11ShaderResourceView> srv;
    if (!device || !pngData || pngSize == 0)
        return srv;

    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (coHr != S_OK && coHr != S_FALSE)
    {
        if (coHr == RPC_E_CHANGED_MODE)
            OutputDebugStringA("LoadTextureFromPngMemory: COM already initialized with a different threading model (RPC_E_CHANGED_MODE).\n");
        return srv;
    }

    std::vector<uint8_t> rgba;
    int width = 0, height = 0;
    if (!DecodePngToRgba(pngData, pngSize, rgba, width, height))
        return srv;

    if (outWidth)
        *outWidth = width;
    if (outHeight)
        *outHeight = height;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = rgba.data();
    initData.SysMemPitch = static_cast<UINT>(width * 4);
    initData.SysMemSlicePitch = 0;

    wil::com_ptr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&desc, &initData, texture.put());
    if (FAILED(hr) || !texture)
        return srv;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = device->CreateShaderResourceView(texture.get(), &srvDesc, srv.put());
    if (FAILED(hr) || !srv)
        return wil::com_ptr<ID3D11ShaderResourceView>();

    return srv;
}
