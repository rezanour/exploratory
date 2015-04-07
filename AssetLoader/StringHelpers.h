#pragma once

// Returns pointer to first non-whitespace character or
// end of string if none exist.
inline char* TrimLeadingWhitespace(char* line)
{
    char* p = line;
    while ((*p == ' ' || *p == '\t') && *p != 0)
    {
        ++p;
    }
    return p;
}

// Modifies line in-place
inline void TrimTrailingWhitespace(char* line)
{
    char* p = line;
    // Go to end of string
    while (*p != 0) ++p;

    --p;
    // Now, scan backwards for first non-whitespace
    while ((*p == ' ' || *p == '\t') && p > line)
    {
        --p;
    }
    *(p + 1) = 0;
}

inline std::wstring ConvertToWide(const std::string& input)
{
    std::wstring output(input.size(), L' ');
    std::copy(input.begin(), input.end(), output.begin());
    return output;
}

inline void NormalizeSlashes(std::wstring& path)
{
    for (int i = 0; i < (int)path.size(); ++i)
    {
        if (path[i] == L'\\') path[i] = L'/';
    }
}

inline void EnsureTrailingSlash(std::wstring& path)
{
    if (path.size() > 0 && path[path.size() - 1] != L'/')
    {
        path.append(L"/");
    }
}

inline std::wstring ReplaceExtension(const std::wstring& source, const std::wstring& newExt)
{
    for (int i = (int)source.size() - 1; i >= 0; --i)
    {
        if (source[i] == L'/')
        {
            break;
        }
        else if (source[i] == L'.')
        {
            return source.substr(0, i + 1) + newExt;
        }
    }

    // No extension found. Just append
    return source + L'.' + newExt;
}