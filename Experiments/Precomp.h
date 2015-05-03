#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#include <float.h>

#include <memory>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <algorithm>

#define ENABLE_DX12_SUPPORT

#if defined (ENABLE_DX12_SUPPORT)
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_4.h>
#else
#include <d3d11_2.h>
#include <dxgi1_3.h>
#endif

// DDS library
#include <DirectXTex.h>
// Fast vector math with SSE support
#include <DirectXMath.h>
using namespace DirectX;

// Custom asset loading
#include <AssetLoader.h>

// For RAII wrappers
#include <wrl.h>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

// Our own utility headers that change infrequently
#include "NonCopyable.h"
#include "Debug.h"

#define CheckResult(x) { HRESULT _hr = (x); if (FAILED(_hr)) { LogError(L#x L" failed with hr = 0x%08x.", _hr); return false; }}

struct ComPtrHasher
{
    template <typename T>
    size_t operator()(const ComPtr<T>& ptr) const { std::hash<T*> hasher; return hasher(ptr.Get()); }
};
