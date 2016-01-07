#include "Precomp.h"
#include "FrameProvider.h"
#include "Debug.h"

Frame::Frame(PXCSenseManager* senseManager, PXCCapture::Sample* sample)
    : _senseManager(senseManager), _sample(sample)
{
}

Frame::Frame(Frame&& other)
    : _senseManager(other._senseManager), _sample(other_sample)
{
    other._senseManager = nullptr;
    other._sample = nullptr;
}

Frame::~Frame()
{
    if (_senseManager)
    {
        _senseManager->ReleaseFrame();
    }
    _senseManager = nullptr;
    _sample = nullptr;
}

bool FrameProvider::Create(std::shared_ptr<FrameProvider>* frameProvider)
{
    frameProvider->reset(new FrameProvider());
    return true;
}

FrameProvider::FrameProvider()
    : _session(PXCSession::CreateInstance())
    , _senseManager(_session->CreateSenseManager())
{
    assert(_session && _senseManager);

    _senseManager->EnableStream(PXCCapture::StreamType::STREAM_TYPE_COLOR, 640, 480);
    _senseManager->Init();
}

Frame FrameProvider::GetFrame()
{
    // This function blocks until a color sample is ready
    if (_senseManager->AcquireFrame(true) < PXC_STATUS_NO_ERROR)
    {
        assert(false);
        return nullptr;
    }

    // retrieve the sample
    return Frame(_senseManager, _senseManager->QuerySample());
}

FrameProvider::~FrameProvider()
{
    if (_senseManager)
    {
        _senseManager->Release();
        _senseManager = nullptr;
    }

    if (_session)
    {
        _session->Release();
        _session = nullptr;
    }
}
