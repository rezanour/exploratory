#include "Precomp.h"
#include "Debug.h"
#include "Geometry.h"

const uint32_t VertexStride[(uint32_t)VertexType::Count] =
{
    sizeof(StandardVertex),
};

const D3D11_INPUT_ELEMENT_DESC VertexElements[(uint32_t)VertexType::Count][16] = 
{
    { // StandardVertex
        { "POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0,                      D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  sizeof(XMFLOAT3),       D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  sizeof(XMFLOAT3) * 2,   D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BITANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  sizeof(XMFLOAT3) * 3,   D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,    0,  sizeof(XMFLOAT3) * 4,   D3D11_INPUT_PER_VERTEX_DATA, 0 },
    },
};

const uint32_t VertexElementCount[(uint32_t)VertexType::Count] =
{
    5,  // StandardVertex
};


/////////////////////////
// GeometryPool

std::shared_ptr<GeometryPool> GeometryPool::Create(const ComPtr<ID3D11Device>& device, VertexType type, uint32_t vertexCapacity, uint32_t indexCapacity)
{
    std::shared_ptr<GeometryPool> pool(new GeometryPool(type));
    if (pool)
    {
        if (pool->Initialize(device, vertexCapacity, indexCapacity))
        {
            return pool;
        }
    }
    return nullptr;
}

GeometryPool::GeometryPool(VertexType type)
    : Type(type)
    , VertexCount(0)
    , VertexCapacity(0)
    , IndexCount(0)
    , IndexCapacity(0)
{
}

bool GeometryPool::Initialize(const ComPtr<ID3D11Device>& device, uint32_t vertexCapacity, uint32_t indexCapacity)
{
    D3D11_BUFFER_DESC bd{};
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = VertexStride[(uint32_t)Type] * vertexCapacity;
    bd.StructureByteStride = VertexStride[(uint32_t)Type];
    bd.Usage = D3D11_USAGE_DEFAULT;

    CheckResult(device->CreateBuffer(&bd, nullptr, &VertexBuffer));
    VertexCapacity = vertexCapacity;

    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.ByteWidth = sizeof(uint32_t) * indexCapacity;
    bd.StructureByteStride = sizeof(uint32_t);

    CheckResult(device->CreateBuffer(&bd, nullptr, &IndexBuffer));
    IndexCapacity = indexCapacity;

    return true;
}

bool GeometryPool::ReserveRange(uint32_t vertexCount, uint32_t indexCount, uint32_t* baseVertex, uint32_t* baseIndex)
{
    if ((VertexCount + vertexCount > VertexCapacity) ||
        (IndexCount + indexCount > IndexCapacity))
    {
        // Not enough room
        return false;
    }

    *baseVertex = VertexCount;
    *baseIndex = IndexCount;

    VertexCount += vertexCount;
    IndexCount += indexCount;

    return true;
}
