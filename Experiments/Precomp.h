#pragma once

#include <Windows.h>

#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#include <float.h>

#include <memory>
#include <vector>

#include <d3d12.h>
#include <dxgi.h>

// DDS library
#include <DirectXTex.h>
// Fast vector math with SSE support
#include <DirectXMath.h>
using namespace DirectX;

// For RAII wrappers
#include <wrl.h>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
