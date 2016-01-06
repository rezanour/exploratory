#include "Precomp.h"
#include "FrameProvider.h"
#include "Debug.h"

bool FrameProvider::Create(std::shared_ptr<FrameProvider>* frameProvider)
{
    frameProvider->reset(new FrameProvider());
    return true;
}

FrameProvider::FrameProvider()
    : _session(PXCSession::CreateInstance())
    , _captureManager(_session->CreateCaptureManager())
{
    assert(_session && _captureManager);
}

FrameProvider::~FrameProvider()
{
    if (_captureManager)
    {
        _captureManager->Release();
        _captureManager = nullptr;
    }

    if (_session)
    {
        _session->Release();
        _session = nullptr;
    }
}
