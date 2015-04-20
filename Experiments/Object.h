#pragma once

struct GeoMesh;

struct Object
{
    struct Part
    {
        XMFLOAT4X4                          RelativeTransform;
        std::shared_ptr<GeoMesh>            Mesh;
        ComPtr<ID3D11ShaderResourceView>    AlbedoSRV;
        ComPtr<ID3D11ShaderResourceView>    NormalSRV;
        ComPtr<ID3D11ShaderResourceView>    SpecularSRV;
    };

    XMFLOAT4X4  RootTransform;
    std::vector<std::shared_ptr<Part>>      Parts;
};
