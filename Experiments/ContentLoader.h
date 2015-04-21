#pragma once

#include "Object.h"

class ContentLoader : public NonCopyable
{
public:
    ContentLoader(const ComPtr<ID3D11Device>& device, const std::wstring& contentRoot);

    bool LoadObject(const std::wstring& filename, std::shared_ptr<Object>* object);
    bool LoadTexture(const std::wstring& filename, ComPtr<ID3D11ShaderResourceView>* srv);

private:
    std::wstring ContentRoot;
    ComPtr<ID3D11Device> Device;

    std::map<std::wstring, ComPtr<ID3D11ShaderResourceView>> CachedTextureMap;
};
