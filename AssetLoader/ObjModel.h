#pragma once

#include "AssetLoader.h"

struct ObjMaterial
{
    enum class TextureType
    {
        Ambient = 0,
        Diffuse,
        SpecularColor,
        SpecularPower,  // R channel only
        Transparency,
        Bump,           // Heightmap, uses R channel only
        Displacement,
    };

    std::string Name;
    XMFLOAT3    AmbientColor;
    XMFLOAT3    DiffuseColor;
    XMFLOAT3    SpecularColor;
    float       SpecularPower;  // exponent term for highlight
    float       Transparency;   // 1 - Alpha, so 1.0 is fully transparent, 0 is fully opaque
    std::map<TextureType, std::wstring> TextureMaps;    // Relative paths
};

struct ObjModelPart
{
    std::string Material;
    uint32_t StartIndex;
    uint32_t NumIndices;
};

struct ObjModelObject
{
    std::string Name;
    std::vector<ObjModelPart> Parts;
};

struct ObjModel
{
    // Shared by all parts
    std::vector<ModelVertex> Vertices;
    std::vector<uint32_t> Indices;

    std::vector<ObjMaterial> Materials;
    std::vector<ObjModelObject> Objects;

    bool Load(const wchar_t* filename);

private:
    std::vector<XMFLOAT3> Positions;    // x, y, z
    std::vector<XMFLOAT3> Normals;      // may not be normalized
    std::vector<XMFLOAT2> TexCoords;    // u, v

    std::map<uint64_t, std::map<uint32_t, uint32_t>> IndexMap;

    bool LoadMaterials(const wchar_t* filename);

    void ReadPositionAndColor(const char* line);
    void ReadTexCoord(const char* line);
    void ReadNormal(const char* line);
    void ReadFace(char* line, ObjModelPart* part);
};
