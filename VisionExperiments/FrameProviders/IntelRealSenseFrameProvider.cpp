#include "Precomp.h"
#include "IntelRealSenseFrameProvider.h"
#include "Debug.h"

bool IntelRealSenseFrameProvider::Initialize()
{
    _session = PXCSession::CreateInstance();
    if (!_session)
    {
        LogError(L"Failed to create PXC Session. Is RealSense SDK installed?\n");
        return false;
    }

    _senseManager = _session->CreateSenseManager();
    if (!_senseManager)
    {
        LogError(L"Failed to create PXC SenseManager.\n");
        return false;
    }

    pxcStatus status = _senseManager->EnableStream(PXCCapture::StreamType::STREAM_TYPE_COLOR, 640, 480);
    if (status < PXC_STATUS_NO_ERROR)
    {
        LogError(L"PXCSenseManager::EnableStream failed (err %d)\n", status);
        return false;
    }

    status = _senseManager->Init();
    if (status < PXC_STATUS_NO_ERROR)
    {
        // Non-fatal error (don't assert). Could just mean they don't have
        // camera plugged in.
        Log(L"PXCSenseManager::Init failed (err %d).\n", status);
        return false;
    }

    return true;
}

IntelRealSenseFrameProvider::~IntelRealSenseFrameProvider()
{
    if (_senseManager)
    {
        _senseManager->Close();
        _senseManager->Release();
        _senseManager = nullptr;
    }

    if (_session)
    {
        _session->Release();
        _session = nullptr;
    }
}

bool IntelRealSenseFrameProvider::AcquireFrame(Frame* frame)
{
    ZeroMemory(frame, sizeof(Frame));

    // This function blocks until a color sample is ready
    pxcStatus status = _senseManager->AcquireFrame(true);
    if (status < PXC_STATUS_NO_ERROR)
    {
        LogError(L"PXCSenseManager::AcquireFrame failed (err %d).\n", status);
        return false;
    }

    // retrieve the sample
    PXCCapture::Sample* sample = _senseManager->QuerySample();

    PXCImage::ImageData data{};
    status = sample->color->AcquireAccess(PXCImage::ACCESS_READ, &data);
    if (status < PXC_STATUS_NO_ERROR)
    {
        LogError(L"Failed to acquire color sample (err %d).\n", status);
        return false;
    }

    frame->Width = data.pitches[0];
    frame->Height = 480;
    frame->ColorData = data.planes[0];

    return true;
}

void IntelRealSenseFrameProvider::ReleaseFrame(const Frame& frame)
{
    UNREFERENCED_PARAMETER(frame);
    _senseManager->ReleaseFrame();
}
