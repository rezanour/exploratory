#include "Precomp.h"
#include "Assets.h"
#include "Debug.h"
#include "ObjModel.h"
#include "StringHelpers.h"

static std::map<AssetType, std::wstring> Extensions;

// Ensures that all subdirectories up to the file exist
static bool EnsurePathExists(const std::wstring& path);
static bool DoesAssetNeedBuilt(const std::wstring& assetFilename, const std::wstring& outputFilename, bool* needsBuild);
static bool WriteTimeEntry(const std::wstring& assetFilename, const std::wstring& outputFilename);

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
    }

    // For each source asset:
    //  1. Compute the final output filename
    //  2. If output file already exists, check timestamp reported in <outputfile>.time
    //  3. If output file doesn't exist, or time file says source asset is newer, build asset

    for (int i = 0; i < (int)assets.size(); ++i)
    {
        const SourceAsset& asset = assets[i];

        Log(L"Processing asset: %s...", asset.Path.c_str());
            
        std::wstring assetFilename = sourceRoot + asset.Path;
        std::wstring outputFilename = outputRoot + ReplaceExtension(asset.Path, Extensions[asset.Type]);
        bool needsBuild = false;
        if (!DoesAssetNeedBuilt(assetFilename, outputFilename, &needsBuild))
        {
            LogError(L"Failed to query file information.");
            return false;
        }

        if (!needsBuild)
        {
            Log(L"  Content up to date. Skipping.");
            continue;
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

        default:
            LogError(L"Unimplemented.");
            break;
        }

        if (!WriteTimeEntry(assetFilename, outputFilename))
        {
            LogError(L"Failed to write time data.");
            return false;
        }

        Log(L"  Done.");
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

bool DoesAssetNeedBuilt(const std::wstring& assetFilename, const std::wstring& outputFilename, bool* needsBuild)
{
    // By default, needs to be built.
    *needsBuild = true;

    if (GetFileAttributes(outputFilename.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        // File exists, check for .time data
        std::wstring timeFilename = outputFilename + L".time";
        FileHandle timeFile(CreateFile(timeFilename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (timeFile.IsValid())
        {
            // Time file exists
            DWORD fileSize = GetFileSize(timeFile.Get(), nullptr);
            if (fileSize < sizeof(FILETIME))
            {
                LogError(L"Invalid time file: %s.", timeFilename.c_str());
                return false;
            }

            FILETIME loggedLastWriteTime{};
            DWORD bytesRead{};
            if (!ReadFile(timeFile.Get(), &loggedLastWriteTime, sizeof(loggedLastWriteTime), &bytesRead, nullptr))
            {
                LogError(L"Failed to read time file: %s.", timeFilename.c_str());
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

            if (CompareFileTime(&lastWriteTime, &loggedLastWriteTime) <= 0)
            {
                // Source asset has not been modified since the last asset build. No need to build again
                *needsBuild = false;
            }
        }
    }

    return true;
}

bool BuildModel(
    const std::wstring& assetFilename,
    const std::wstring& outputFilename)
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

bool WriteTimeEntry(const std::wstring& assetFilename, const std::wstring& outputFilename)
{
    // Store asset last write time to the time file
    std::wstring timeFilename = outputFilename + L".time";
    FileHandle timeFile(CreateFile(timeFilename.c_str(), GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!timeFile.IsValid())
    {
        LogError(L"Failed to create time file: %s.", timeFilename.c_str());
        return false;
    }

    FileHandle assetFile(CreateFile(assetFilename.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!assetFile.IsValid())
    {
        LogError(L"Failed to open asset file: %s.", assetFilename.c_str());
        return false;
    }

    FILETIME lastWriteTime{};
    if (!GetFileTime(assetFile.Get(), nullptr, nullptr, &lastWriteTime))
    {
        LogError(L"Failed to query file time from: %s.", assetFilename.c_str());
        return false;
    }

    DWORD bytesWritten{};
    if (!WriteFile(timeFile.Get(), &lastWriteTime, sizeof(lastWriteTime), &bytesWritten, nullptr))
    {
        LogError(L"Failed to write data to time file: %s.", timeFilename.c_str());
        return false;
    }

    return true;
}
