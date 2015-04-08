#include "Precomp.h"
#include "Assets.h"
#include "Debug.h"
#include "StringHelpers.h"
#include "AssetLoader.h"

bool SaveTexture(const std::wstring& assetFilename, const std::wstring& outputFilename)
{
    FileHandle outputFile(CreateFile(outputFilename.c_str(), GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!outputFile.IsValid())
    {
        LogError(L"Failed to create output file: %s.", outputFilename.c_str());
        return false;
    }

    DWORD bytesWritten{};

    // This function implicitly uses WIC via DirectXTex, so we need COM initialized
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        LogError(L"Failed to initialize COM.");
        return false;
    }

    TexMetadata metadata;
    ScratchImage image;
    // Try for DDS first
    hr = LoadFromDDSFile(assetFilename.c_str(), DDS_FLAGS_NONE, &metadata, image);
    if (FAILED(hr))
    {
        // No? then try TGA
        hr = LoadFromTGAFile(assetFilename.c_str(), &metadata, image);
        if (FAILED(hr))
        {
            // Boo, use WIC as the catch-all
            hr = LoadFromWICFile(assetFilename.c_str(), WIC_FLAGS_NONE, &metadata, image);
            if (FAILED(hr))
            {
                LogError(L"Failed to load texture image.");
                CoUninitialize();
                return false;
            }
        }
    }

    ScratchImage mipChain;
    hr = GenerateMipMaps(image.GetImages(), image.GetImageCount(), metadata, TEX_FILTER_BOX | TEX_FILTER_FORCE_NON_WIC, 0, mipChain);
    if (FAILED(hr))
    {
        LogError(L"Failed to create mips for texture.");
        CoUninitialize();
        return false;
    }
    CoUninitialize();

    const TexMetadata& mipChainMetadata = mipChain.GetMetadata();

    TextureHeader header{};
    header.Signature = TextureHeader::ExpectedSignature;
    header.ArrayCount = (uint32_t)mipChainMetadata.arraySize;
    header.Format = mipChainMetadata.format;
    header.Width = (uint32_t)mipChainMetadata.width;
    header.Height = (uint32_t)mipChainMetadata.height;
    header.MipLevels = (uint32_t)mipChainMetadata.mipLevels;

    if (!WriteFile(outputFile.Get(), &header, sizeof(header), &bytesWritten, nullptr))
    {
        LogError(L"Error writing output file.");
        return false;
    }

    if (!WriteFile(outputFile.Get(), mipChain.GetPixels(), (uint32_t)mipChain.GetPixelsSize(), &bytesWritten, nullptr))
    {
        LogError(L"Error writing output file.");
        return false;
    }

    return true;
}