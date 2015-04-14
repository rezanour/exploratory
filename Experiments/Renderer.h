#pragma once

#if defined (ENABLE_DX12_SUPPORT)

// Implements all rendering code, used by the main application
class Renderer
{
public:
    static std::unique_ptr<Renderer> Create(HWND window);
    ~Renderer();

    bool Render(bool vsync);

private:
    Renderer(HWND window);
    Renderer(const Renderer&);
    Renderer& operator= (const Renderer&);

    bool Initialize();
    bool Present(bool vsync);

private:
    HWND Window;
    ComPtr<IDXGISwapChain> SwapChain;
    ComPtr<ID3D12Device> Device;
};

#endif // DX12 support