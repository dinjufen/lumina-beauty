#include "LmImageLoader.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincodec.h>
#include <algorithm>
#include "LmLog.hpp"

namespace {

template <typename T>
void SafeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

std::wstring ToWide(const std::string& utf8) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
    if (length > 1) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
    }
    return wide;
}

} // namespace

bool LmLoadImageRGBA(const std::string& path, int maxSide, std::vector<uint8_t>& rgba, int& width, int& height) {
    // 调用线程可能还没有初始化 COM；已经以别的模式初始化过时返回 RPC_E_CHANGED_MODE，同样可以继续用
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool needUninit = SUCCEEDED(comInit);

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool ok = false;

    do {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory));
        if (FAILED(hr)) break;

        hr = factory->CreateDecoderFromFilename(ToWide(path).c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr)) break;

        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) break;

        UINT srcW = 0;
        UINT srcH = 0;
        hr = frame->GetSize(&srcW, &srcH);
        if (FAILED(hr) || srcW == 0 || srcH == 0) break;

        UINT dstW = srcW;
        UINT dstH = srcH;
        const UINT longest = std::max(srcW, srcH);
        if (maxSide > 0 && longest > static_cast<UINT>(maxSide)) {
            dstW = std::max<UINT>(1, srcW * maxSide / longest);
            dstH = std::max<UINT>(1, srcH * maxSide / longest);
        }

        IWICBitmapSource* source = frame;
        if (dstW != srcW || dstH != srcH) {
            hr = factory->CreateBitmapScaler(&scaler);
            if (FAILED(hr)) break;
            hr = scaler->Initialize(frame, dstW, dstH, WICBitmapInterpolationModeFant);
            if (FAILED(hr)) break;
            source = scaler;
        }

        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) break;
        hr = converter->Initialize(source, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) break;

        rgba.resize(static_cast<size_t>(dstW) * dstH * 4);
        hr = converter->CopyPixels(nullptr, dstW * 4, static_cast<UINT>(rgba.size()), rgba.data());
        if (FAILED(hr)) break;

        width = static_cast<int>(dstW);
        height = static_cast<int>(dstH);
        ok = true;
    } while (false);

    SafeRelease(converter);
    SafeRelease(scaler);
    SafeRelease(frame);
    SafeRelease(decoder);
    SafeRelease(factory);
    if (needUninit) {
        CoUninitialize();
    }

    if (!ok) {
        LM_LOG_E << "Failed to load image " << path;
    }
    return ok;
}
