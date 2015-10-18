#include "Precomp.h"
#include "Renderer.h"
#include "Debug.h"
#include "Shaders/SimpleTransformVS.h"
#include "Shaders/SimpleTexturePS.h"

#if defined (ENABLE_DX12_SUPPORT)
#pragma comment(lib, "d3d12.lib")

#define CHK(a) { HRESULT hr = a; if (FAILED(hr)) { assert(false); return false; } }

typedef std::unordered_map<ComPtr<ID3D12Resource>, ComPtr<ID3D12Resource>, ComPtrHasher> ComResourceMap;

const UINT kNumBackBuffers = 2;

std::unique_ptr<Renderer> Renderer::Create(HWND window)
{
    std::unique_ptr<Renderer> renderer(new Renderer(window));
    if (renderer)
    {
        if (renderer->Initialize())
        {
            return renderer;
        }
    }
    return nullptr;
}

Renderer::Renderer(HWND window) :
    Window(window),
    RenderedEvent(nullptr),
    BackBufferIdx(0),
    RenderFenceIdx(1),
    BundleCreated(false)
{
}

Renderer::~Renderer()
{
}

bool Renderer::Initialize()
{
    HRESULT hr;

#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    hr = D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()));
    if (FAILED(hr))
    {
        LogError(L"Failed to get debug D3D12.");
        return false;
    }

    debug->EnableDebugLayer();
#endif

    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(Device.GetAddressOf()));
    if (FAILED(hr))
    {
        LogError(L"Failed to create D3D12 device.");
        return false;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    std::wstring output = L"Raster order views supported: ";
    output += options.ROVsSupported ? L"Yes" : L"No";
    output += L"\n";
    OutputDebugString(output.c_str());

    D3D12_COMMAND_QUEUE_DESC queueDesc;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = 0;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;
    CHK(Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(DefaultQueue.GetAddressOf())));

    UINT dxgiFlag = 0;
