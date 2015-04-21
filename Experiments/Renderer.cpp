#include "Precomp.h"
#include "Renderer.h"
#include "Debug.h"
#include "Shaders/SimpleTransformVS.h"
//#include "Shaders/SimpleTexturePS.h"
#include "Shaders/SimplePS.h"

#if defined (ENABLE_DX12_SUPPORT)
#pragma comment(lib, "d3d12.lib")

#define CHK(a) { HRESULT hr = a; if (FAILED(hr)) { assert(false); return false; } }

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

Renderer::Renderer(HWND window)
    : Window(window)
{
}

Renderer::~Renderer()
{
}

bool Renderer::Initialize()
{
    D3D12_CREATE_DEVICE_FLAG d3dFlag = D3D12_CREATE_DEVICE_NONE;
#if defined(_DEBUG)
    d3dFlag |= D3D12_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D12CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, d3dFlag,
        D3D_FEATURE_LEVEL_11_0, D3D12_SDK_VERSION, IID_PPV_ARGS(Device.GetAddressOf()));
    if (FAILED(hr))
    {
        // Did it fail because we're requesting the debug layer and it's not present
        // on this machine (and, for D3D12 preview, in the directory of the exe)?
        if (d3dFlag == D3D12_CREATE_DEVICE_DEBUG && hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
        {
            // Try again without debug layer
            d3dFlag &= ~D3D12_CREATE_DEVICE_DEBUG;
            hr = D3D12CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, d3dFlag,
                D3D_FEATURE_LEVEL_11_0, D3D12_SDK_VERSION, IID_PPV_ARGS(Device.GetAddressOf()));
        }

        if (FAILED(hr))
        {
            LogError(L"Failed to create D3D12 device.");
            return false;
        }
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = 0;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_NONE;
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
    heapDesc.Type = D3D12_RTV_DESCRIPTOR_HEAP;
    heapDesc.NumDescriptors = 1;
    CHK(Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(RenderTargetDescHeap.GetAddressOf())));

    memset(&heapDesc, 0, sizeof(heapDesc));
    heapDesc.Type = D3D12_DSV_DESCRIPTOR_HEAP;
    heapDesc.NumDescriptors = 1;
    CHK(Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(DepthStencilDescHeap.GetAddressOf())));

    memset(&heapDesc, 0, sizeof(heapDesc));
    heapDesc.Type = D3D12_CBV_SRV_UAV_DESCRIPTOR_HEAP;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_SHADER_VISIBLE;
    heapDesc.NumDescriptors = 100;
    CHK(Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(DefaultDescHeap.GetAddressOf())));

    for (int32_t i = 0; i < 2; ++i)
    {
        CHK(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(CmdAllocators[i].GetAddressOf())));
        CHK(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CmdAllocators[i].Get(), nullptr, IID_PPV_ARGS(CmdLists[i].GetAddressOf())));
        CHK(CmdLists[i]->Close());
    }

    CHK(SwapChain->GetBuffer(0, IID_PPV_ARGS(BackBuffer.GetAddressOf())));
    Device->CreateRenderTargetView(BackBuffer.Get(), nullptr, RenderTargetDescHeap->GetCPUDescriptorHandleForHeapStart());

    RECT clientRect;
    GetClientRect(Window, &clientRect);
    CD3D12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3D12_RESOURCE_DESC texDesc = CD3D12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D24_UNORM_S8_UINT, clientRect.right, clientRect.bottom, 1, 1, 1, 0, D3D12_RESOURCE_MISC_ALLOW_DEPTH_STENCIL);
    CD3D12_CLEAR_VALUE depthClearValue(DXGI_FORMAT_D24_UNORM_S8_UINT, 1.0f, 0);
    CHK(Device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_MISC_NONE, &texDesc, D3D12_RESOURCE_USAGE_DEPTH, &depthClearValue, IID_PPV_ARGS(DepthBuffer.GetAddressOf())));
    depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilViewDesc.Texture2D.MipSlice = 0;
    depthStencilViewDesc.Flags = D3D12_DSV_NONE;
    Device->CreateDepthStencilView(DepthBuffer.Get(), &depthStencilViewDesc, DepthStencilDescHeap->GetCPUDescriptorHandleForHeapStart());

    CHK(Device->CreateFence(0, D3D12_FENCE_MISC_NONE, IID_PPV_ARGS(RenderFence.GetAddressOf())));

    RenderedEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);

    D3D12_ROOT_PARAMETER rootParams[1];
