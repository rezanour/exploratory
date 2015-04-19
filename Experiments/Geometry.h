#pragma once

// Types of vertex layouts
enum class VertexType
{
    Standard = 0,
    Count
};

// Quick lookup for vertex stride per type, and input elements
extern const uint32_t VertexStride[(uint32_t)VertexType::Count];
extern const D3D11_INPUT_ELEMENT_DESC VertexElements[(uint32_t)VertexType::Count][16];
extern const uint32_t VertexElementCount[(uint32_t)VertexType::Count];

// Standard vertex type used by most things.
// TODO: We could probably optimize this a bit if we cared.
// I'm too lazy for that right now
struct StandardVertex
{
    XMFLOAT3 Position;
    XMFLOAT3 Normal;
    XMFLOAT3 Tangent;
    XMFLOAT3 BiTangent;
    XMFLOAT2 TexCoord;
};

// A GeometryPool is a single (large) chunk of vertex buffer & index buffer memory
// that can be shared by many meshes. This single chunk is homogenous in vertex type,
// and is paged in/out as a unit by the OS (so batch accordingly to avoid thrash).
class GeometryPool :
    public std::enable_shared_from_this<GeometryPool>,
    public NonCopyable
{
public:
    static std::shared_ptr<GeometryPool> Create(const ComPtr<ID3D11Device>& device, VertexType type, uint32_t vertexCapacity, uint32_t indexCapacity);

    VertexType GetType() const { return Type; }

    uint64_t GetEstVRAMBytes() const
    {
        return 
            VertexStride[(uint32_t)Type] * (uint64_t)VertexCapacity +
            sizeof(uint32_t) * (uint64_t)IndexCapacity;
    }

    // Try to reserve a chunk of the buffer. If successful, returns base vertex & index of the range.
    bool ReserveRange(uint32_t vertexCount, uint32_t indexCount, uint32_t* baseVertex, uint32_t* baseIndex);

    const ComPtr<ID3D11Buffer>& GetVertexBuffer() const { return VertexBuffer; }
    const ComPtr<ID3D11Buffer>& GetIndexBuffer() const { return IndexBuffer; }

private:
    GeometryPool(VertexType type);

    bool Initialize(const ComPtr<ID3D11Device>& device, uint32_t vertexCapacity, uint32_t indexCapacity);

    VertexType Type;
    ComPtr<ID3D11Buffer> VertexBuffer;
    ComPtr<ID3D11Buffer> IndexBuffer;

    uint32_t VertexCount;
    uint32_t VertexCapacity;
    uint32_t IndexCount;
    uint32_t IndexCapacity;
};

// A GeoMesh is a reference to a vertex buffer & index buffer,
// with range info on what portion of that buffer to use.
struct GeoMesh
{
    std::shared_ptr<GeometryPool> Pool;
    uint32_t BaseVertex;
    uint32_t NumVertices;
    uint32_t BaseIndex;
    uint32_t NumIndices;
};