#if defined(_DEBUG)
    dxgiFlag |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    ComPtr<IDXGIFactory> factory;
    CHK(CreateDXGIFactory2(dxgiFlag, IID_PPV_ARGS(factory.GetAddressOf())));

    DXGI_SWAP_CHAIN_DESC scd {};
    scd.BufferCount = 2;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.OutputWindow = Window;
    scd.Windowed = TRUE;

    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    CHK(factory->CreateSwapChain(DefaultQueue.Get(), &scd, &SwapChain)); // Swap chain needs the queue so it can force a flush on it

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
    memset(&heapDesc, 0, sizeof(heapDesc));
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = 1;
    CHK(Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(RenderTargetDescHeap.GetAddressOf())));

    memset(&heapDesc, 0, sizeof(heapDesc));
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = 1;
    CHK(Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(DepthStencilDescHeap.GetAddressOf())));

    memset(&heapDesc, 0, sizeof(heapDesc));
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NumDescriptors = 100;
    CHK(Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(ShaderResourceDescHeap.GetAddressOf())));
    ShaderResourceDescHandle = ShaderResourceDescHeap->GetCPUDescriptorHandleForHeapStart();
    DescIncrementSize = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    memset(&heapDesc, 0, sizeof(heapDesc));
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NumDescriptors = 1;
    CHK(Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(SamplerDescHeap.GetAddressOf())));

    for (int32_t i = 0; i < _countof(CmdAllocators); ++i)
    {
        CHK(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(CmdAllocators[i].GetAddressOf())));
        CHK(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CmdAllocators[i].Get(), nullptr, IID_PPV_ARGS(CmdLists[i].GetAddressOf())));
        CHK(CmdLists[i]->Close());
    }

    for (int32_t i = 0; i < _countof(BundleAllocators); ++i)
    {
        CHK(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(BundleAllocators[i].GetAddressOf())));
        CHK(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE, BundleAllocators[i].Get(), nullptr, IID_PPV_ARGS(Bundles[i].GetAddressOf())));
    }

    CHK(SwapChain->GetBuffer(0, IID_PPV_ARGS(BackBuffer.GetAddressOf())));
    Device->CreateRenderTargetView(BackBuffer.Get(), nullptr, RenderTargetDescHeap->GetCPUDescriptorHandleForHeapStart());

    RECT clientRect;
    GetClientRect(Window, &clientRect);
    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, clientRect.right, clientRect.bottom, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    CD3DX12_CLEAR_VALUE depthClearValue(DXGI_FORMAT_D24_UNORM_S8_UINT, 1.0f, 0);
    CHK(Device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(DepthBuffer.GetAddressOf())));
    depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilViewDesc.Texture2D.MipSlice = 0;
    depthStencilViewDesc.Flags = D3D12_DSV_FLAG_NONE;
    Device->CreateDepthStencilView(DepthBuffer.Get(), &depthStencilViewDesc, DepthStencilDescHeap->GetCPUDescriptorHandleForHeapStart());

    CHK(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(RenderFence.GetAddressOf())));

    RenderedEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);

    //D3D12_SAMPLER_DESC samplerDesc;
    //memset(&samplerDesc, 0, sizeof(samplerDesc));
    //samplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
    //samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    //samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    //samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    //samplerDesc.MaxAnisotropy = 16;
    //samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    //Device->CreateSampler(&samplerDesc, SamplerDescHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_STATIC_SAMPLER_DESC samplerDescriptions[1];
    samplerDescriptions[0].Init(0);

    CD3DX12_ROOT_PARAMETER rootParams[3];
    rootParams[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
//    rootParams[3].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_DESCRIPTOR_RANGE descRanges[3];
//    descRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);
    descRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
    descRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
//    rootParams[3].InitAsDescriptorTable(1, descRanges);
    rootParams[2].InitAsDescriptorTable(1, descRanges + 1);
    rootParams[1].InitAsDescriptorTable(1, descRanges + 2, D3D12_SHADER_VISIBILITY_PIXEL);
    ComPtr<ID3DBlob> pOutBlob, pErrorBlob;
    CD3DX12_ROOT_SIGNATURE_DESC rootSignature;
    rootSignature.Init(_countof(rootParams), rootParams, _countof(samplerDescriptions), samplerDescriptions, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    CHK(D3D12SerializeRootSignature(&rootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob));
    CHK(Device->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(RootSignature.GetAddressOf())));

    D3D12_INPUT_ELEMENT_DESC inputElemDesc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT , D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT , D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT , D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(
        TRUE,
        D3D12_DEPTH_WRITE_MASK_ALL,
        D3D12_COMPARISON_FUNC_LESS,
        FALSE,
        D3D12_DEFAULT_STENCIL_READ_MASK,
        D3D12_DEFAULT_STENCIL_WRITE_MASK,
        D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_KEEP,
        D3D12_COMPARISON_FUNC_ALWAYS,
        D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_KEEP,
        D3D12_COMPARISON_FUNC_ALWAYS
    );

    CD3DX12_RASTERIZER_DESC rasterizerDesc(
        D3D12_FILL_MODE_SOLID,
//        D3D12_FILL_MODE_WIREFRAME,
        D3D12_CULL_MODE_BACK,
        TRUE,
        D3D12_DEFAULT_DEPTH_BIAS,
        D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
        D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
        TRUE,
        FALSE,
        FALSE,
        0,
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
    );

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc;
    memset(&pipelineDesc, 0, sizeof(pipelineDesc));
    pipelineDesc.pRootSignature = RootSignature.Get();
    pipelineDesc.VS.pShaderBytecode = SimpleTransformVS;
    pipelineDesc.VS.BytecodeLength = sizeof(SimpleTransformVS);
    pipelineDesc.PS.pShaderBytecode = SimpleTexturePS;//SimplePS;
    pipelineDesc.PS.BytecodeLength = sizeof(SimpleTexturePS);//sizeof(SimplePS);
    pipelineDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pipelineDesc.SampleMask = UINT_MAX;
    pipelineDesc.DepthStencilState = depthStencilDesc;
    pipelineDesc.RasterizerState = rasterizerDesc;
    pipelineDesc.InputLayout.pInputElementDescs = inputElemDesc;
    pipelineDesc.InputLayout.NumElements = _countof(inputElemDesc);
    pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineDesc.NumRenderTargets = 1;
    pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipelineDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pipelineDesc.SampleDesc.Count = 1;
    CHK(Device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(PipelineStates[0].GetAddressOf())));

    for (size_t iBuf = 0; iBuf < _countof(GlobalConstantBuffers); ++iBuf)
    {
        size_t bufSize = sizeof(LightConstants);
        bufSize = (bufSize + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT-1) / D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        CreateUploadBuffer(nullptr, bufSize, &GlobalConstantBuffers[iBuf]);
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbDesc = {GlobalConstantBuffers[iBuf]->GetGPUVirtualAddress(), (UINT)bufSize};
        Device->CreateConstantBufferView(&cbDesc, ShaderResourceDescHandle);
        GlobalConstantDescOffsets[iBuf] = (UINT)(ShaderResourceDescHandle.ptr - ShaderResourceDescHeap->GetCPUDescriptorHandleForHeapStart().ptr) / DescIncrementSize;
        ShaderResourceDescHandle.ptr += DescIncrementSize;
    }

    return true;
}

