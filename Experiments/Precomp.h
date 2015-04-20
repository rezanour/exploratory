#pragma once

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

//#define ENABLE_DX12_SUPPORT

#if defined (ENABLE_DX12_SUPPORT)
#include <d3d12.h>
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
