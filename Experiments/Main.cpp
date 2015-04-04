#include "Precomp.h"
#include "Debug.h"

// Constants
static const wchar_t ClassName[] = L"Experiments Test Application";
static const uint32_t ScreenWidth = 1280;
static const uint32_t ScreenHeight = 720;
static const float Fov = XMConvertToRadians(90.f);
static const float NearClip = 0.5f;
static const float FarClip = 100.f;
static const float CameraMoveSpeed = 0.125f;
static bool VSyncEnabled = true;

// Application variables
static HINSTANCE Instance;
static HWND Window;

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

    // TODO: Initialize graphics here

    ShowWindow(Window, SW_SHOW);
    UpdateWindow(Window);

    // Timing info
    LARGE_INTEGER lastTime = {};
    LARGE_INTEGER currTime = {};
    LARGE_INTEGER frequency = {};
    QueryPerformanceFrequency(&frequency);

    // TODO: Replace with something better as needed

    // Camera info
    XMMATRIX view = XMMatrixLookToRH(
        XMVectorSet(0.f, 0.f, 5.f, 1.f),    // Camera Position 5 units along Z
        XMVectorSet(0.f, 0.f, -1.f, 0.f),   // Looking back along -Z towards origin
        XMVectorSet(0.f, 1.f, 0.f, 0.f));   // Up

    XMMATRIX projection = XMMatrixPerspectiveFovRH(
        Fov,
        ScreenWidth / (float)ScreenHeight,  // Aspect ratio of window client (rendering) area
        NearClip,
        FarClip);

    wchar_t caption[200] = {};

    // Main loop
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            // Idle, measure time and produce a frame
            QueryPerformanceCounter(&currTime);
            if (lastTime.QuadPart == 0)
            {
                lastTime.QuadPart = currTime.QuadPart;
                continue;
            }

            // Compute time step from last frame until now
            double timeStep = (double)(currTime.QuadPart - lastTime.QuadPart) / (double)frequency.QuadPart;

            // TODO: Artificial throttling to ~60fps until we put in rendering. Keeps
            // DWM from going crazy from all the SetWindowText calls
            if (timeStep < 0.01666)
            {
                // Not an accurate sleep, but good enough for artificial throttling
                ::Sleep((int)(16 - timeStep * 1000));
                continue;
            }

            // Compute fps
            float frameRate = 1.0f / (float)timeStep;
            lastTime = currTime;

            // Handle input
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
            {
                // Exit
                break;
            }

            // TODO: Render something!

            swprintf_s(caption, L"%s (%dx%d)- FPS: %3.2f", ClassName, ScreenWidth, ScreenHeight, frameRate);
            SetWindowText(Window, caption);
        }
    }

    Shutdown();
    return 0;
}

bool Initialize()
{
    WNDCLASSEX wcx = {};
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

    DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

    RECT rc = {};
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

    return true;
}

void Shutdown()
{
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
