#pragma once

#if defined (ENABLE_DX12_SUPPORT)

// Implements all rendering code, used by the main application
class Renderer
{
public:
    static std::unique_ptr<Renderer> Create(HWND window);
    ~Renderer();

    Renderer(HWND window);
    Renderer(const Renderer&);
    Renderer& operator= (const Renderer&);

    bool Initialize();
    bool Render(FXMVECTOR cameraPosition, FXMMATRIX view, FXMMATRIX projection, bool vsync);
    bool AddMeshes(const std::wstring& contentRoot, const std::wstring& modelFilename);

private:
    struct Constants
    {
        XMFLOAT4X4 World;
        XMFLOAT4X4 ViewProjection;
        float NearClip;
        float FarClip;
        XMFLOAT2 Pad;
    };

    struct Vertex
    {
        XMFLOAT3 Position;
        XMFLOAT3 Normal;
        XMFLOAT3 Tangent;
        XMFLOAT3 BiTangent;
        XMFLOAT2 TexCoord;
    };

    struct Mesh
    {
        uint32_t StartIndex;
        uint32_t NumIndices;
        ComPtr<ID3D12Resource> AlbedoTex;
        ComPtr<ID3D12Resource> BumpDerivativeTex;
        ComPtr<ID3D12Resource> SpecularTex;
        UINT AlbedoDescIdx;
        UINT BumpDerivativeDescIdx;
        UINT SpecularDescIdx;
    };

    struct Object
    {
        XMFLOAT4X4 World;
        ComPtr<ID3D12Resource> ConstantBuffers[2];
        std::string Name;
        std::vector<Mesh> Meshes;
    };

    struct Scene
    {
        ComPtr<ID3D12Resource> VertexBuffer;
        ComPtr<ID3D12Resource> IndexBuffer;
        uint32_t VertexCount;
        uint32_t IndexCount;
        D3D12_VERTEX_BUFFER_VIEW VtxBufView;
        D3D12_INDEX_BUFFER_VIEW IdxBufView;
        std::vector<std::shared_ptr<Object>> Objects;
    };

    static const uint32_t MAX_LIGHTS = 8;

    struct Light
    {
        XMFLOAT3 Direction;
        float Pad0;
        XMFLOAT3 Color;
        float Pad1;
    };

    struct PointLight
    {
        XMFLOAT3 Position;
        float Pad0;
        XMFLOAT3 Color;
        float Radius;
    };

    struct LightConstants
    {
        Light Lights[MAX_LIGHTS];
        PointLight PointLights[MAX_LIGHTS];
        XMFLOAT3 EyePosition;
        int NumLights;
        int NumPointLights;
        XMFLOAT3 Pad;
    };

    bool Present(bool vsync);
    bool CreateUploadBuffer(const void* pData, size_t size, ID3D12Resource** ppBuf);
    bool CreateBuffer(const void* pData, size_t size, ID3D12Resource** ppTempBuf, ID3D12Resource** ppFinalBuf);
    bool CreateTexture2D(const void* pData, size_t size, DXGI_FORMAT format, UINT width, UINT height, UINT16 arraySize, UINT16 mipLevels, ID3D12Resource** ppTempTex, ID3D12Resource** ppFinalTex);
    bool LoadTexture(const std::wstring& filename, ID3D12Resource** ppTempTex, ID3D12Resource** ppFinalTex, UINT* pDescHandle);
    void SetResourceBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter);
    UINT CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);

    HWND Window;
    ComPtr<IDXGISwapChain> SwapChain;
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandAllocator> CmdAllocators[2];
    ComPtr<ID3D12CommandQueue> DefaultQueue;
    ComPtr<ID3D12GraphicsCommandList> CmdLists[2];
    ComPtr<ID3D12PipelineState> PipelineStates[1];
    ComPtr<ID3D12DescriptorHeap> ShaderResourceDescHeap;
    ComPtr<ID3D12DescriptorHeap> DepthStencilDescHeap;
    ComPtr<ID3D12DescriptorHeap> RenderTargetDescHeap;
    ComPtr<ID3D12DescriptorHeap> SamplerDescHeap;
    ComPtr<ID3D12Fence> RenderFence;
    ComPtr<ID3D12Resource> BackBuffer;
    ComPtr<ID3D12Resource> DepthBuffer;
    D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
    ComPtr<ID3D12RootSignature> RootSignature;
    HANDLE RenderedEvent;
    UINT BackBufferIdx;
    UINT64 RenderFenceIdx;
    std::unordered_map<std::wstring, std::pair<ID3D12Resource*, UINT>> LoadedTextureMaps;

    std::shared_ptr<Scene> TheScene;
    ComPtr<ID3D12Resource> GlobalConstantBuffers[2];
    UINT GlobalConstantDescOffsets[2];

    D3D12_CPU_DESCRIPTOR_HANDLE ShaderResourceDescHandle;
    UINT DescIncrementSize;
};

#endif // DX12 support