bool Renderer::Render(FXMVECTOR cameraPosition, FXMMATRIX view, FXMMATRIX projection, bool vsync)
{
    int32_t cmdIdx = RenderFenceIdx & 0x01;

    // Generate command list
    CHK(CmdAllocators[cmdIdx]->Reset());

    ID3D12GraphicsCommandList* pCmdList = CmdLists[cmdIdx].Get();
    CHK(pCmdList->Reset(CmdAllocators[cmdIdx].Get(), nullptr));

    SetResourceBarrier(pCmdList, BackBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    float clearClr[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    auto rtDescHandle = RenderTargetDescHeap->GetCPUDescriptorHandleForHeapStart();
    pCmdList->ClearRenderTargetView(rtDescHandle, clearClr, 0, nullptr);
    auto dsvDescHandle = DepthStencilDescHeap->GetCPUDescriptorHandleForHeapStart();
    pCmdList->ClearDepthStencilView(dsvDescHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    pCmdList->OMSetRenderTargets(1, &rtDescHandle, TRUE, &dsvDescHandle);

    RECT clientRect;
    GetClientRect(Window, &clientRect);
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)clientRect.right, (float)clientRect.bottom, 0.0f, 1.0f };
    pCmdList->RSSetViewports(1, &viewport);
    D3D12_RECT scissor = { 0, 0, clientRect.right, clientRect.bottom };
    pCmdList->RSSetScissorRects(1, &scissor);

#if 0
    pCmdList->SetGraphicsRootDescriptorTable(3, SamplerDescHeap->GetGPUDescriptorHandleForHeapStart());
    ID3D12DescriptorHeap* descHeaps[] = { SamplerDescHeap.Get(), ShaderResourceDescHeap.Get() };
#else
    ID3D12DescriptorHeap* descHeaps[] = { ShaderResourceDescHeap.Get() };
#endif
    pCmdList->SetDescriptorHeaps(_countof(descHeaps), descHeaps);

    LightConstants* pLightData;
    GlobalConstantBuffers[cmdIdx]->Map(0, nullptr, reinterpret_cast<void**>(&pLightData));
    XMStoreFloat3(&pLightData->EyePosition, cameraPosition);
    pLightData->NumLights = 0;
    pLightData->Lights[0].Direction = XMFLOAT3(1.f, 1.f, 1.f);
    pLightData->Lights[0].Color = XMFLOAT3(0.6f, 0.6f, 0.6f);
    pLightData->Lights[1].Direction = XMFLOAT3(-1.f, 1.f, -1.f);
    pLightData->Lights[1].Color = XMFLOAT3(0.5f, 0.5f, 0.5f);

    pLightData->NumPointLights = 3;
    pLightData->PointLights[0].Position = XMFLOAT3(0.f, 300.f, 0.f);
    pLightData->PointLights[0].Color = XMFLOAT3(0.6f, 0.6f, 0.6f);
    pLightData->PointLights[0].Radius = 500.f;
    pLightData->PointLights[1].Position = XMFLOAT3(-800.f, 300.f, 0.f);
    pLightData->PointLights[1].Color = XMFLOAT3(0.6f, 0.6f, 0.9f);
    pLightData->PointLights[1].Radius = 500.f;
    pLightData->PointLights[2].Position = XMFLOAT3(800.f, 300.f, 0.f);
    pLightData->PointLights[2].Color = XMFLOAT3(0.9f, 0.6f, 0.6f);
    pLightData->PointLights[2].Radius = 500.f;
    GlobalConstantBuffers[cmdIdx]->Unmap(0, nullptr);

    XMFLOAT4X4 viewProjection;
    XMStoreFloat4x4(&viewProjection, view * projection);

    auto pBundle = Bundles[0].Get();
    if (!BundleCreated)
    {
        pBundle->SetGraphicsRootSignature(RootSignature.Get());
#if 0
        pBundle->SetGraphicsRootDescriptorTable(3, SamplerDescHeap->GetGPUDescriptorHandleForHeapStart());
        ID3D12DescriptorHeap* descHeaps[] = { SamplerDescHeap.Get(), ShaderResourceDescHeap.Get() };
#else
        ID3D12DescriptorHeap* descHeaps[] = { ShaderResourceDescHeap.Get() };
#endif
        pBundle->SetDescriptorHeaps(_countof(descHeaps), descHeaps); // Descriptor heaps need to match cmdList's descriptor heaps

#if 1
        CD3DX12_GPU_DESCRIPTOR_HANDLE globalConstantsHandle(ShaderResourceDescHeap->GetGPUDescriptorHandleForHeapStart(), GlobalConstantDescOffsets[cmdIdx], DescIncrementSize);
        pBundle->SetGraphicsRootDescriptorTable(1, globalConstantsHandle);
#else
        pBundle->SetGraphicsRootConstantBufferView(3, GlobalConstantBuffers[cmdIdx]->GetGPUVirtualAddress());
#endif
        pBundle->SetPipelineState(PipelineStates[0].Get());
        pBundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pBundle->IASetVertexBuffers(0, 1, &TheScene->VtxBufView);
        pBundle->IASetIndexBuffer(&TheScene->IdxBufView);

        for (auto& object : TheScene->Objects)
        {
#if 1
	        UINT8* pData;
	        object->ConstantBuffers[cmdIdx]->Map(0, nullptr, reinterpret_cast<void**>(&pData));
            if (pData != nullptr)
            {
                float identityMtx[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                memcpy(pData, identityMtx, 16 * sizeof(float));
	            memcpy(pData + 16 * sizeof(float), &viewProjection.m[0][0], 16 * sizeof(float));
	            object->ConstantBuffers[cmdIdx]->Unmap(0, nullptr);
            }

            pBundle->SetGraphicsRootConstantBufferView(0, object->ConstantBuffers[cmdIdx]->GetGPUVirtualAddress());
#else
            pBundle->SetGraphicsRoot32BitConstants(0, &object->World.m[0][0], 0, 16);
            pBundle->SetGraphicsRoot32BitConstants(0, &viewProjection.m[0][0], 16, 16);
#endif
            for (auto& mesh : object->Meshes)
            {
                CD3DX12_GPU_DESCRIPTOR_HANDLE albedoHandle(ShaderResourceDescHeap->GetGPUDescriptorHandleForHeapStart(), mesh.AlbedoDescIdx, DescIncrementSize);
                pBundle->SetGraphicsRootDescriptorTable(2, albedoHandle);
                pBundle->DrawIndexedInstanced(mesh.NumIndices, 1, mesh.StartIndex, 0, 0);
            }
        }

        CHK(pBundle->Close());
        BundleCreated = true;
    }
    else
    {
        // Just update world and projection matrices
        for (auto& object : TheScene->Objects)
        {
            UINT8* pData;
            object->ConstantBuffers[cmdIdx]->Map(0, nullptr, reinterpret_cast<void**>(&pData));
            if (pData != nullptr)
            {
                float identityMtx[] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
                memcpy(pData, identityMtx, 16 * sizeof(float));
                memcpy(pData + 16 * sizeof(float), &viewProjection.m[0][0], 16 * sizeof(float));
                object->ConstantBuffers[cmdIdx]->Unmap(0, nullptr);
            }
        }
    }

    pCmdList->ExecuteBundle(pBundle);

    SetResourceBarrier(pCmdList, BackBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    CHK(pCmdList->Close());

    // Execute the command list
    ID3D12CommandList* commandLists[] = { pCmdList };
    DefaultQueue->ExecuteCommandLists(1, commandLists);

    return Present(vsync);
}

bool Renderer::Present(bool vsync)
{
    CHK(SwapChain->Present(vsync ? 1 : 0, 0));

    BackBufferIdx = (BackBufferIdx + 1) % kNumBackBuffers;
    CHK(SwapChain->GetBuffer(BackBufferIdx, IID_PPV_ARGS(BackBuffer.ReleaseAndGetAddressOf())));
    Device->CreateRenderTargetView(BackBuffer.Get(), nullptr, RenderTargetDescHeap->GetCPUDescriptorHandleForHeapStart());

    // Wait until rendering is finished
    const auto fenceIdx = RenderFenceIdx++;
    CHK(DefaultQueue->Signal(RenderFence.Get(), fenceIdx));
    if (RenderFence->GetCompletedValue() < fenceIdx)
    {
        RenderFence->SetEventOnCompletion(fenceIdx, RenderedEvent);
        WaitForSingleObject(RenderedEvent, INFINITE);
    }

    return true;
}

void Renderer::SetResourceBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
{
	D3D12_RESOURCE_BARRIER barrier = {};

    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = stateBefore;
	barrier.Transition.StateAfter = stateAfter;

	commandList->ResourceBarrier(1, &barrier);
}

bool Renderer::CreateUploadBuffer(const void* pData, size_t size, ID3D12Resource** ppBuf)
{
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
	CHK(Device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, 
		nullptr,    // Clear value
		IID_PPV_ARGS(ppBuf)));

    if (pData != nullptr)
    {
	    void* pBufMem;
	    CHK((*ppBuf)->Map(0, nullptr, &pBufMem));
	    memcpy(pBufMem, pData, size);
	    (*ppBuf)->Unmap(0, nullptr);
    }

    return true;
}

bool Renderer::CreateBuffer(const void* pData, size_t size, ID3D12Resource** ppTempBuf, ID3D12Resource** ppFinalBuf)
{
    ComPtr<ID3D12Resource> pUploadBuf;
    if (!CreateUploadBuffer(pData, size, pUploadBuf.GetAddressOf()))
        return false;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
    CHK(Device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,    // Clear value
		IID_PPV_ARGS(ppFinalBuf)));

    *ppTempBuf = pUploadBuf.Detach();

    return true;
}
#if 0
bool Renderer::CreateTexture2D(const void* pData, size_t size, DXGI_FORMAT format, UINT width, UINT height, UINT16 arraySize, UINT16 mipLevels, ID3D12Resource** ppTempTex, ID3D12Resource** ppFinalTex)
{
    ComPtr<ID3D12Resource> pUploadTex;

    mipLevels = 1;

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, arraySize, mipLevels);
	CHK(Device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,    // Clear value
		IID_PPV_ARGS(pUploadTex.GetAddressOf())));

    if (pData != nullptr)
    {
#if 1
		UINT bytesPerPixel = (UINT)BitsPerPixel(format) / 8;

        UINT levelWidth = width;
        UINT levelHeight = height;
        const char* pPixels = reinterpret_cast<const char*>(pData);
        for (UINT16 iLevel = 0; iLevel < mipLevels; ++iLevel)
        {
            UINT rowBytes = levelWidth * bytesPerPixel;

            CHK(pUploadTex->Map(iLevel, nullptr, nullptr));
            CHK(pUploadTex->WriteToSubresource(iLevel, nullptr, pPixels, rowBytes, rowBytes * levelHeight));
            pUploadTex->Unmap(iLevel, nullptr);

            pPixels += rowBytes * levelHeight * arraySize;
            levelWidth = max(levelWidth >> 1, 1);
            levelHeight = max(levelHeight >> 1, 1);
        }

        assert(size_t(pPixels - reinterpret_cast<const char*>(pData)) <= size);
#else
        (void)size;
#endif
    }
