#pragma once

class GeometryPool;
struct GeoMesh;
struct Object;

// D3D11 based deferred renderer
class DeferredRenderer11
{
    // Types
    enum class GBufferSlice
    {
        LightAccum = 0,             // BackBuffer. RGB = Light, A = Glow factor for bloom.
                                    // Initialize RGB during Geo pass with indirect & constant terms
        Normals,                    // TODO: Move to R16G16 FP16, R=X, G=Y, Z=sqrt(1 - X*X - Y*Y). No neg Z,
                                    // but these are screen space so all point forward anyways
        SpecularRoughness,          // RGB = SpecularColor, A = Roughness
        Color,                      // RGB = Color
        Count,
    };

    enum class PassType
    {
        Geometry = 0,               // Render Geometry into GBuffer
        DirectLighting,             // Render direct light full screen passes
        PointLighting,              // Render point light spheres
        DebugDisplayDepth,          // Dbg rendering of depth buffer
        Count,
    };

public:
    static std::unique_ptr<DeferredRenderer11> Create(HWND window);
    ~DeferredRenderer11();

    bool AddObjects(const std::wstring& contentRoot, const std::wstring& modelFilename);

    bool Render(FXMMATRIX view, FXMMATRIX projection, bool vsync);

private:
    DeferredRenderer11();
    DeferredRenderer11(const DeferredRenderer11&);
    DeferredRenderer11& operator= (const DeferredRenderer11&);

private:
    // Initialization
    bool Initialize(HWND window);
    bool CreateStateObjects();
    bool CreateGBuffer();
    bool InitializePasses();
    bool LoadTexture(const std::wstring& filename, ID3D11ShaderResourceView** srv);

    // Rendering
    void ApplyPass(PassType type, const ComPtr<ID3D11DepthStencilView>& dsv);
    void BindGeometryPool(const std::shared_ptr<GeometryPool>& pool);
    void DrawMesh(const std::shared_ptr<GeoMesh>& mesh);

private:
    // Always nullptr. Used to clear out bindings for clean input->output or output->input transitions
    static ID3D11ShaderResourceView* const  NullSRVs[8];
    static ID3D11RenderTargetView* const    NullRTVs[8];

    // Core objects
    ComPtr<IDXGISwapChain1>         SwapChain;
    ComPtr<ID3D11Device>            Device;
    ComPtr<ID3D11DeviceContext>     Context;
    ComPtr<ID3D11Texture2D>         DepthStencilBuffer;
    ComPtr<ID3D11ShaderResourceView> DepthStencilSRV;
    ComPtr<ID3D11DepthStencilView>  DepthStencilView;

    // State objects
    ComPtr<ID3D11RasterizerState>   RasterizerState;
    ComPtr<ID3D11BlendState>        AdditiveBlendState;
    ComPtr<ID3D11SamplerState>      LinearWrapSampler;
    ComPtr<ID3D11SamplerState>      PointClampSampler;

    // GBuffer resources
    ComPtr<ID3D11Texture2D>         GBuffer[(uint32_t)GBufferSlice::Count];
    ComPtr<ID3D11ShaderResourceView> GBufferSRV[(uint32_t)GBufferSlice::Count];
    ComPtr<ID3D11RenderTargetView>  GBufferRTV[(uint32_t)GBufferSlice::Count];

    // Rendering pass pipline setup
    ComPtr<ID3D11InputLayout>       InputLayout[(uint32_t)PassType::Count];
    ComPtr<ID3D11VertexShader>      VertexShader[(uint32_t)PassType::Count];
    ComPtr<ID3D11PixelShader>       PixelShader[(uint32_t)PassType::Count];

    // These don't hold references to the objects. These are just pointers to existing objects above
    ID3D11ShaderResourceView*       PSShaderResources[(uint32_t)PassType::Count][8];
    ID3D11RenderTargetView*         RenderTargets[(uint32_t)PassType::Count][8];

    uint32_t                        NumShaderResources[(uint32_t)PassType::Count];
    uint32_t                        NumRenderTargets[(uint32_t)PassType::Count];

    std::vector<std::shared_ptr<GeometryPool>>  GeometryPools;
    std::vector<std::shared_ptr<Object>> Objects;

    // Fullscreen quad (Post-projection vertices)
    std::shared_ptr<GeoMesh>        FullscreenQuad;

    ////////////////////////////////////
    // Constant buffers

    // Geometry pass VS constant buffer
    struct GeometryVSConstants
    {
        XMFLOAT4X4 World;
        XMFLOAT4X4 View;
        XMFLOAT4X4 Projection;
    };
    ComPtr<ID3D11Buffer>            GeometryCB;

    // TODO: Improve our janky content loading mechanism
    std::map<std::wstring, ComPtr<ID3D11ShaderResourceView>> CachedTextureMap;
};
