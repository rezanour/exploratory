#include "Precomp.h"
#include "Debug.h"
#include "FrameProvider.h"

#include <d3d11.h>
#include <wrl.h>
using namespace Microsoft::WRL;

// Constants
static const wchar_t ClassName[] = L"Vision Experiments Test Application";
static const uint32_t ScreenWidth = 1280;
static const uint32_t ScreenHeight = 720;

// Application variables
static HINSTANCE Instance;
static HWND Window;

// Basic output capabilities
ComPtr<ID3D11Device> Device;
ComPtr<ID3D11DeviceContext> Context;
ComPtr<IDXGISwapChain> SwapChain;
ComPtr<ID3D11Texture2D> BackBuffer;
ComPtr<ID3D11RenderTargetView> BackBufferRTV;

// Local methods
static bool Initialize();
static void Shutdown();
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// Entry point
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    Instance = instance;
    if (!Initialize())
    {
        assert(false);
        return -1;
    }

    ShowWindow(Window, SW_SHOW);
    UpdateWindow(Window);

    std::shared_ptr<FrameProvider> frameProvider;
    if (!FrameProvider::Create(&frameProvider))
    {
        assert(false);
        return -2;
    }

    std::unique_ptr<uint32_t[]> buffer(new uint32_t[640 * 480]);

    // Main loop
    MSG msg {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            // Idle

            // Handle input
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
            {
                // Exit
                break;
            }

            auto frame = frameProvider->GetFrame();
            PXCImage::ImageData imageData{};
            imageData.format = PXCImage::PixelFormat::PIXEL_FORMAT_RGB32;
            imageData.pitches[0] = 640;
            imageData.planes[0] = (pxcBYTE*)buffer.get();

            frame->GetSample()->color->ExportData(&imageData);

            D3D11_BOX box{};
            box.right = 640;
            box.bottom = 480;
            box.back = 1;
            Context->UpdateSubresource(BackBuffer.Get(), 0, &box, imageData.planes[0], imageData.pitches[0] * sizeof(uint32_t), imageData.pitches[0] * sizeof(uint32_t) * 480);

            // TODO: doesn't seem to work. I only get black pixels here! Need to debug

            //frame->GetSample()->color->ReleaseAccess(&imageData);

            SwapChain->Present(1, 0);
        }
    }

    frameProvider = nullptr;
    Shutdown();
    return 0;
}

bool Initialize()
{
    WNDCLASSEX wcx{};
    wcx.cbSize = sizeof(wcx);
    wcx.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wcx.hInstance = Instance;
    wcx.lpfnWndProc = WndProc;
    wcx.lpszClassName = ClassName;

    if (!RegisterClassEx(&wcx))
    {
        LogError(L"Failed to initialize window class.");
        return false;
    }

    DWORD style{ WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX) };

    RECT rc{};
    rc.right = ScreenWidth;
    rc.bottom = ScreenHeight;
    AdjustWindowRect(&rc, style, FALSE);

    Window = CreateWindow(ClassName, ClassName, style,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, Instance, nullptr);

    if (!Window)
    {
        LogError(L"Failed to create window.");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = ScreenWidth;
    scd.BufferDesc.Height = ScreenHeight;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = Window;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE,
        nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &scd, &SwapChain, &Device,
        nullptr, &Context);
    if (FAILED(hr))
    {
        LogError(L"Failed to create D3D device or swapchain.");
        return false;
    }

    hr = SwapChain->GetBuffer(0, IID_PPV_ARGS(&BackBuffer));
    if (FAILED(hr))
    {
        LogError(L"Failed to retrieve the back buffer.");
        return false;
    }

    hr = Device->CreateRenderTargetView(BackBuffer.Get(), nullptr, &BackBufferRTV);
    if (FAILED(hr))
    {
        LogError(L"Failed to create render target view.");
        return false;
    }

    return true;
}

void Shutdown()
{
    BackBufferRTV = nullptr;
    BackBuffer = nullptr;
    SwapChain = nullptr;
    Context = nullptr;
    Device = nullptr;

    DestroyWindow(Window);
    Window = nullptr;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CLOSE:
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
