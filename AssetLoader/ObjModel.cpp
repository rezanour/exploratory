#include "Precomp.h"
#include "ObjModel.h"
#include "Debug.h"

//#define PRINT_UNKNOWN_LINES

#if defined(PRINT_UNKNOWN_LINES)
#define PRINT_LINE(x) Log(L"%S", x);
#else
#define PRINT_LINE(x)
#endif


bool ObjModel::Load(const wchar_t* filename)
{
    FileHandle file(CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.IsValid())
    {
        LogError(L"Failed to open file %s.", filename);
        return false;
    }

    // Doesn't currently support files over 4GB
    DWORD fileSize = GetFileSize(file.Get(), nullptr);

    std::unique_ptr<char[]> buffer(new char[fileSize]);
    if (!buffer)
    {
        LogError(L"Failed to allocate buffer for reading file into.");
        return false;
    }

    // Read file in
    DWORD bytesRead = 0;
    if (!ReadFile(file.Get(), buffer.get(), fileSize, &bytesRead, nullptr))
    {
        LogError(L"Error reading file.");
        return false;
    }

    file.Close();

    ObjModelObject* currentObject = nullptr;
    ObjModelPart* currentPart = nullptr;

    char* p = buffer.get();
    char* end = p + fileSize;
    while (p < end)
    {
        // Read the next line of text
        char* line = p;
        while (*p != '\n' && *p != '\r' && p < (end - 1))
        {
            ++p;
        }
        // Replace newline with null terminator to cap off the line
        *p = 0;

        switch (*line)
        {
        case 'm':
            if (_strnicmp(line, "mtllib", 6) == 0)
            {
                wchar_t objPath[1024] = {};
                wcscpy_s(objPath, filename);

                wchar_t* slash1 = wcsrchr(objPath, L'\\');
                wchar_t* slash2 = wcsrchr(objPath, L'/');
                if (!slash1) slash1 = slash2;
                if (slash1 && slash2 && slash2 > slash1)
                {
                    slash1 = slash2;
                }
                if (slash1)
                {
                    *(slash1 + 1) = 0;
                }
                else
                {
                    objPath[0] = 0;
                }

                wchar_t matFile[1024] = {};
                swprintf_s(matFile, L"%s%S", objPath, line + 7);

                if (!LoadMaterials(matFile))
                {
                    LogError(L"Failed to load material file.");
                    return false;
                }
            }
            else
            {
                LogError(L"Unknown m type.");
            }
            break;

        case 'v':   // Vertex info
            switch (*(line + 1))
            {
            case ' ':   // Position
                ReadPositionAndColor(line);
                break;

            case 't':   // TexCoord
                ReadTexCoord(line);
                break;

            case 'n':   // Normal
                ReadNormal(line);
                break;

            default:
                LogError(L"Unknown v type.");
                break;
            }
            break;

        case 'f':   // Face info
            ReadFace(line, currentPart);
            break;

        case 'o':   // Object
        case 'g':   // Group
            // Insert a new object
            Objects.push_back(ObjModelObject());

            // Get a pointer to it
            // This pointer is only valid as long as we don't insert or remove anything from this vector.
            currentObject = &Objects[Objects.size() - 1];
            currentObject->Name = (line + 2);
            break;

        case 'u':
            if (_strnicmp(line, "usemtl", 6) == 0)
            {
                // If we are wrapping up a part, store off bb
                currentObject->Parts.push_back(ObjModelPart());
                currentPart = &currentObject->Parts[currentObject->Parts.size() - 1];
                currentPart->Material = (line + 7);
                currentPart->StartIndex = (uint32_t)Indices.size();
                currentPart->NumIndices = 0;
            }
            else
            {
                LogError(L"Unknown u type.");
            }
            break;

        default:
        case '#':   // Comment
            // Echo to the debug output for now
            PRINT_LINE(line);
            break;
        }

        // Advance to beginning of next line
        ++p;
        while ((*p == '\n' || *p == '\r') && p < end)
        {
            ++p;
        }
    }

    return true;
}

void ObjModel::ReadPositionAndColor(const char* line)
{
    XMFLOAT3 position;
    sscanf_s(line, "v %f %f %f", &position.x, &position.y, &position.z);
    Positions.push_back(position);
}

void ObjModel::ReadTexCoord(const char* line)
{
    XMFLOAT2 value;
    sscanf_s(line, "vt %f %f", &value.x, &value.y);
    TexCoords.push_back(value);
}

void ObjModel::ReadNormal(const char* line)
{
    XMFLOAT3 value;
    sscanf_s(line, "vn %f %f %f", &value.x, &value.y, &value.z);
    Normals.push_back(value);
}

