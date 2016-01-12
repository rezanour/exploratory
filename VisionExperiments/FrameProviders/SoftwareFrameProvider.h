#pragma once

#include "FrameProvider.h"

// Implementation for Software (no camera) provider
class SoftwareFrameProvider : public FrameProvider, NonCopyable
{
public:
    virtual ~SoftwareFrameProvider();

    bool Initialize();

    // FrameProvider interface
    virtual bool IsHardware() const override
    {
        return false;
    }

    virtual bool AcquireFrame(Frame* frame) override;
    virtual void ReleaseFrame(const Frame& frame) override;

private:
    // search files in frame DB location
    HANDLE _hFindFile = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATA _findData{};

    // reuse the same memory for each frame
    std::unique_ptr<uint32_t[]> _image;
    uint32_t _width = 0;
    uint32_t _height = 0;
};
