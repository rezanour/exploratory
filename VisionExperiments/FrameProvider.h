#pragma once

class FrameProvider;

class Frame : NonCopyable
{
public:
    Frame(Frame&& other);
    ~Frame();

    PXCCapture::Sample* GetSample() { return _sample; }

private:
    friend class FrameProvider;
    Frame(PXCSenseManager* senseManager, PXCCapture::Sample* sample);

    PXCSenseManager* _senseManager;
    PXCCapture::Sample* _sample;
};

// Interface for retrieving a stream of frame data.
// The source could be an imaging device (camera, etc...) or playback from
// previously stored database of frames.
class FrameProvider : NonCopyable
{
public:
    // REVIEW: How do we feel about returning pointer to shared_ptr like this as an
    // out parameter? It allows us to return status/return code, but returning the
    // shared_ptr directly would be nicer I think.
    static bool Create(std::shared_ptr<FrameProvider>* frameProvider);

    virtual ~FrameProvider();

    Frame GetFrame();

private:
    FrameProvider();

    PXCSession* _session;
    PXCSenseManager* _senseManager;
};