void ObjModel::ReadFace(char* line, ObjModelPart* part)
{
    char* context = nullptr;
    char* token = strtok_s(line, " ", &context);
    uint32_t positionIndex = 0, texIndex = 0, normIndex = 0;
    uint32_t posFirst = 0, posPrev = 0;
    uint32_t texFirst = 0, texPrev = 0;
    uint32_t normFirst = 0, normPrev = 0;
    uint32_t i = 0;
    while (token)
    {
        if (*token >= '0' && *token <= '9')
        {
            char* subContext = nullptr;
            char* subToken = strtok_s(token, "/ ", &subContext);
            if (subToken)
            {
                positionIndex = (uint32_t)atoll(subToken);
                subToken = strtok_s(nullptr, "/ ", &subContext);
            }
            if (subToken)
            {
                texIndex = (uint32_t)atoll(subToken);
                subToken = strtok_s(nullptr, "/ ", &subContext);
            }
            if (subToken)
            {
                normIndex = (uint32_t)atoll(subToken);
                subToken = strtok_s(nullptr, "/ ", &subContext);
            }

            if (posFirst == 0) posFirst = positionIndex;
            if (texFirst == 0) texFirst = texIndex;
            if (normFirst == 0) normFirst = normIndex;

            if (i >= 2)
            {
                // Third+ points. Add triangle
                struct IndexKey { uint32_t vals[3]; };

                IndexKey indexKeys[3] {
                    { { posFirst, texFirst, normFirst } },
                    { { posPrev, texPrev, normPrev } },
                    { { positionIndex, texIndex, normIndex } }
                };

                XMFLOAT3 normal = XMFLOAT3(0.f, 0.f, 0.f);

                // In case the data doesn't have a normal, compute the triangle one
                XMVECTOR a = XMLoadFloat3(&Positions[indexKeys[0].vals[0] - 1]);
                XMVECTOR b = XMLoadFloat3(&Positions[indexKeys[1].vals[0] - 1]);
                XMVECTOR c = XMLoadFloat3(&Positions[indexKeys[2].vals[0] - 1]);
                XMVECTOR n = XMVector3Normalize(XMVector3Cross(b - a, c - a));
                XMStoreFloat3(&normal, n);

                for (auto key : indexKeys)
                {
                    bool found = false;

                    uint64_t positionTex = ((uint64_t)key.vals[0] << 32) | key.vals[1];
                    auto it = IndexMap.find(positionTex);
                    if (it != IndexMap.end())
                    {
                        auto it2 = it->second.find(key.vals[2]);
                        if (it2 != it->second.end())
                        {
                            Indices.push_back(it2->second);
                            found = true;
                        }
                    }

                    if (!found)
                    {
                        // Didn't find it, create a new vertex & add
                        ModelVertex v{};
                        v.Position = Positions[key.vals[0] - 1];
                        if (key.vals[2] > 0)
                        {
                            XMStoreFloat3(&v.Normal, XMVector3Normalize(XMLoadFloat3(&Normals[key.vals[2] - 1])));
                        }
                        else
                        {
                            v.Normal = normal;
                        }
                        if (key.vals[1] > 0)
                        {
                            v.TexCoord = XMFLOAT2(1.f - TexCoords[key.vals[1] - 1].x, 1.f - TexCoords[key.vals[1] - 1].y);
                        }

                        Vertices.push_back(v);
                        Indices.push_back((uint32_t)Vertices.size() - 1);

                        if (it != IndexMap.end())
                        {
                            it->second[key.vals[2]] = (uint32_t)Vertices.size() - 1;
                        }
                        else
                        {
                            IndexMap[positionTex] = std::map<uint32_t, uint32_t>();
                            IndexMap[positionTex][key.vals[2]] = (uint32_t)Vertices.size() - 1;
                        }
                    }

                    ++part->NumIndices;
                }
            }

            posPrev = positionIndex;
            texPrev = texIndex;
            normPrev = normIndex;
            ++i;
        }

        token = strtok_s(nullptr, " ", &context);
    }
}

