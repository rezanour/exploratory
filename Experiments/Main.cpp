#include "Precomp.h"
#include "Debug.h"
#include "Renderer.h"
#include "TestRenderer.h"
#include "ObjModel.h"

// Constants
static const wchar_t ClassName[] = L"Experiments Test Application";
static const uint32_t ScreenWidth = 1280;
static const uint32_t ScreenHeight = 720;
static const float Fov = XMConvertToRadians(90.f);
static const float NearClip = 0.5f;
static const float FarClip = 10000.f;
static const float CameraMoveSpeed = 25.f;
static const float CameraTurnSpeed = 0.0125f;
static const bool VSyncEnabled = true;

// Application variables
static HINSTANCE Instance;
static HWND Window;

// Local methods
static bool Initialize();
static void Shutdown();
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// Entry point
_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    Instance = instance;
    if (!Initialize())
    {
        assert(false);
        return -1;
    }

    std::unique_ptr<ObjModel> objModel(new ObjModel);
    if (!objModel)
    {
        assert(false);
        return -2;
    }

    if (!objModel->Load(L"../Assets/crytek-sponza/sponza.obj"))
    {
        assert(false);
        return -3;
    }

    // Initialize graphics
    std::unique_ptr<TestRenderer> renderer(TestRenderer::Create(Window));
    if (!renderer)
    {
        assert(false);
        return -4;
    }

    if (!renderer->AddMeshes(objModel))
    {
        assert(false);
        return -5;
    }

    ShowWindow(Window, SW_SHOW);
    UpdateWindow(Window);

    // Timing info
    LARGE_INTEGER lastTime {};
    LARGE_INTEGER currTime {};
    LARGE_INTEGER frequency {};
    QueryPerformanceFrequency(&frequency);

    // TODO: Replace with something better as needed

    // Camera info
    XMVECTOR position = XMVectorSet(0.f, 100.f, 1000.f, 1.f);
    XMVECTOR forward = XMVectorSet(-1.f, 0.f, 0.f, 0.f);
    XMVECTOR right = XMVectorSet(-1.f, 0.f, 0.f, 0.f);
    XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
    float yaw = 0.f;
    float pitch = 0.f;

    XMMATRIX projection = XMMatrixPerspectiveFovRH(
        Fov,
        ScreenWidth / (float)ScreenHeight,  // Aspect ratio of window client (rendering) area
        NearClip,
        FarClip);

    wchar_t caption[200] = {};

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
            // Idle, measure time and produce a frame
            QueryPerformanceCounter(&currTime);
            if (lastTime.QuadPart == 0)
            {
                lastTime.QuadPart = currTime.QuadPart;
                continue;
            }

            // Compute time step from last frame until now
            double timeStep = (double)(currTime.QuadPart - lastTime.QuadPart) / (double)frequency.QuadPart;

            // Compute fps
            float frameRate = 1.0f / (float)timeStep;
            lastTime = currTime;

            // Handle input
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
            {
                // Exit
                break;
            }

            // TODO: Replace with something better as needed

            XMVECTOR movement = XMVectorZero();

            if (GetAsyncKeyState('W') & 0x8000)
            {
                movement += forward;
            }
            if (GetAsyncKeyState('A') & 0x8000)
            {
                movement += -right;
            }
            if (GetAsyncKeyState('S') & 0x8000)
            {
                movement += -forward;
            }
            if (GetAsyncKeyState('D') & 0x8000)
            {
                movement += right;
            }

            position += XMVector3Normalize(movement) * CameraMoveSpeed;

            if (GetAsyncKeyState(VK_LEFT) & 0x8000)
            {
                yaw += CameraTurnSpeed;
            }
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
            {
                yaw -= CameraTurnSpeed;
            }
            if (GetAsyncKeyState(VK_UP) & 0x8000)
            {
                pitch += CameraTurnSpeed;
            }
            if (GetAsyncKeyState(VK_DOWN) & 0x8000)
            {
                pitch -= CameraTurnSpeed;
            }

            //forward = XMVector3TransformNormal(XMVectorSet(0.f, 0.f, -1.f, 0.f), XMMatrixRotationX(pitch) * XMMatrixRotationY(yaw));

            // Orthonormalize
            right = XMVector3Cross(forward, up);
            forward = XMVector3Cross(up, right);
            up = XMVector3Cross(right, forward);

            renderer->Render(XMMatrixLookToRH(position, forward, up), projection, VSyncEnabled);

            swprintf_s(caption, L"%s (%dx%d) - FPS: %3.2f", ClassName, ScreenWidth, ScreenHeight, frameRate);
            SetWindowText(Window, caption);
        }
    }

    renderer.reset();
    Shutdown();
    return 0;
}

bool Initialize()
{
    WNDCLASSEX wcx {};
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

    DWORD style { WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX) };

    RECT rc {};
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
