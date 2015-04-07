#pragma once

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
    std::map<TextureType, std::wstring> TextureMaps;
};

struct ObjModelPart
{
    std::string Material;

    // To build triangles, loop through index lists together (if non-empty) and fetch the components
    // matching by each index, and combine into a vertex
    std::vector<int32_t> PositionIndices;   // Index list to build triangles from vertices above
    std::vector<int32_t> TextureIndices;    // Index list to correlate tex coords with triangles above
    std::vector<int32_t> NormalIndices;     // Index list to correlate normals with triangles.
};

struct ObjModelObject
{
    std::string Name;
    std::vector<ObjModelPart> Parts;
};

struct ObjModel
{
    // Shared by all parts
    std::vector<XMFLOAT4> Positions;    // x, y, z, [w]. w is optional, defaults to 1
    std::vector<XMFLOAT3> Colors;
    std::vector<XMFLOAT3> TexCoords;    // u, v, [w]. w is optional, defaults to 0
    std::vector<XMFLOAT3> Normals;      // may not be normalized

    std::vector<ObjMaterial> Materials;
    std::vector<ObjModelObject> Objects;

    bool Load(const wchar_t* filename);

private:
    bool LoadMaterials(const wchar_t* filename);

    void ReadPositionAndColor(const char* line);
    void ReadTexCoord(const char* line);
    void ReadNormal(const char* line);
    void ReadFace(char* line, ObjModelPart* part);
};