//    rootParams[0].InitAsConstants(16, 0);
    rootParams[0].InitAsConstantBufferView(0);

    ComPtr<ID3DBlob> pOutBlob, pErrorBlob;
    D3D12_ROOT_SIGNATURE rootSignature;
    rootSignature.Init(_countof(rootParams), rootParams, 0, nullptr, D3D12_ROOT_SIGNATURE_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    CHK(D3D12SerializeRootSignature(&rootSignature, D3D_ROOT_SIGNATURE_V1, &pOutBlob, &pErrorBlob));
    CHK(Device->CreateRootSignature(0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS(RootSignature.GetAddressOf())));

    D3D12_INPUT_ELEMENT_DESC inputElemDesc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT , D3D12_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT , D3D12_INPUT_PER_VERTEX_DATA, 0},
        {"BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT , D3D12_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_PER_VERTEX_DATA, 0},
    };

    CD3D12_DEPTH_STENCIL_DESC depthStencilDesc(
        TRUE,
        D3D12_DEPTH_WRITE_MASK_ALL,
        D3D12_COMPARISON_LESS,
        FALSE,
        D3D12_DEFAULT_STENCIL_READ_MASK,
        D3D12_DEFAULT_STENCIL_WRITE_MASK,
        D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_KEEP,
        D3D12_COMPARISON_ALWAYS,
        D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_KEEP,
        D3D12_STENCIL_OP_KEEP,
        D3D12_COMPARISON_ALWAYS
    );

    CD3D12_RASTERIZER_DESC rasterizerDesc(
        D3D12_FILL_SOLID,
        D3D12_CULL_BACK,
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
    pipelineDesc.PS.pShaderBytecode = SimplePS;
    pipelineDesc.PS.BytecodeLength = sizeof(SimplePS);
    pipelineDesc.BlendState = CD3D12_BLEND_DESC(D3D12_DEFAULT);
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

    return true;
}

