#pragma once

//*****************************************************
// PUBLIC HEADER. For use from calling applications
//*****************************************************

#pragma pack(push)

// MODEL

#pragma pack(4)

struct ModelVertex
{
    XMFLOAT3 Position;
    XMFLOAT3 Normal;
    XMFLOAT2 TexCoord;
};

#pragma pack(1)

// Followed directly by all vertices, then all indices, then all objects
struct ModelHeader
{
    static const uint32_t ExpectedSignature = 'MODL';

    uint32_t Signature;
    uint32_t NumVertices;
    uint32_t NumIndices;
    uint32_t NumObjects;
};

// Followed directly by all parts
struct ModelObject
{
    char Name[128];
    uint32_t NumParts;
};

// Followed directly by all vertices
struct ModelPart
{
    wchar_t DiffuseTexture[256];
    wchar_t NormalTexture[256];
    wchar_t SpecularTexture[256];
    uint32_t StartIndex;
    uint32_t NumIndices;
};

// MODEL

#pragma pack(1)

// Followed immediately by raw texture data ready for uploading to GPU
struct TextureHeader
{
    static const uint32_t ExpectedSignature = 'TEX ';

    uint32_t Signature;
    uint32_t Width;
    uint32_t Height;
    uint32_t ArrayCount;
    uint32_t MipLevels;
    DXGI_FORMAT Format;
};

#pragma pack(pop)

