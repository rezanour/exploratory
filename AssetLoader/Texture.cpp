#include "Precomp.h"
#include "Assets.h"
#include "Debug.h"
#include "StringHelpers.h"
#include "AssetLoader.h"
#include <wincodec.h>

bool SaveTexture(const std::wstring& assetFilename, const std::wstring& outputFilename, bool saveDerivativeMap, bool expandChannels)
{
    FileHandle outputFile(CreateFile(outputFilename.c_str(), GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!outputFile.IsValid())
    {
        LogError(L"Failed to create output file: %s.", outputFilename.c_str());
        return false;
    }

    DWORD bytesWritten{};

    TexMetadata metadata;
    ScratchImage image;
    // Try for DDS first
    HRESULT hr = LoadFromDDSFile(assetFilename.c_str(), DDS_FLAGS_NONE, &metadata, image);
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
                return false;
            }
        }
    }

    if (saveDerivativeMap)
    {
#if 0 // Build normal maps instead
        ScratchImage originalImage(std::move(image));

        if (metadata.format != DXGI_FORMAT_R8_UNORM)
        {
            LogError(L"Saving as derivative map currently expects to operate on single channel texture.");
            return false;
        }

        hr = image.Initialize2D(DXGI_FORMAT_R8G8_UNORM, metadata.width, metadata.height, 1, 1);
        if (FAILED(hr))
        {
            LogError(L"Failed to create intermediate derivative map.");
            return false;
        }

        uint8_t* pSrc = originalImage.GetPixels();
        uint16_t* pDst = (uint16_t*)image.GetPixels();

        for (int y = 0; y < metadata.height; ++y)
        {
            for (int x = 0; x < metadata.width; ++x)
            {
                float x1 = pSrc[y * metadata.width + x] / 255.f;
                float x2 = pSrc[y * metadata.width + min(x + 1, metadata.width - 1)] / 255.f;
                float y1 = pSrc[y * metadata.width + x] / 255.f;
                float y2 = pSrc[min(y + 1, metadata.height - 1) * metadata.width + x] / 255.f;

                // Save derivatives in unorm space
                float dy = ((y2 - y1) + 1) * 0.5f;
                float dx = ((x2 - x1) + 1) * 0.5f;

                // save out to R8G8
                pDst[y * metadata.width + x] = ((uint16_t)(dy * 255.f) << 8) | (uint16_t)(dx * 255.f);
            }
        }
#else
        ScratchImage originalImage(std::move(image));

        hr = ComputeNormalMap(originalImage.GetImages(), originalImage.GetImageCount(),
                              metadata, CNMAP_DEFAULT, 10.f,
                              DXGI_FORMAT_R8G8B8A8_UNORM, image);
        if (FAILED(hr))
        {
            LogError(L"Failed to create intermediate derivative map.");
            return false;
        }
#endif
    }
    else if (expandChannels && metadata.format == DXGI_FORMAT_R8_UNORM)
    {
        ScratchImage originalImage(std::move(image));

        hr = Convert(originalImage.GetImages(), originalImage.GetImageCount(),
            metadata, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_BOX, 0.f, image);
        if (FAILED(hr))
        {
            LogError(L"Failed to create intermediate expanded texture.");
            return false;
        }
    }

    ScratchImage mipChain;
    hr = GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), TEX_FILTER_BOX | TEX_FILTER_FORCE_NON_WIC, 0, mipChain);
    if (FAILED(hr))
    {
        LogError(L"Failed to create mips for texture.");
        return false;
    }

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

bool ConvertToBumpMapToNormalMap(const std::wstring& bumpFilename, const std::wstring& outputFilename)
{
    TexMetadata metadata;
    ScratchImage image;
    // Try for DDS first
    HRESULT hr = LoadFromDDSFile(bumpFilename.c_str(), DDS_FLAGS_NONE, &metadata, image);
    if (FAILED(hr))
    {
        // No? then try TGA
        hr = LoadFromTGAFile(bumpFilename.c_str(), &metadata, image);
        if (FAILED(hr))
        {
            // Boo, use WIC as the catch-all
            hr = LoadFromWICFile(bumpFilename.c_str(), WIC_FLAGS_NONE, &metadata, image);
            if (FAILED(hr))
            {
                LogError(L"Failed to load texture image.");
                return false;
            }
        }
    }

    ScratchImage normalMap;
    hr = ComputeNormalMap(image.GetImages(), image.GetImageCount(), metadata, CNMAP_CHANNEL_RED, 10.f, DXGI_FORMAT_R8G8B8A8_UNORM, normalMap);
    if (FAILED(hr))
    {
        LogError(L"Failed to generate normal map.");
        return false;
    }

    hr = SaveToWICFile(normalMap.GetImages(), normalMap.GetImageCount(), 0, GUID_ContainerFormatPng, outputFilename.c_str());
    if (FAILED(hr))
    {
        LogError(L"Failed to generate normal map.");
        return false;
    }

    return true;
}
