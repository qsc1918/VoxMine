#include "png.hpp"
#include <windows.h>
#include <wincodec.h>
#include <cstdio>
#include <string>

namespace {
void comCheck(HRESULT hr) {
    // no-op on failure for now (callers rely on false return of the load)
}
} // namespace

bool loadPNG(const char* path, std::vector<uint8_t>& rgba, int& w, int& h) {
    bool ok = false;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    IWICBitmapDecoder* decoder = nullptr;
    std::wstring wpath;
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
        wpath.resize(n > 0 ? n - 1 : 0);
        if (n > 1) MultiByteToWideChar(CP_UTF8, 0, path, -1, &wpath[0], n);
    }
    hr = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) {
        IWICBitmapFrameDecode* frame = nullptr;
        if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
            UINT pw = 0, ph = 0;
            frame->GetSize(&pw, &ph);
            w = (int)pw;
            h = (int)ph;
            std::vector<uint8_t> bgra(pw * ph * 4);
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
            IWICFormatConverter* conv = nullptr;
            if (SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
                SUCCEEDED(conv->Initialize(frame, fmt, WICBitmapDitherTypeNone, nullptr, 0.0,
                                           WICBitmapPaletteTypeCustom))) {
                UINT stride = pw * 4;
                if (SUCCEEDED(conv->CopyPixels(nullptr, stride, (UINT)bgra.size(), bgra.data()))) {
                    rgba.resize(pw * ph * 4);
                    for (UINT i = 0; i < pw * ph; i++) {
                        rgba[i * 4 + 0] = bgra[i * 4 + 2];
                        rgba[i * 4 + 1] = bgra[i * 4 + 1];
                        rgba[i * 4 + 2] = bgra[i * 4 + 0];
                        rgba[i * 4 + 3] = bgra[i * 4 + 3];
                    }
                    ok = true;
                }
                conv->Release();
            }
            frame->Release();
        }
        decoder->Release();
    }
    factory->Release();
    CoUninitialize();
    return ok;
}

bool savePNG(const std::string& path, int w, int h, const std::vector<uint8_t>& bgra) {
    bool ok = false;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return false;

    IWICStream* stream = nullptr;
    if (SUCCEEDED(factory->CreateStream(&stream))) {
        std::wstring wp(path.begin(), path.end());
        if (SUCCEEDED(stream->InitializeFromFilename(wp.c_str(), GENERIC_WRITE))) {
            IWICBitmapEncoder* encoder = nullptr;
            if (SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) {
                if (SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) {
                    IWICBitmapFrameEncode* frame = nullptr;
                    if (SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr))) {
                        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
                        if (SUCCEEDED(frame->Initialize(nullptr)) &&
                            SUCCEEDED(frame->SetSize(w, h)) &&
                            SUCCEEDED(frame->SetPixelFormat(&fmt))) {
                            UINT stride = (UINT)w * 4;
                            if (SUCCEEDED(frame->WritePixels(h, stride, (UINT)bgra.size(),
                                                             const_cast<uint8_t*>(bgra.data()))) &&
                                SUCCEEDED(frame->Commit())) {
                                ok = true;
                            }
                        }
                        frame->Release();
                    }
                    encoder->Commit();
                }
                encoder->Release();
            }
        }
        stream->Release();
    }
    factory->Release();
    CoUninitialize();
    return ok;
}
