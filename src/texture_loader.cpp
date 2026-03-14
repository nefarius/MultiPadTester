#include "texture_loader.h"
#include <objbase.h>
#include <wincodec.h>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace {

bool DecodePngToRgba(const void* pngData, size_t pngSize,
                     std::vector<uint8_t>& outRgba, int& outWidth, int& outHeight)
{
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory)
        return false;

    IWICStream* stream = nullptr;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr) || !stream)
    {
        factory->Release();
        return false;
    }

    hr = stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(pngData)),
                                      static_cast<DWORD>(pngSize));
    if (FAILED(hr))
    {
        stream->Release();
        factory->Release();
        return false;
    }

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    stream->Release();
    if (FAILED(hr) || !decoder)
    {
        factory->Release();
        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    decoder->Release();
    if (FAILED(hr) || !frame)
    {
        factory->Release();
        return false;
    }

    UINT width = 0, height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0)
    {
        frame->Release();
        factory->Release();
        return false;
    }

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter)
    {
        frame->Release();
        factory->Release();
        return false;
    }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                               WICBitmapDitherTypeNone, nullptr, 0.f,
                               WICBitmapPaletteTypeCustom);
    frame->Release();
    if (FAILED(hr))
    {
        converter->Release();
        factory->Release();
        return false;
    }

    const size_t stride = static_cast<size_t>(width) * 4u;
    const size_t totalBytes = stride * static_cast<size_t>(height);
    outRgba.resize(totalBytes);

    hr = converter->CopyPixels(nullptr, static_cast<UINT>(stride),
                                static_cast<UINT>(totalBytes), outRgba.data());
    converter->Release();
    factory->Release();

    if (FAILED(hr))
        return false;

    outWidth = static_cast<int>(width);
    outHeight = static_cast<int>(height);
    return true;
}

} // namespace

ID3D11ShaderResourceView* LoadTextureFromPngMemory(ID3D11Device* device,
                                                    const void* pngData,
                                                    size_t pngSize,
                                                    int* outWidth,
                                                    int* outHeight)
{
    if (!device || !pngData || pngSize == 0)
        return nullptr;

    (void)CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::vector<uint8_t> rgba;
    int width = 0, height = 0;
    if (!DecodePngToRgba(pngData, pngSize, rgba, width, height))
        return nullptr;

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

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &initData, &texture);
    if (FAILED(hr) || !texture)
        return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = device->CreateShaderResourceView(texture, &srvDesc, &srv);
    texture->Release();
    if (FAILED(hr) || !srv)
        return nullptr;

    return srv;
}

void ReleaseControllerTexture(ID3D11ShaderResourceView* srv)
{
    if (!srv)
        return;
    ID3D11Resource* resource = nullptr;
    srv->GetResource(&resource);
    srv->Release();
    if (resource)
        resource->Release();
}
