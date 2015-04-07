#pragma once

//*****************************************************
// PUBLIC HEADER. For use from calling applications
//*****************************************************

#pragma pack(push)

#pragma pack(4)

struct ModelVertex
{
    XMFLOAT3 Position;
    XMFLOAT3 Normal;
    XMFLOAT2 TexCoord;
};

#pragma pack(1)

// Followed directly by all objects
struct ModelHeader
{
    static const uint32_t ExpectedSignature = 'MODL';

    uint32_t Signature;
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
    uint32_t NumVertices;
};

#pragma pack(pop)

