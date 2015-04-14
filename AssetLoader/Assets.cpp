#include "Precomp.h"
#include "Assets.h"
#include "Debug.h"
#include "ObjModel.h"
#include "StringHelpers.h"

static std::map<AssetType, std::wstring> Extensions;

static std::wstring SourceRoot;
static std::wstring OutputRoot;

// Ensures that all subdirectories up to the file exist
static bool EnsurePathExists(const std::wstring& path);
static bool DoesAssetNeedBuilt(const std::wstring& assetFilename, const std::wstring& outputFilename, bool* needsBuild);

static bool BuildModel(const std::wstring& assetFilename, const std::wstring& outputFilename);
static bool BuildTexture(const std::wstring& assetFilename, const std::wstring& outputFilename, bool saveDerivativeMap = false, bool expandChannels = false);


bool ProcessAssets(
    const std::wstring& sourceRoot,
    const std::wstring& outputRoot,
    const std::vector<SourceAsset>& assets)
{
    if (Extensions.empty())
    {
        // Initialize extension table
        Extensions[AssetType::Model] = L"model";
        Extensions[AssetType::Texture] = L"texture";
        Extensions[AssetType::BumpTexture] = L"texture";
        Extensions[AssetType::SpecularTexture] = L"texture";
    }

    SourceRoot = sourceRoot;
    OutputRoot = outputRoot;

    // For each source asset:
    //  1. Compute the final output filename
    //  2. If output file already exists, check timestamp reported in <outputfile>.time
    //  3. If output file doesn't exist, or time file says source asset is newer, build asset

    for (int i = 0; i < (int)assets.size(); ++i)
    {
        std::wstring outputFilename;
        if (!BuildAsset(assets[i], outputFilename))
        {
            LogError(L"Failed processing assets.");
            return false;
        }
    }

    return true;
}

bool EnsurePathExists(const std::wstring& path)
{
    const wchar_t* start = path.c_str();
    const wchar_t* p = start;

    while (*p != 0)
    {
        while (*p != L'/' && *p != 0)
        {
            ++p;
        }
        if (*p == 0)
        {
            break;
        }

        std::wstring subPath = path.substr(0, (p - start));
        if (!CreateDirectory(subPath.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            LogError(L"Failed to create directory: %s.", subPath.c_str());
            return false;
        }

        ++p;
    }

    return true;
}

bool DoesAssetNeedBuilt(const SourceAsset& asset, bool* needsBuild)
{
    std::wstring assetFilename = SourceRoot + asset.Path;
    std::wstring outputFilename = OutputRoot + ReplaceExtension(asset.Path, Extensions[asset.Type]);
    return DoesAssetNeedBuilt(assetFilename, outputFilename, needsBuild);
}

bool DoesAssetNeedBuilt(const std::wstring& assetFilename, const std::wstring& outputFilename, bool* needsBuild)
{
    // By default, needs to be built.
    *needsBuild = true;

    if (GetFileAttributes(outputFilename.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        // File exists, is source asset last modified time newer than built content?
        FileHandle outputFile(CreateFile(outputFilename.c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!outputFile.IsValid())
        {
            LogError(L"Failed to access file: %s.", outputFilename.c_str());
            return false;
        }

        FILETIME contentLastWriteTime{};
        if (!GetFileTime(outputFile.Get(), nullptr, nullptr, &contentLastWriteTime))
        {
            LogError(L"Failed to get file timestamp info: %s.", outputFilename.c_str());
            return false;
        }

        // Get source asset last modified time
        FileHandle sourceFile(CreateFile(assetFilename.c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!sourceFile.IsValid())
        {
            LogError(L"Failed to open source asset: %s.", assetFilename.c_str());
            return false;
        }

        FILETIME lastWriteTime{};
        if (!GetFileTime(sourceFile.Get(), nullptr, nullptr, &lastWriteTime))
        {
            LogError(L"Failed to get file timestamp info: %s.", assetFilename.c_str());
            return false;
        }

        if (CompareFileTime(&lastWriteTime, &contentLastWriteTime) <= 0)
        {
            // Source asset has not been modified since the last asset build. No need to build again
            *needsBuild = false;
        }
    }

    return true;
}

bool BuildAsset(const SourceAsset& asset, std::wstring& outputRelativePath)
{
    Log(L"Processing asset: %s...", asset.Path.c_str());

    std::wstring assetPath = asset.Path;
    // If asset path already includes SourceRoot, strip it off
    if (_wcsnicmp(assetPath.c_str(), SourceRoot.c_str(), SourceRoot.size()) == 0)
    {
        assetPath = assetPath.substr(SourceRoot.size());
    }

    std::wstring assetFilename = SourceRoot + assetPath;
    outputRelativePath = ReplaceExtension(assetPath, Extensions[asset.Type]);
    std::wstring outputFilename = OutputRoot + outputRelativePath;

    bool needsBuild = false;
    if (!DoesAssetNeedBuilt(assetFilename, outputFilename, &needsBuild))
    {
        LogError(L"Failed to query file information.");
        return false;
    }

    if (!needsBuild)
    {
        Log(L"  Content up to date. Skipping.");
        return true;
    }

    if (!EnsurePathExists(outputFilename))
    {
        LogError(L"Failed to create output file.");
        return false;
    }

    // Build asset
    switch (asset.Type)
    {
    case AssetType::Model:
        if (!BuildModel(assetFilename, outputFilename))
        {
            LogError(L"Failed to build asset: %s.", assetFilename.c_str());
            return false;
        }
        break;

    case AssetType::Texture:
        if (!BuildTexture(assetFilename, outputFilename))
        {
            LogError(L"Failed to build asset: %s.", assetFilename.c_str());
            return false;
        }
        break;

    case AssetType::BumpTexture:
        if (!BuildTexture(assetFilename, outputFilename, true))
        {
            LogError(L"Failed to build asset: %s.", assetFilename.c_str());
            return false;
        }
        break;

    case AssetType::SpecularTexture:
        if (!BuildTexture(assetFilename, outputFilename, false, true))
        {
            LogError(L"Failed to build asset: %s.", assetFilename.c_str());
            return false;
        }
        break;

    default:
        LogError(L"Unimplemented.");
        return true;
    }

    Log(L"  Done.");
    return true;
}

bool BuildModel(const std::wstring& assetFilename, const std::wstring& outputFilename)
{
    std::unique_ptr<ObjModel> objModel(new ObjModel);
    if (!objModel)
    {
        LogError(L"Failed to create model parser.");
        return false;
    }

    if (!objModel->Load(assetFilename.c_str()))
    {
        LogError(L"Failed to parse model: %s.", assetFilename.c_str());
    }

    if (!SaveModel(objModel, outputFilename))
    {
        LogError(L"Failed to save model file: %s.", outputFilename.c_str());
        return false;
    }

    return true;
}

bool BuildTexture(const std::wstring& assetFilename, const std::wstring& outputFilename, bool saveDerivativeMap, bool expandChannels)
{
    if (!SaveTexture(assetFilename, outputFilename, saveDerivativeMap, expandChannels))
    {
        LogError(L"Failed to save texture file: %s.", outputFilename.c_str());
        return false;
    }

    return true;
}
