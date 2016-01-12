#include "Precomp.h"
#include "FrameProviders/IntelRealSenseFrameProvider.h"
#include "FrameProviders/SoftwareFrameProvider.h"
#include "Debug.h"

// Public create method for frame provider
bool CreateFrameProvider(bool forceSoftware, std::shared_ptr<FrameProvider>* frameProvider)
{
    frameProvider->reset();

    if (!forceSoftware)
    {
        // Try HW first
        std::shared_ptr<IntelRealSenseFrameProvider> provider = std::make_shared<IntelRealSenseFrameProvider>();
        if (provider->Initialize())
        {
            *frameProvider = std::static_pointer_cast<FrameProvider, IntelRealSenseFrameProvider>(provider);
            return true;
        }
    }

    // Fall back to software provider (no camera)
    std::shared_ptr<SoftwareFrameProvider> provider = std::make_shared<SoftwareFrameProvider>();
    if (provider->Initialize())
    {
        *frameProvider = std::static_pointer_cast<FrameProvider, SoftwareFrameProvider>(provider);
        return true;
    }

    return false;
}
