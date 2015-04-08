#pragma once

struct ObjModel;

// Debug D3D11 Renderer for quick tests/validation
class TestRenderer
{
    struct Constants
    {
        XMFLOAT4X4 World;
        XMFLOAT4X4 ViewProjection;
    };

    struct Vertex
    {
        XMFLOAT3 Position;
        XMFLOAT3 Normal;
        XMFLOAT2 TexCoord;
    };

    struct Mesh
    {
        uint32_t StartIndex;
        uint32_t NumIndices;
        ComPtr<ID3D11ShaderResourceView> SRV;
    };

    struct Object
    {
        XMFLOAT4X4 World;
        std::string Name;
        std::vector<Mesh> Meshes;
    };

    struct Scene
    {
        ComPtr<ID3D11Buffer> VertexBuffer;
        ComPtr<ID3D11Buffer> IndexBuffer;
        uint32_t VertexCount;
        uint32_t IndexCount;
        std::vector<std::shared_ptr<Object>> Objects;
    };

public:
    static std::unique_ptr<TestRenderer> Create(HWND window);
    ~TestRenderer();

    bool AddMeshes(const std::wstring& contentRoot, const std::wstring& modelFilename);

    bool Render(FXMMATRIX view, FXMMATRIX projection, bool vsync);

private:
    TestRenderer(HWND window);
    TestRenderer(const TestRenderer&);
    TestRenderer& operator= (const TestRenderer&);

    bool Initialize();
    bool LoadTexture(const std::wstring& filename, ID3D11ShaderResourceView** srv);

    void Clear();
    bool Present(bool vsync);

private:
    HWND Window;
    ComPtr<IDXGISwapChain> SwapChain;
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11RenderTargetView> RenderTarget;
    ComPtr<ID3D11DepthStencilView> DepthBuffer;
    ComPtr<ID3D11InputLayout> InputLayout;
    ComPtr<ID3D11Buffer> ConstantBuffer;
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11RasterizerState> RasterizerState;
    ComPtr<ID3D11SamplerState> Sampler;

    std::shared_ptr<Scene> TheScene;
};
