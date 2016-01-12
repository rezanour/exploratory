#include "Precomp.h"
#include "SoftwareFrameProvider.h"
#include "Debug.h"

bool SoftwareFrameProvider::Initialize()
{
    wchar_t filename[MAX_PATH]{};
    if (!GetModuleFileName(nullptr, filename, _countof(filename)))
    {
        LogError(L"Failed to get module filename. err=%d\n", GetLastError());
        return false;
    }

    // determine location of last \ and then append frame DB
    // relative directory to that.
    wchar_t* p = wcsrchr(filename, L'\\');
    if (!p)
    {
        LogError(L"Path appears to be malformed.");
        return false;
    }

    uint32_t remaining = (uint32_t)(filename + wcslen(filename) - p);
    wcscpy_s(p, remaining, L"\\FrameDB\\*.png");

    _hFindFile = FindFirstFile(filename, &_findData);
    if (_hFindFile == INVALID_HANDLE_VALUE)
    {
        LogError(L"Failed to find any images in frame DB. err=%d\n", GetLastError());
        return false;
    }

    _width = 640;
    _height = 480;
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
        return false;
    }

    // fill image with a solid color for testing
    static int frameIndex = 0;
    uint32_t color = (frameIndex++ % 2) ? 0xFF000099 : 0xFF000055;
    uint32_t size = _width * _height;
    for (uint32_t i = 0; i < size; ++i)
    {
        _image[i] = color;
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