bool ObjModel::LoadMaterials(const wchar_t* filename)
{
    FileHandle file(CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.IsValid())
    {
        LogError(L"Failed to open material file %s.", filename);
        return false;
    }

    // Doesn't currently support files over 4GB
    DWORD fileSize = GetFileSize(file.Get(), nullptr);

    std::unique_ptr<char[]> buffer(new char[fileSize]);
    if (!buffer)
    {
        LogError(L"Failed to allocate buffer for reading file into.");
        return false;
    }

    // Read file in
    DWORD bytesRead = 0;
    if (!ReadFile(file.Get(), buffer.get(), fileSize, &bytesRead, nullptr))
    {
        LogError(L"Error reading file.");
        return false;
    }

    file.Close();

    wchar_t basePath[1024] = {};
    wcscpy_s(basePath, filename);

    wchar_t* slash1 = wcsrchr(basePath, L'\\');
    wchar_t* slash2 = wcsrchr(basePath, L'/');
    if (!slash1) slash1 = slash2;
    if (slash1 && slash2 && slash2 > slash1)
    {
        slash1 = slash2;
    }
    if (slash1)
    {
        *(slash1 + 1) = 0;
    }
    else
    {
        basePath[0] = 0;
    }

    wchar_t path[1024] = {};

    ObjMaterial* currentMaterial = nullptr;

    char* p = buffer.get();
    char* end = p + fileSize;
    while (p < end)
    {
        // Read the next line of text
        char* line = p;
        while (*p != '\n' && *p != '\r' && p < (end - 1))
        {
            ++p;
        }
        // Replace newline with null terminator to cap off the line
        *p = 0;

        if (*line == '#')
        {
            // Comment
            // Echo to the debug output for now
            PRINT_LINE(line);
        }
        else if (_strnicmp(line, "newmtl", 6) == 0)
        {
            // Define new material

            // Insert a new object
            Materials.push_back(ObjMaterial());

            // Get a pointer to it
            // This pointer is only valid as long as we don't insert or remove anything from this vector.
            currentMaterial = &Materials[Materials.size() - 1];
            currentMaterial->Name = (line + 7);
        }
        else
        {
            // Material property

            // Make sure it's not an empy line, and strip whitespace
            while (!isalnum(*line) && line < p)
            {
                ++line;
            }
            if (line != p)
            {
                switch (*line)
                {
                case 'K':
                    switch (*(line + 1))
                    {
                    case 'a':   // ambient
                        sscanf_s(line + 3, "%f %f %f",
                            &currentMaterial->AmbientColor.x,
                            &currentMaterial->AmbientColor.y,
                            &currentMaterial->AmbientColor.z);
                        break;
                    case 'd':   // diffuse
                        sscanf_s(line + 3, "%f %f %f",
                            &currentMaterial->DiffuseColor.x,
                            &currentMaterial->DiffuseColor.y,
                            &currentMaterial->DiffuseColor.z);
                        break;
                    case 's':   // specular
                        sscanf_s(line + 3, "%f %f %f",
                            &currentMaterial->SpecularColor.x,
                            &currentMaterial->SpecularColor.y,
                            &currentMaterial->SpecularColor.z);
                        break;
                    default:
                        // Unknown, print it
                        PRINT_LINE(line);
                        break;
                    }
                    break;

                case 'N':
                    if (*(line + 1) == 's') // Specular power
                    {
                        sscanf_s(line + 3, "%f", &currentMaterial->SpecularPower);
                    }
                    else
                    {
                        // Unknown, print it
                        PRINT_LINE(line);
                    }
                    break;

                case 'T':
                    if (*(line + 1) == 'r') // Transparency
                    {
                        sscanf_s(line + 3, "%f", &currentMaterial->Transparency);
                    }
                    else
                    {
                        // Unknown, print it
                        PRINT_LINE(line);
                    }
                    break;

                case 'd':
                    if (*(line + 1) == ' ')    // dissolve (aka Transparency)
                    {
                        sscanf_s(line + 3, "%f", &currentMaterial->Transparency);
                    }
                    else if (_strnicmp(line, "disp", 4) == 0)   // displacement map
                    {
                        swprintf_s(path, L"%s%S", basePath, (line + 5));
                        currentMaterial->TextureMaps[ObjMaterial::TextureType::Displacement] = path;
                    }
                    break;

                case 'b':
                    if (_strnicmp(line, "bump", 4) == 0)   // bump map
                    {
                        swprintf_s(path, L"%s%S", basePath, (line + 5));
                        currentMaterial->TextureMaps[ObjMaterial::TextureType::Bump] = path;
                    }
                    break;

                case 'm':
                    if (_strnicmp(line, "map_Ka", 6) == 0)  // Ambient color
                    {
                        swprintf_s(path, L"%s%S", basePath, (line + 7));
                        currentMaterial->TextureMaps[ObjMaterial::TextureType::Ambient] = path;
                    }
                    else if (_strnicmp(line, "map_Kd", 6) == 0)  // Diffuse color
                    {
                        swprintf_s(path, L"%s%S", basePath, (line + 7));
                        currentMaterial->TextureMaps[ObjMaterial::TextureType::Diffuse] = path;
                    }
                    else if (_strnicmp(line, "map_Ks", 6) == 0)  // Specular color
                    {
                        swprintf_s(path, L"%s%S", basePath, (line + 7));
                        currentMaterial->TextureMaps[ObjMaterial::TextureType::SpecularColor] = path;
                    }
                    else if (_strnicmp(line, "map_Ns", 6) == 0)  // Specular Power
                    {
                        swprintf_s(path, L"%s%S", basePath, (line + 7));
                        currentMaterial->TextureMaps[ObjMaterial::TextureType::SpecularPower] = path;
                    }
                    else if (_strnicmp(line, "map_d", 5) == 0)  // Transparency
                    {
                        swprintf_s(path, L"%s%S", basePath, (line + 6));
                        currentMaterial->TextureMaps[ObjMaterial::TextureType::Transparency] = path;
                    }
                    else if (_strnicmp(line, "map_bump", 8) == 0)  // Bump
                    {
                        swprintf_s(path, L"%s%S", basePath, (line + 9));
                        currentMaterial->TextureMaps[ObjMaterial::TextureType::Bump] = path;
                    }
                    else
                    {
                        // Unknown, print it
                        PRINT_LINE(line);
                    }
                    break;

                default:
                    // Unknown line, print it
                    PRINT_LINE(line);
                    break;
                }
            }
        }

        // Advance to beginning of next line
        ++p;
    }



    return true;
}
