#include "Precomp.h"
#include "SoftwareFrameProvider.h"
#include "Debug.h"

#include <wincodec.h>   // WIC. Used for image loading
#include <wrl.h>
using Microsoft::WRL::ComPtr;

bool SoftwareFrameProvider::Initialize()
{
    // Initialize MTA, if it's not already. We pick MTA because:
    // 1. WIC supports it
    // 2. It allows us to keep SoftwareFrameProvider free threaded if we want
    // 3. Shouldn't (in theory) conflict with any STA the caller may have established
    //    on this thread.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        LogError(L"Failed to initialize COM. err=0x%08x\n", hr);
        return false;
    }

    // Create WIC Factory
    hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&_WICFactory));
    if (FAILED(hr))
    {
        CoUninitialize();

        LogError(L"Failed to create WIC imaging factory. err=0x%08x\n", hr);
        return false;
    }

    // TODO: For now, the code assumes the FrameDB directory
    // is relative to the current working directory.
    // We should eventually allow configuring the root path.

    wchar_t frameSearchString[MAX_PATH]{};
    if (!GetCurrentDirectory(_countof(frameSearchString), frameSearchString))
    {
        LogError(L"Failed to get current directory. err=%d\n", GetLastError());
        return false;
    }

    // If the path doesn't have a trailing slash, add one
    if (frameSearchString[wcslen(frameSearchString) - 1] != L'\\')
    {
        wcscat_s(frameSearchString, L"\\");
    }

    wcscat_s(frameSearchString, L"FrameDB\\");

    _frameDBRoot.assign(frameSearchString);

    // TODO: just picked png arbitrarily. We should decide on
    // format later.
    wcscat_s(frameSearchString, L"*.png");

    _hFindFile = FindFirstFile(frameSearchString, &_findData);
    if (_hFindFile == INVALID_HANDLE_VALUE)
    {
        LogError(L"Failed to find any images in frame DB. err=%d\n", GetLastError());
        return false;
    }

    // Open first file to determine image properties.
    // We require all subsequent frames to match.
    wchar_t absoluteFilename[MAX_PATH];
    swprintf_s(absoluteFilename, L"%s%s", _frameDBRoot.c_str(), _findData.cFileName);

    ComPtr<IWICBitmapDecoder> decoder;
    hr = _WICFactory->CreateDecoderFromFilename(absoluteFilename, nullptr,
        GENERIC_READ, WICDecodeOptions::WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr))
    {
        LogError(L"Failed to decode frame %s. err=0x%08x\n", _findData.cFileName, hr);
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr))
    {
        LogError(L"Failed to decode frame %s. err=0x%08x\n", _findData.cFileName, hr);
        return false;
    }

    hr = frame->GetSize(&_width, &_height);
    if (FAILED(hr))
    {
        LogError(L"Failed to read information from %s. err=0x%08x\n", _findData.cFileName, hr);
        return false;
    }

    _image.reset(new uint32_t[_width * _height]);
    return true;
}

SoftwareFrameProvider::~SoftwareFrameProvider()
{
    if (_hFindFile != INVALID_HANDLE_VALUE)
    {
        FindClose(_hFindFile);
        _hFindFile = INVALID_HANDLE_VALUE;
    }

    if (_WICFactory)
    {
        _WICFactory->Release();
        _WICFactory = nullptr;
        CoUninitialize();
    }
}

bool SoftwareFrameProvider::AcquireFrame(Frame* frame)
{
    // Always recycle same memory
    frame->Width = _width;
    frame->Height = _height;
    frame->ColorData = _image.get();

    if (_findData.cFileName[0] == 0)
    {
        // no more frames
        return true;
    }

    wchar_t absoluteFilename[MAX_PATH];
    swprintf_s(absoluteFilename, L"%s%s", _frameDBRoot.c_str(), _findData.cFileName);

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = _WICFactory->CreateDecoderFromFilename(absoluteFilename, nullptr,
        GENERIC_READ, WICDecodeOptions::WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr))
    {
        LogError(L"Failed to decode frame %s. err=0x%08x\n", _findData.cFileName, hr);
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> decodeFrame;
    hr = decoder->GetFrame(0, &decodeFrame);
    if (FAILED(hr))
    {
        LogError(L"Failed to decode frame %s. err=0x%08x\n", _findData.cFileName, hr);
        return false;
    }

    uint32_t width, height;
    hr = decodeFrame->GetSize(&width, &height);
    if (FAILED(hr))
    {
        LogError(L"Failed to read information from %s. err=0x%08x\n", _findData.cFileName, hr);
        return false;
    }

    if (width != _width || height != _height)
    {
        LogError(L"Inconsistent frame found. %s. err=0x%08x\n", _findData.cFileName, hr);
        return false;
    }

    hr = decodeFrame->CopyPixels(nullptr, sizeof(uint32_t) * _width,
        sizeof(uint32_t) * _width * _height, reinterpret_cast<BYTE*>(_image.get()));
    if (FAILED(hr))
    {
        LogError(L"Failed decoding frame. %s. err=0x%08x\n", _findData.cFileName, hr);
        return false;
    }

    // Find next file in database. If none found,
    // clear cFileName so we know on the next request.
    if (!FindNextFile(_hFindFile, &_findData))
    {
        _findData.cFileName[0] = 0;
    }

    return true;
}

void SoftwareFrameProvider::ReleaseFrame(const Frame& frame)
{
    assert(frame.ColorData == _image.get());
    UNREFERENCED_PARAMETER(frame);
}
