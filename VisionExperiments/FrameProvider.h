#pragma once

// Only RGB32 right now. Need to add support for others
struct Frame
{
    uint32_t Width;
    uint32_t Height;
    void*    Data;
};

// Interface for retrieving a stream of frame data.
// The source could be an imaging device (camera, etc...) or playback from
// previously stored database of frames (software device)
struct FrameProvider
{
    virtual ~FrameProvider() {}

    // Returns true if the provider is actual physical hardware.
    // ex: Intel RealSense.
    // Returns false if the provider is a software device.
    virtual bool IsHardware() const = 0;

    // Retrieves the next frame from the provider
    virtual bool AcquireFrame(Frame* frame) = 0;
    virtual void ReleaseFrame(const Frame& frame) = 0;
};

// REVIEW: How do we feel about returning pointer to shared_ptr like this as an
// out parameter? It allows us to return status/return code, but returning the
// shared_ptr directly would be nicer I think.
bool CreateFrameProvider(bool forceSoftware, std::shared_ptr<FrameProvider>* frameProvider);