bool Renderer::Render(FXMVECTOR /*cameraPosition*/, FXMMATRIX view, FXMMATRIX projection, bool vsync)
{
    int32_t cmdIdx = RenderFenceIdx & 0x01;

    // Generate command list
    CHK(CmdAllocators[cmdIdx]->Reset());

    ID3D12GraphicsCommandList* pCmdList = CmdLists[cmdIdx].Get();
    CHK(pCmdList->Reset(CmdAllocators[cmdIdx].Get(), nullptr));

    pCmdList->SetGraphicsRootSignature(RootSignature.Get());

    SetResourceBarrier(pCmdList, BackBuffer.Get(), D3D12_RESOURCE_USAGE_PRESENT, D3D12_RESOURCE_USAGE_RENDER_TARGET);

    float clearClr[] = {0.5f, 0.5f, 0.5f, 1.0f};
    auto rtDescHandle = RenderTargetDescHeap->GetCPUDescriptorHandleForHeapStart();
    pCmdList->ClearRenderTargetView(rtDescHandle, clearClr, nullptr, 0);
    auto dsvDescHandle = DepthStencilDescHeap->GetCPUDescriptorHandleForHeapStart();
    pCmdList->ClearDepthStencilView(dsvDescHandle, D3D12_CLEAR_DEPTH | D3D12_CLEAR_STENCIL, 1.0f, 0, nullptr, 0);
    pCmdList->SetRenderTargets(&rtDescHandle, TRUE, 1, &dsvDescHandle);

    RECT clientRect;
    GetClientRect(Window, &clientRect);
    D3D12_VIEWPORT viewport = {0.0f, 0.0f, (float)clientRect.right, (float)clientRect.bottom, 0.0f, 1.0f};
    pCmdList->RSSetViewports(1, &viewport);
    D3D12_RECT scissor = {0, 0, clientRect.right, clientRect.bottom};
    pCmdList->RSSetScissorRects(1, &scissor);

    XMFLOAT4X4 viewProjection;
    XMStoreFloat4x4(&viewProjection, view * projection);

    pCmdList->SetPipelineState(PipelineStates[0].Get());
    pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pCmdList->SetVertexBuffers(0, &TheScene->VtxBufView, 1);
    pCmdList->SetIndexBuffer(&TheScene->IdxBufView);
    for (auto& object : TheScene->Objects)
    {
#if 1
	    UINT8* pData;
	    object->ConstantBuffers[cmdIdx]->Map(0, nullptr, reinterpret_cast<void**>(&pData));
        float identityMtx[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        memcpy(pData, identityMtx, 16 * sizeof(float));
	    memcpy(pData + 16 * sizeof(float), &viewProjection.m[0][0], 16 * sizeof(float));
	    object->ConstantBuffers[cmdIdx]->Unmap(0, nullptr);
        pCmdList->SetGraphicsRootConstantBufferView(0, object->ConstantBuffers[cmdIdx]->GetGPUVirtualAddress());
#else
        pCmdList->SetGraphicsRoot32BitConstants(0, &object->World.m[0][0], 0, 16);
        pCmdList->SetGraphicsRoot32BitConstants(0, &viewProjection.m[0][0], 16, 16);
#endif

        for (auto& mesh : object->Meshes)
        {
            pCmdList->DrawIndexedInstanced(mesh.NumIndices, 1, mesh.StartIndex, 0, 0);
        }
    }

    SetResourceBarrier(pCmdList, BackBuffer.Get(), D3D12_RESOURCE_USAGE_RENDER_TARGET, D3D12_RESOURCE_USAGE_PRESENT);

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

void Renderer::SetResourceBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, UINT stateBefore, UINT stateAfter)
{
	D3D12_RESOURCE_BARRIER_DESC descBarrier = {};

	descBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	descBarrier.Transition.pResource = resource;
	descBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	descBarrier.Transition.StateBefore = stateBefore;
	descBarrier.Transition.StateAfter = stateAfter;

	commandList->ResourceBarrier(1, &descBarrier);
}

bool Renderer::AddMeshes(const std::wstring& contentRoot, const std::wstring& modelFilename)
{
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

    CD3D12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3D12_RESOURCE_DESC vtxBufDesc = CD3D12_RESOURCE_DESC::Buffer(TheScene->VertexCount * sizeof(Vertex));
	CHK(Device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_MISC_NONE,
		&vtxBufDesc,
		D3D12_RESOURCE_USAGE_GENERIC_READ,
		nullptr,    // Clear value
		IID_PPV_ARGS(TheScene->VertexBuffer.GetAddressOf())));

    TheScene->VtxBufView.BufferLocation = TheScene->VertexBuffer->GetGPUVirtualAddress();
    TheScene->VtxBufView.SizeInBytes = TheScene->VertexCount * sizeof(Vertex);
    TheScene->VtxBufView.StrideInBytes = sizeof(Vertex);

	UINT8* pVtxData;
	TheScene->VertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pVtxData));
	memcpy(pVtxData, vertices.get(), TheScene->VertexCount * sizeof(Vertex));
	TheScene->VertexBuffer->Unmap(0, nullptr);

    // Free up memory
    vertices.reset();

    std::unique_ptr<uint32_t[]> indices(new uint32_t[header.NumIndices]);
    if (!ReadFile(modelFile.Get(), indices.get(), header.NumIndices * sizeof(uint32_t), &bytesRead, nullptr))
    {
        LogError(L"Failed to read file.");
        return false;
    }

    CD3D12_RESOURCE_DESC idxBufDesc = CD3D12_RESOURCE_DESC::Buffer(TheScene->IndexCount * sizeof(uint32_t));
	CHK(Device->CreateCommittedResource(
		&uploadHeapProps,
		D3D12_HEAP_MISC_NONE,
		&idxBufDesc,
		D3D12_RESOURCE_USAGE_GENERIC_READ,
		nullptr,    // Clear value
		IID_PPV_ARGS(TheScene->IndexBuffer.GetAddressOf())));

	UINT8* pIdxData;
	TheScene->IndexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pIdxData));
	memcpy(pIdxData, indices.get(), TheScene->IndexCount * sizeof(uint32_t));
	TheScene->IndexBuffer->Unmap(0, nullptr);

    TheScene->IdxBufView.BufferLocation = TheScene->IndexBuffer->GetGPUVirtualAddress();
    TheScene->IdxBufView.Format = DXGI_FORMAT_R32_UINT;
    TheScene->IdxBufView.SizeInBytes = TheScene->IndexCount * sizeof(uint32_t);

    // Free up memory
    indices.reset();

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
            CD3D12_RESOURCE_DESC bufDesc = CD3D12_RESOURCE_DESC::Buffer(32 * sizeof(float));
	        CHK(Device->CreateCommittedResource(
		        &uploadHeapProps,
		        D3D12_HEAP_MISC_NONE,
		        &bufDesc,
		        D3D12_RESOURCE_USAGE_GENERIC_READ,
		        nullptr,    // Clear value
		        IID_PPV_ARGS(obj->ConstantBuffers[i].GetAddressOf())));
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
                if (!LoadTexture(contentRoot + part.DiffuseTexture, &mesh.AlbedoSRV))
                {
                    LogError(L"Failed to load texture.");
                    return false;
                }
            }
            if (part.NormalTexture[0] != 0)
            {
                if (!LoadTexture(contentRoot + part.NormalTexture, &mesh.BumpDerivativeSRV))
                {
                    LogError(L"Failed to load texture.");
                    return false;
                }
            }

            obj->Meshes.push_back(mesh);
        }

        TheScene->Objects.push_back(obj);
    }

    return true;
}

bool Renderer::LoadTexture(const std::wstring& /*filename*/, void** /*srv*/)
{
    // TODO: Implement!
    return true;
}

#endif // DX12 support
