#include "Precomp.h"
#include "Renderer.h"
#include "Debug.h"

std::unique_ptr<Renderer> Renderer::Create(HWND window)
{
    std::unique_ptr<Renderer> renderer(new Renderer(window));
    if (renderer)
    {
        if (renderer->Initialize())
        {
            return renderer;
        }
    }
    return nullptr;
}

Renderer::Renderer(HWND window)
    : Window(window)
{
}

Renderer::~Renderer()
{
}

bool Renderer::Render(bool vsync)
{
    // TODO: Draw stuff
    return Present(vsync);
}

bool Renderer::Initialize()
{
    D3D12_CREATE_DEVICE_FLAG d3dFlag = D3D12_CREATE_DEVICE_NONE;
#if defined(_DEBUG)
    d3dFlag |= D3D12_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D12CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, d3dFlag,
        D3D_FEATURE_LEVEL_11_0, D3D12_SDK_VERSION, IID_PPV_ARGS(&Device));
    if (FAILED(hr))
    {
        // Did it fail because we're requesting the debug layer and it's not present
        // on this machine (and, for D3D12 preview, in the directory of the exe)?
        if (d3dFlag == D3D12_CREATE_DEVICE_DEBUG && hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
        {
            // Try again without debug layer
            d3dFlag &= ~D3D12_CREATE_DEVICE_DEBUG;
            hr = D3D12CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, d3dFlag,
                D3D_FEATURE_LEVEL_11_0, D3D12_SDK_VERSION, IID_PPV_ARGS(&Device));
        }

        if (FAILED(hr))
        {
            LogError(L"Failed to create D3D12 device.");
            return false;
        }
    }

    UINT dxgiFlag = 0;
#if defined(_DEBUG)
    dxgiFlag |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    ComPtr<IDXGIFactory> factory;
    hr = CreateDXGIFactory2(dxgiFlag, IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        LogError(L"Failed to create DXGI factory.");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC scd {};
    scd.BufferCount = 2;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.OutputWindow = Window;
    scd.Windowed = TRUE;

    hr = factory->CreateSwapChain(Device.Get(), &scd, &SwapChain);
    if (FAILED(hr))
    {
        LogError(L"Failed to create swapchain.");
        return false;
    }

    return true;
}

bool Renderer::Present(bool vsync)
{
    HRESULT hr = SwapChain->Present(vsync ? 1 : 0, 0);
    if (FAILED(hr))
    {
        LogError(L"Present failed.");
        return false;
    }
    return true;
}