#if 1
    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
	CHK(Device->CreateCommittedResource(
		&defaultHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,    // Clear value
		IID_PPV_ARGS(ppFinalTex)));

    *ppTempTex = pUploadTex.Detach();
#else
    *ppTempTex = nullptr;
    *ppFinalTex = pUploadTex.Detach();
#endif

    return true;
}
#else

bool Renderer::CreateTexture2D(const void* pData, size_t size, DXGI_FORMAT format, UINT width, UINT height, UINT16 arraySize, UINT16 mipLevels, ID3D12Resource** ppTempTex, ID3D12Resource** ppFinalTex)
{
    CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, arraySize, mipLevels);

    UINT64 requiredSize;
    UINT numSubresources = texDesc.DepthOrArraySize * texDesc.MipLevels;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(numSubresources);
    std::vector<UINT64> rowSizes(numSubresources);
    Device->GetCopyableFootprints(&texDesc, 0, numSubresources, 0, &layouts[0], nullptr, &rowSizes[0], &requiredSize);

    ComPtr<ID3D12Resource> pUploadTex;
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(requiredSize);
    CHK(Device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,    // Clear value
        IID_PPV_ARGS(pUploadTex.GetAddressOf())));

    if (pData != nullptr)
    {
        UINT8* pDestStart;
        const UINT8* pSrc = reinterpret_cast<const UINT8*>(pData);
        CHK(pUploadTex->Map(0, nullptr, reinterpret_cast<void**>(&pDestStart)));
        for (UINT iSubresource = 0; iSubresource < numSubresources; ++iSubresource)
        {
            auto pDest = pDestStart + layouts[iSubresource].Offset;
            for (UINT y = 0; y < layouts[iSubresource].Footprint.Height; ++y)
            {
                memcpy(pDest, pSrc, rowSizes[iSubresource]);
                pDest += layouts[iSubresource].Footprint.RowPitch;
                pSrc += rowSizes[iSubresource];
            }
        }
        pUploadTex->Unmap(0, nullptr); // TODO: Do we need to pass in the written range???

        assert(pSrc - reinterpret_cast<const UINT8*>(pData) == (ptrdiff_t)size);
    }

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CHK(Device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,    // Clear value
        IID_PPV_ARGS(ppFinalTex)));

    *ppTempTex = pUploadTex.Detach();

    return true;
}
#endif
bool Renderer::AddMeshes(const std::wstring& contentRoot, const std::wstring& modelFilename)
{
    ComResourceMap resourceMap;
    ComPtr<ID3D12Resource> tempResource;

    static_assert(sizeof(ModelVertex) == sizeof(Vertex), "Make sure structures (and padding) match so we can read directly!");

    FileHandle modelFile(CreateFile((contentRoot + modelFilename).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!modelFile.IsValid())
    {
        LogError(L"Failed to open asset file.");
        return false;
    }

    DWORD bytesRead{};
    ModelHeader header{};
    if (!ReadFile(modelFile.Get(), &header, sizeof(header), &bytesRead, nullptr))
    {
        LogError(L"Failed to read file.");
        return false;
    }

    if (header.Signature != header.ExpectedSignature)
    {
        LogError(L"Invalid model file.");
        return false;
    }

    TheScene.reset(new Scene);

    TheScene->VertexCount = header.NumVertices;
    TheScene->IndexCount = header.NumIndices;

    std::unique_ptr<Vertex[]> vertices(new Vertex[header.NumVertices]);
    if (!ReadFile(modelFile.Get(), vertices.get(), header.NumVertices * sizeof(Vertex), &bytesRead, nullptr))
    {
        LogError(L"Failed to read file.");
        return false;
    }

    if (!CreateBuffer(vertices.get(), TheScene->VertexCount * sizeof(Vertex), tempResource.ReleaseAndGetAddressOf(), TheScene->VertexBuffer.GetAddressOf()))
        return false;

    resourceMap.emplace(tempResource, TheScene->VertexBuffer);
    TheScene->VtxBufView.BufferLocation = TheScene->VertexBuffer->GetGPUVirtualAddress();
    TheScene->VtxBufView.SizeInBytes = TheScene->VertexCount * sizeof(Vertex);
    TheScene->VtxBufView.StrideInBytes = sizeof(Vertex);

    // Free up memory
    vertices.reset();

    std::unique_ptr<uint32_t[]> indices(new uint32_t[header.NumIndices]);
    if (!ReadFile(modelFile.Get(), indices.get(), header.NumIndices * sizeof(uint32_t), &bytesRead, nullptr))
    {
        LogError(L"Failed to read file.");
        return false;
    }

    if (!CreateBuffer(indices.get(), TheScene->IndexCount * sizeof(uint32_t), tempResource.ReleaseAndGetAddressOf(), TheScene->IndexBuffer.GetAddressOf()))
        return false;

    resourceMap.emplace(tempResource, TheScene->IndexBuffer);
    TheScene->IdxBufView.BufferLocation = TheScene->IndexBuffer->GetGPUVirtualAddress();
    TheScene->IdxBufView.Format = DXGI_FORMAT_R32_UINT;
    TheScene->IdxBufView.SizeInBytes = TheScene->IndexCount * sizeof(uint32_t);

    // Free up memory
    indices.reset();

    D3D12_SHADER_RESOURCE_VIEW_DESC emptyTexDesc;
    memset(&emptyTexDesc, 0, sizeof(emptyTexDesc));
    emptyTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    emptyTexDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    emptyTexDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    emptyTexDesc.Texture2D.MipLevels = 1;

    // Load objects
    for (int iObj = 0; iObj < (int)header.NumObjects; ++iObj)
    {
        ModelObject object{};
        if (!ReadFile(modelFile.Get(), &object, sizeof(object), &bytesRead, nullptr))
        {
            LogError(L"Failed to read file.");
            return false;
        }

        std::shared_ptr<Object> obj = std::make_shared<Object>();
        obj->Name = object.Name;
        XMStoreFloat4x4(&obj->World, XMMatrixIdentity());

        for (int32_t i = 0; i < 2; ++i)
        {
            CreateUploadBuffer(nullptr, 32 * sizeof(float), obj->ConstantBuffers[i].GetAddressOf());
        }

        for (int iPart = 0; iPart < (int)object.NumParts; ++iPart)
        {
            ModelPart part{};
            if (!ReadFile(modelFile.Get(), &part, sizeof(part), &bytesRead, nullptr))
            {
                LogError(L"Failed to read file.");
                return false;
            }

            Mesh mesh{};
            mesh.StartIndex = part.StartIndex;
            mesh.NumIndices = part.NumIndices;

            if (part.DiffuseTexture[0] != 0)
            {
                if (!LoadTexture(contentRoot + part.DiffuseTexture, tempResource.ReleaseAndGetAddressOf(), mesh.AlbedoTex.GetAddressOf(), &mesh.AlbedoDescIdx))
                {
                    LogError(L"Failed to load texture.");
                    return false;
                }

                resourceMap.emplace(tempResource, mesh.AlbedoTex);
            }

            if (part.NormalTexture[0] != 0)
            {
                if (!LoadTexture(contentRoot + part.NormalTexture, tempResource.ReleaseAndGetAddressOf(), mesh.BumpDerivativeTex.GetAddressOf(), &mesh.BumpDerivativeDescIdx))
                {
                    LogError(L"Failed to load texture.");
                    return false;
                }

                resourceMap.emplace(tempResource, mesh.BumpDerivativeTex);
            }
            else
            {
                if (tempResource != nullptr)
                {
                    mesh.BumpDerivativeDescIdx = CreateShaderResourceView(nullptr, emptyTexDesc);
                }
                else
                {
                    mesh.BumpDerivativeDescIdx = mesh.AlbedoDescIdx + 1;
                }
            }

            if (part.SpecularTexture[0] != 0)
            {
                if (!LoadTexture(contentRoot + part.SpecularTexture, tempResource.ReleaseAndGetAddressOf(), mesh.SpecularTex.GetAddressOf(), &mesh.SpecularDescIdx))
                {
                    LogError(L"Failed to load texture.");
                    return false;
                }

                resourceMap.emplace(tempResource, mesh.SpecularTex);
            }
            else
            {
                if (tempResource != nullptr)
                {
                    mesh.SpecularDescIdx = CreateShaderResourceView(nullptr, emptyTexDesc);
                }
                else
                {
                    mesh.SpecularDescIdx = mesh.BumpDerivativeDescIdx + 1;
                }
            }

            obj->Meshes.push_back(mesh);
        }

        TheScene->Objects.push_back(obj);
    }

    // Generate command list
    CHK(CmdAllocators[0]->Reset());
    ID3D12GraphicsCommandList* pCmdList = CmdLists[0].Get();
    CHK(pCmdList->Reset(CmdAllocators[0].Get(), nullptr));
    for (const auto& pair : resourceMap)
    {
        if (pair.first == nullptr)
            continue;

        auto destDesc = pair.second->GetDesc();
        D3D12_RESOURCE_STATES newState = (destDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if (destDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            pCmdList->CopyResource(pair.second.Get(), pair.first.Get());
        }
        else
        {
            UINT numSubresources = destDesc.DepthOrArraySize * destDesc.MipLevels;
            std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(numSubresources);
            Device->GetCopyableFootprints(&destDesc, 0, numSubresources, 0, &layouts[0], nullptr, nullptr, nullptr);
            for (UINT iSubresource = 0; iSubresource < numSubresources; ++iSubresource)
            {
                CD3DX12_TEXTURE_COPY_LOCATION srcLoc(pair.first.Get(), layouts[iSubresource]);
                CD3DX12_TEXTURE_COPY_LOCATION destLoc(pair.second.Get(), iSubresource);
                pCmdList->CopyTextureRegion(&destLoc, 0, 0, 0, &srcLoc, nullptr);
            }
        }

        SetResourceBarrier(pCmdList, pair.second.Get(), D3D12_RESOURCE_STATE_COPY_DEST, newState);
    }
    CHK(pCmdList->Close());

    // Execute the command list
    ID3D12CommandList* commandLists[] = { pCmdList };
    DefaultQueue->ExecuteCommandLists(1, commandLists);

    // Wait for transfers to complete
    const auto fenceIdx = RenderFenceIdx++;
    CHK(DefaultQueue->Signal(RenderFence.Get(), fenceIdx));
    if (RenderFence->GetCompletedValue() < fenceIdx)
    {
        RenderFence->SetEventOnCompletion(fenceIdx, RenderedEvent);
        WaitForSingleObject(RenderedEvent, INFINITE);
    }

    return true;
}

bool Renderer::LoadTexture(const std::wstring& filename, ID3D12Resource** ppTempTex, ID3D12Resource** ppFinalTex, UINT* pDescIdx)
{
    assert(ppTempTex);
    assert(ppFinalTex);
    assert(pDescIdx);

    auto iter = LoadedTextureMaps.find(filename);
    if (iter != LoadedTextureMaps.end())
    {
        *ppTempTex = nullptr;
        iter->second.first->AddRef();
        *ppFinalTex = iter->second.first;
        *pDescIdx = iter->second.second;
        return true;
    }

    FileHandle texFile(CreateFile(filename.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!texFile.IsValid())
    {
        LogError(L"Failed to open texture.");
        return false;
    }

    DWORD bytesRead{};
    uint32_t fileSize = GetFileSize(texFile.Get(), nullptr);

    TextureHeader texHeader{};
    if (!ReadFile(texFile.Get(), &texHeader, sizeof(texHeader), &bytesRead, nullptr))
    {
        LogError(L"Failed to read texture.");
        return false;
    }

    if (texHeader.Signature != TextureHeader::ExpectedSignature)
    {
        LogError(L"Invalid texture file.");
        return false;
    }

    uint32_t pixelDataSize = fileSize - sizeof(TextureHeader);
    std::unique_ptr<uint8_t[]> pixelData(new uint8_t[pixelDataSize]);
    if (!ReadFile(texFile.Get(), pixelData.get(), pixelDataSize, &bytesRead, nullptr))
    {
        LogError(L"Failed to read texture data.");
        return false;
    }

#if USE_SRGB
    if (td.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
    {
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }
    else if (td.Format == DXGI_FORMAT_B8G8R8A8_UNORM)
    {
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    }
#endif

    HRESULT hr = CreateTexture2D(pixelData.get(), pixelDataSize, texHeader.Format, texHeader.Width, texHeader.Height, (UINT16)texHeader.ArrayCount, (UINT16)texHeader.MipLevels, ppTempTex, ppFinalTex);
    if (FAILED(hr))
        return false;

    D3D12_SHADER_RESOURCE_VIEW_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Format = texHeader.Format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture2D.MipLevels = UINT_MAX; // Use all mip levels
    UINT descIdx = CreateShaderResourceView(*ppFinalTex, desc);
    LoadedTextureMaps.emplace(filename, std::make_pair(*ppFinalTex, descIdx));

    if (pDescIdx)
    {
        *pDescIdx = descIdx;
    }

    return true;
}

UINT Renderer::CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
{
    Device->CreateShaderResourceView(pResource, &desc, ShaderResourceDescHandle);

    auto descHeapStart = ShaderResourceDescHeap->GetCPUDescriptorHandleForHeapStart();
    UINT descIdx = (UINT)(ShaderResourceDescHandle.ptr - descHeapStart.ptr) / DescIncrementSize;
    ShaderResourceDescHandle.ptr += DescIncrementSize;

    return descIdx;
}

#endif // DX12 support
