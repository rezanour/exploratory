#pragma once

#include "FrameProvider.h"

// Implementation for Intel RealSense R200
class IntelRealSenseFrameProvider : public FrameProvider, NonCopyable
{
public:
    virtual ~IntelRealSenseFrameProvider();

    bool Initialize();

    // FrameProvider interface
    virtual bool IsHardware() const override
    {
        return true;
    }

    virtual bool AcquireFrame(Frame* frame) override;
    virtual void ReleaseFrame(const Frame& frame) override;

private:
    PXCSession* _session = nullptr;
    PXCSenseManager* _senseManager = nullptr;
};

