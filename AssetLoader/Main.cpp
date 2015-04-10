#include "Precomp.h"
#include "Assets.h"
#include "Debug.h"
#include "StringHelpers.h"

static bool ReadConfig(
    const std::wstring& configFilename,
    std::wstring& sourceRoot,
    std::wstring& outputRoot,
    std::vector<SourceAsset>& assets);

int wmain(int argc, wchar_t* argv[])
{
    // WIC is used in various parts of DirectXTex, and needs COM to be initialized
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        LogError(L"Failed to initialize COM.");
        return -1;
    }

    std::wstring configFilename(L"AssetLoader.cfg");    // Default config file

    // Check if an override file was specified on command line
    if (argc > 1)
    {
        // Check if the parameter is a valid file path.
        if (GetFileAttributes(argv[1]) == INVALID_FILE_ATTRIBUTES)
        {
            LogError(L"Config file %s doesn't exist.", argv[1]);
            CoUninitialize();
            return -2;
        }

        configFilename = argv[1];
    }

    std::wstring sourceRoot;    // Root directory of source assets
    std::wstring outputRoot;    // Root where processed output files should go
    std::vector<SourceAsset> assets;

    if (!ReadConfig(configFilename, sourceRoot, outputRoot, assets))
    {
        LogError(L"Failed to load config file: %s.", configFilename.c_str());
        CoUninitialize();
        return -3;
    }

    // Make sure both roots are standardized on / and also end in /
    NormalizeSlashes(sourceRoot);
    EnsureTrailingSlash(sourceRoot);
    NormalizeSlashes(outputRoot);
    EnsureTrailingSlash(outputRoot);

    // Process assets
    ProcessAssets(sourceRoot, outputRoot, assets);

    CoUninitialize();

    return 0;
}

static bool ReadConfig(
    const std::wstring& configFilename,
    std::wstring& sourceRoot,
    std::wstring& outputRoot,
    std::vector<SourceAsset>& assets)
{
    FileHandle configFile(CreateFile(configFilename.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!configFile.IsValid())
    {
        LogError(L"Failed to find AssetLoader.cfg config file.");
        return false;
    }

    DWORD fileSize = GetFileSize(configFile.Get(), nullptr);
    std::unique_ptr<char[]> buffer(new char[fileSize]);
    if (!buffer)
    {
        LogError(L"Failed to allocate buffer for reading config file.");
        return false;
    }

    DWORD bytesRead{};
    if (!ReadFile(configFile.Get(), buffer.get(), fileSize, &bytesRead, nullptr))
    {
        LogError(L"Failed to read config file data.");
        return false;
    }

    char* p = buffer.get();
    char* end = p + fileSize;
    char* line = nullptr;

    while (p < end)
    {
        line = p;
        // Find next newline and replace with \0 to terminate 'line'
        while (*p != '\n' && *p != '\r' && p < end)
        {
            ++p;
        }
        if (p < end)
        {
            *p = 0;
        }

        line = TrimLeadingWhitespace(line);
        TrimTrailingWhitespace(line);

        // Process line
        if (_strnicmp(line, "SourceRoot:", 11) == 0)
        {
            sourceRoot = ConvertToWide(TrimLeadingWhitespace(line + 12));
        }
        else if (_strnicmp(line, "OutputRoot:", 11) == 0)
        {
            outputRoot = ConvertToWide(TrimLeadingWhitespace(line + 12));
        }
        else if (_strnicmp(line, "Model:", 6) == 0)
        {
            assets.push_back(SourceAsset(AssetType::Model, ConvertToWide(TrimLeadingWhitespace(line + 7))));
        }

        // Advance p to next line
        ++p;
        while ((*p == '\n' || *p == '\r' || *p == ' ') && p < end)
        {
            ++p;
        }
    }

    return true;
}
