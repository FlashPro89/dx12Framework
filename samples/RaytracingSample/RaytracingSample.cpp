#include "RaytracingSample.h"
#include "../../compiled_shaders/RaytracingSample.hlsl.h"

const wchar_t *c_hitGroupName = L"MyHitGroup";
const wchar_t *c_raygenShaderName = L"MyRaygenShader";
const wchar_t *c_closestHitShaderName = L"MyClosestHitShader";
const wchar_t *c_missShaderName = L"MyMissShader";

constexpr int VSTRIDE = 44;
constexpr int CUBES_NUM = 60;
constexpr int CUBES_ROWS = 200;
constexpr float CUBES_ROW_HEIGHT = 1.f;
constexpr float CUBES_RADIUS = 10.f;

constexpr float PI = 3.141592f;
constexpr float PI2 = PI * 2.f;
constexpr float DEG2RAD = 3.141592f / 180.f;

// ------------------------------------
//
//		*** class RaytracingSample ***
//
// ------------------------------------

RaytracingSample::RaytracingSample(std::string name,
    DX12Framework::DX12FRAMEBUFFERING buffering, bool useWARP) :
    DX12Framework(name, buffering, useWARP)
{
}

RaytracingSample::~RaytracingSample()
{ 
}

bool RaytracingSample::initialize()
{
	if (!initDefault())
		return false;
	if (!initInput())
		return false;

    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    m_cpD3DDev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    bool tilingSupport = options.TiledResourcesTier != D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;
    ASSERT(tilingSupport, "Tiled Resources Not Supported By Video Adapter!");

    m_spCamera = std::make_shared<gCamera>(m_spInput.get());
    m_spCamera->setPosition(XMFLOAT3(0, 0, -4.f));
    m_spCamera->lookAt(XMFLOAT3(0, 0, 0));
    m_spCamera->setMovementSpeed(5.f);
    m_spCamera->setNearPlane(0.05f);
    m_spCamera->setFarPlane(150.f);
        
    if (!createRootSignatureAndPSO())
        return false;

    createRTRootSignatures();

    if (!m_spRingBuffer->initialize(0x3FFFFFF)) // 64MBytes
        return false;

    DX12MeshGenerator<0, 12, 24, 36> meshGen(m_spRingBuffer);
    DX12MeshGeneratorOffets offs;
    bool r = meshGen.buildIndexedCube(offs, 0.4f, 0.4f, 0.4f, false);

    //----------------------------------------------

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(offs.vDataSize);
    HRESULT hr;

    hr = m_cpD3DDev->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_cpVB));
    if (FAILED(hr))
        throw("3DDevice->CreateCommittedResource() failed!");

    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_cpVB.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_cpCommList->CopyBufferRegion(m_cpVB.Get(), 0, m_spRingBuffer->getResource(), offs.vDataOffset, offs.vDataSize);
        m_cpCommList->ResourceBarrier(1, &barrier);
        createSRVBuffer(m_cpVB.Get(), 1, offs.vertexesNum, VSTRIDE);
    }
    //----------------------------------------------
    desc = CD3DX12_RESOURCE_DESC::Buffer(offs.iDataSize);

    hr = m_cpD3DDev->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_cpIB));
    if (FAILED(hr))
        throw("3DDevice->CreateCommittedResource() failed!");

    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_cpIB.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        m_cpCommList->CopyBufferRegion(m_cpIB.Get(), 0, m_spRingBuffer->getResource(), offs.iDataOffset, offs.iDataSize);
        m_cpCommList->ResourceBarrier(1, &barrier);
        createSRVBuffer(m_cpIB.Get(), 2, offs.indexesNum, sizeof(WORD));
    }
    //----------------------------------------------

    m_vb.BufferLocation = m_cpVB->GetGPUVirtualAddress();
    m_vb.SizeInBytes = offs.vDataSize;
    m_vb.StrideInBytes = VSTRIDE;

    m_ib.BufferLocation = m_cpIB->GetGPUVirtualAddress();
    m_ib.SizeInBytes = offs.iDataSize;
    m_ib.Format = DXGI_FORMAT_R16_UINT; 

    {   // Base texture
        std::unique_ptr<uint8_t[]> ddsData;
        std::vector<D3D12_SUBRESOURCE_DATA> subResDataVector;
        ID3D12Resource* pResource;

        HRESULT tlResult = LoadDDSTextureFromFile(m_cpD3DDev.Get(), "../textures/stones.dds",
            &pResource, ddsData, subResDataVector);
        if (FAILED(tlResult))
        {
            MessageBox(0, "Cannot load texture!", "Error", MB_OK | MB_ICONERROR);
            return false;
        }

        uploadSubresources(pResource, static_cast<UINT>(subResDataVector.size()), &subResDataVector[0]);
        createSRVTex2D(pResource, 0);
        m_cpTexture = pResource;
    }

    initRaytracingResources();

    endCommandList();
    executeCommandList();
    WaitForGpu();

    m_spWindow->showWindow(true);

	return true;
}


bool RaytracingSample::beginCommandList()
{
    if (FAILED(m_cpCommAllocator->Reset()))
        return false;

    if (FAILED(m_cpCommList->Reset(m_cpCommAllocator.Get(), m_cpPipelineState[1].Get())))
        return false;

    return true;
}

bool RaytracingSample::populateCommandList()
{
    // Set necessary state.
    m_cpCommList->SetGraphicsRootSignature(m_cpRootSignature[1].Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_cpSRVHeap.Get() };
    m_cpCommList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    CD3DX12_GPU_DESCRIPTOR_HANDLE h(m_cpSRVHeap->GetGPUDescriptorHandleForHeapStart(), 
        0, m_srvDescriptorSize );

    m_cpCommList->RSSetViewports(1, &m_viewport);
    m_cpCommList->RSSetScissorRects(1, &m_scissorRect);

    // Indicate that the back buffer will be used as a render target.
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_cpRenderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_cpCommList->ResourceBarrier(1, &barrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_cpRTVHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_cpDSVHeap->GetCPUDescriptorHandleForHeapStart());

    // With DS
    m_cpCommList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.4f, 0.2f, 1.0f };
    m_cpCommList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_cpCommList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

    m_cpCommList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cpCommList->IASetVertexBuffers(0, 1, &m_vb);
    m_cpCommList->IASetIndexBuffer(&m_ib);

    XMFLOAT4X4 fmWVP;
    XMMATRIX mWVP, mVP, mTranslation;
    mVP = m_spCamera->getViewProjMatrix();

    // ----------------------------------------------------
    mTranslation = XMMatrixTranslation(0.f, 0.f, 0.f);
    mWVP = mTranslation * mVP;
    XMStoreFloat4x4( &fmWVP, XMMatrixTranspose(mWVP) );


    // draw cube
    mTranslation = XMMatrixTranslation(0.f, 0.f, 0.f);
    mWVP = mTranslation * mVP;
    XMStoreFloat4x4(&fmWVP, XMMatrixTranspose(mWVP));

    m_cpCommList->SetGraphicsRootDescriptorTable(1, h);
    m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmWVP, 0);
    m_cpCommList->DrawIndexedInstanced(36, 1, 0, 0, 0);

    // Indicate that the back buffer will now be used to present.
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_cpRenderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_cpCommList->ResourceBarrier(1, &barrier);

    // run RT!:
    auto DispatchRays = [&](auto *commandList, auto *stateObject, auto *dispatchDesc)
    {
        // Since each shader table has only one shader record, the stride is same as the size.
        dispatchDesc->HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
        dispatchDesc->HitGroupTable.SizeInBytes = m_hitGroupShaderTable->GetDesc().Width;
        dispatchDesc->HitGroupTable.StrideInBytes = dispatchDesc->HitGroupTable.SizeInBytes;
        dispatchDesc->MissShaderTable.StartAddress = m_missShaderTable->GetGPUVirtualAddress();
        dispatchDesc->MissShaderTable.SizeInBytes = m_missShaderTable->GetDesc().Width;
        dispatchDesc->MissShaderTable.StrideInBytes = dispatchDesc->MissShaderTable.SizeInBytes;
        dispatchDesc->RayGenerationShaderRecord.StartAddress = m_rayGenShaderTable->GetGPUVirtualAddress();
        dispatchDesc->RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable->GetDesc().Width;
        dispatchDesc->Width = this->m_spWindow->getWindowParameters().width;
        dispatchDesc->Height = this->m_spWindow->getWindowParameters().height;
        dispatchDesc->Depth = 1;
        commandList->SetPipelineState1(stateObject);
        commandList->DispatchRays(dispatchDesc);
    };

    m_cpCommList->SetComputeRootSignature(m_cpRootSignature[2].Get());
   
    // Bind the heaps, acceleration structure and dispatch rays.   
    auto h0 = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cpSRVHeap->GetGPUDescriptorHandleForHeapStart(), 3, m_srvDescriptorSize);
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};

    auto h_vb = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cpSRVHeap->GetGPUDescriptorHandleForHeapStart(), 4, m_srvDescriptorSize);

    m_cpCommList->SetComputeRootDescriptorTable(0, h0);
    m_cpCommList->SetComputeRootShaderResourceView(1, m_topLevelAccelerationStructure->GetGPUVirtualAddress());
    m_cpCommList->SetComputeRoot32BitConstants(2, 24, &m_RTC[m_frameIndex], 0);
    m_cpCommList->SetComputeRootDescriptorTable(3, h); // stone texture, vb, ib

    DispatchRays(m_cpCommList.Get(), m_cpStateObject.Get(), &dispatchDesc);
  
    if (m_spInput->isKeyPressed(DIK_SPACE))
        return true;

    // TMP: RT output
    D3D12_RESOURCE_BARRIER preCopyBarriers[2];
    preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_cpRenderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_cpCommList->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);

    m_cpCommList->CopyResource(m_cpRenderTargets[m_frameIndex].Get(), m_raytracingOutput.Get());

    D3D12_RESOURCE_BARRIER postCopyBarriers[2];
    postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_cpRenderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    m_cpCommList->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);

    return true;
}

bool RaytracingSample::endCommandList()
{
    if (FAILED(m_cpCommList->Close()))
        return false;
    return true;
}

void RaytracingSample::executeCommandList()
{
    ID3D12CommandList* ppCommandLists[] = { m_cpCommList.Get() };
    m_cpCommQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}


bool RaytracingSample::executeCommandListAndPresent()
{
    
    executeCommandList();

    if (FAILED(m_cpSwapChain->Present(1, 0)))
        return false;

    return true;
}

bool RaytracingSample::update()
{
    if (m_spInput)
        m_spInput->update();

    if (m_spCamera)
    {
        m_spCamera->tick(m_spTimer->getDelta());
        UpdateCameraMatrices();
    }

	return true;
}

bool RaytracingSample::render()
{
    if (!beginCommandList())
        return false;
    
    if (!populateCommandList())
        return false;

    if (!endCommandList())
        return false;

    if (!executeCommandListAndPresent())
        return false;

    WaitForPreviousFrame();
	return true;
}

bool RaytracingSample::createRootSignatureAndPSO()
{

// ROOT SIGNATURE:

    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(m_cpD3DDev->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

    CD3DX12_DESCRIPTOR_RANGE1 ranges[1]; // diffuse
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

    CD3DX12_ROOT_PARAMETER1 rootParameters[2];
    rootParameters[0].InitAsConstants(16, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX); // wvp
    rootParameters[1].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL); // srv descriptor

    D3D12_STATIC_SAMPLER_DESC sampler[1] = {};
    sampler[0].Filter = D3D12_FILTER_ANISOTROPIC;//D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[0].MipLODBias = 0;
    sampler[0].MaxAnisotropy = 16;
    sampler[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler[0].MinLOD = 0.0f;
    sampler[0].MaxLOD = D3D12_FLOAT32_MAX;
    sampler[0].ShaderRegister = 0;
    sampler[0].RegisterSpace = 0;
    sampler[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    if (FAILED(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error)))
        return false;

    if (FAILED(m_cpD3DDev->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(), IID_PPV_ARGS(&m_cpRootSignature[1]))))
        return false;


    // PSO :
#if defined(_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ID3DBlob *errorBlob;

    std::wstring w_fileName = L"../shaders/TemplateProject.hlsl";

    if (FAILED(D3DCompileFromFile(w_fileName.c_str(), nullptr, nullptr, "vs_main",
        "vs_5_1", compileFlags, 0, &vertexShader, &errorBlob)))
    {
        MessageBoxA(0, reinterpret_cast<const char *>(errorBlob->GetBufferPointer()), "Vertex Shader Error", MB_OK);
        errorBlob->Release();
        return false;
    }
    if (errorBlob)
    {
        if (errorBlob->GetBufferSize() > 0)
            MessageBoxA(0, reinterpret_cast<const char *>(errorBlob->GetBufferPointer()), "Vertex Shader Warning", MB_OK);
        errorBlob->Release();
    }

    if (FAILED(D3DCompileFromFile(w_fileName.c_str(), nullptr, nullptr, "ps_main",
        "ps_5_1", compileFlags, 0, &pixelShader, &errorBlob)))
    {
        MessageBoxA(0, reinterpret_cast<const char *>(errorBlob->GetBufferPointer()), "Vertex Shader Error", MB_OK);
        errorBlob->Release();
        return false;
    }
    if (errorBlob)
    {
        if (errorBlob->GetBufferSize() > 0)
            MessageBoxA(0, reinterpret_cast<const char *>(errorBlob->GetBufferPointer()), "Pixel Shader Warning", MB_OK);
        errorBlob->Release();
    }
    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_cpRootSignature[1].Get();
    psoDesc.VS = { reinterpret_cast<UINT8 *>(vertexShader->GetBufferPointer()), vertexShader->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<UINT8 *>(pixelShader->GetBufferPointer()), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    if (FAILED(m_cpD3DDev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_cpPipelineState[1]))))
        return false;

    return true;
}

void RaytracingSample::initRaytracingResources()
{
    // Create the output resource. The dimensions and format should match the swap-chain.
    auto w = m_spWindow->getWindowParameters().width;
    auto h = m_spWindow->getWindowParameters().height;
    auto backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(backbufferFormat, w, h, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    DXASSERT(m_cpD3DDev->CreateCommittedResource(
        &defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &uavDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracingOutput)),
        "Can't create raytracing output");
    m_raytracingOutput->SetName(L"m_raytracingOutput");

    D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_cpSRVHeap->GetCPUDescriptorHandleForHeapStart(), 3, m_srvDescriptorSize);
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
    UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_cpD3DDev->CreateUnorderedAccessView(m_raytracingOutput.Get(), nullptr, &UAVDesc, uavDescriptorHandle);


    // TMP: fill UAV texture
    auto _align = [](ADDRESS uLocation, ADDRESS uAlign) -> ADDRESS
    {
        if ((0 == uAlign) || (uAlign & (uAlign - 1)))
        {
            assert(true && "non-pow2 alignment");
        }
        return ((uLocation + (uAlign - 1)) & ~(uAlign - 1));
    };

    ADDRESS alligned_w_in_bytes = _align(w * 4, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    ADDRESS sz = alligned_w_in_bytes * h;
    ADDRESS offset{};
    void *res = m_spRingBuffer->allocate(this->m_frameIndex, sz, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, &offset);
 
    for (int r = 0; r < h; r++)
    {
        auto row_ptr = static_cast<unsigned char *>(res) + alligned_w_in_bytes * r;
        auto *p = reinterpret_cast<unsigned int *>(row_ptr);

        for (int c = 0; c < w; c++, p++)
            *p = static_cast<unsigned char>(((c / float(w)) * 256)) << 8 | 
                static_cast<unsigned char>(((r / float(h)) * 256)) << 16;
    }

    D3D12_BOX box;
    box.left = 0;
    box.right = w;
    box.top = 0;
    box.bottom = h;

    box.front = 0;
    box.back = 1;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    footprint.Footprint.Width = w;
    footprint.Footprint.Height = h;
    footprint.Footprint.Depth = 1;
    footprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    footprint.Footprint.RowPitch = static_cast<UINT>(alligned_w_in_bytes);
    footprint.Offset = offset;

    CD3DX12_TEXTURE_COPY_LOCATION Dst(m_raytracingOutput.Get(), 0);
    CD3DX12_TEXTURE_COPY_LOCATION Src(m_spRingBuffer->getResource(), footprint);

    CD3DX12_RESOURCE_BARRIER barrier_begin_copy = CD3DX12_RESOURCE_BARRIER::Transition(
        m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    CD3DX12_RESOURCE_BARRIER barrier_end_copy = CD3DX12_RESOURCE_BARRIER::Transition(
        m_raytracingOutput.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    m_cpCommList->ResourceBarrier(1, &barrier_begin_copy);
    m_cpCommList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, &box);
    m_cpCommList->ResourceBarrier(1, &barrier_end_copy);
    // END TMP

// -=====================================================================================================
    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
    geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometryDesc.Triangles.IndexBuffer = m_cpIB->GetGPUVirtualAddress();
    geometryDesc.Triangles.IndexCount = static_cast<UINT>(m_cpIB->GetDesc().Width) / sizeof(WORD/*Index Stride*/);
    geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
    geometryDesc.Triangles.Transform3x4 = 0;
    geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometryDesc.Triangles.VertexCount = static_cast<UINT>(m_cpVB->GetDesc().Width) / VSTRIDE;
    geometryDesc.Triangles.VertexBuffer.StartAddress = m_cpVB->GetGPUVirtualAddress();
    geometryDesc.Triangles.VertexBuffer.StrideInBytes = VSTRIDE;

    // Mark the geometry as opaque. 
    // PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
    // Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
    geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    // Get required sizes for an acceleration structure.
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
    topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    topLevelInputs.Flags = buildFlags;
    topLevelInputs.NumDescs = CUBES_NUM * CUBES_ROWS;
    topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
    m_cpD3DDev->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
    ASSERT(topLevelPrebuildInfo.ResultDataMaxSizeInBytes, "Invalid TLAS prebuild info max size");

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs = topLevelInputs;
    bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottomLevelInputs.pGeometryDescs = &geometryDesc;
    bottomLevelInputs.NumDescs = 1;
    m_cpD3DDev->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
    ASSERT(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, "Invalid BLAS prebuild info max size");

    auto AllocateUAVBuffer = [](ID3D12Device *pDevice, UINT64 bufferSize, ID3D12Resource **ppResource, 
        D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_COMMON, const wchar_t *resourceName = nullptr)
    {
        auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        DXASSERT(pDevice->CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            initialResourceState,
            nullptr,
            IID_PPV_ARGS(ppResource)), "Can't create UAV buffer");
        if (resourceName)
        {
            (*ppResource)->SetName(resourceName);
        }
    };

    AllocateUAVBuffer(m_cpD3DDev.Get(), max(topLevelPrebuildInfo.ScratchDataSizeInBytes,
        bottomLevelPrebuildInfo.ScratchDataSizeInBytes), &scratchResource, 
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

    // Allocate resources for acceleration structures.
    // Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
    // Default heap is OK since the application doesn’t need CPU read/write access to them. 
    // The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
    // and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
    //  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
    //  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
    {
        D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

        AllocateUAVBuffer(m_cpD3DDev.Get(), bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_bottomLevelAccelerationStructure, initialResourceState, L"BottomLevelAccelerationStructure");
        AllocateUAVBuffer(m_cpD3DDev.Get(), topLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_topLevelAccelerationStructure, initialResourceState, L"TopLevelAccelerationStructure");
    }

    // Create an instance desc for the bottom-level acceleration structure.
    D3D12_RAYTRACING_INSTANCE_DESC instanceDescs[CUBES_NUM * CUBES_ROWS] = {};
    const XMFLOAT3 center_pos = { 0, 0, 0 };
    for (int c = 0; c < CUBES_ROWS; c++)
    {
        for (int b = 0; b < CUBES_NUM; b++)
        {
            int i = b + c * CUBES_NUM;
            float start_angle = c * 0.05f;

            XMFLOAT3 cube_pos;
            const float angle = PI2 * i / float(CUBES_NUM) + start_angle;
            cube_pos.x = CUBES_RADIUS * cos(angle);
            cube_pos.y = c * CUBES_ROW_HEIGHT;
            cube_pos.z = CUBES_RADIUS * sin(angle);

            XMFLOAT2 dir = { center_pos.x - cube_pos.x, center_pos.z - cube_pos.z };
            float l = sqrt(dir.x * dir.x + dir.y * dir.y);
            dir.x /= l;
            dir.y /= l;

            instanceDescs[i].Transform[0][0] = dir.x;
            instanceDescs[i].Transform[0][2] = -dir.y;

            instanceDescs[i].Transform[1][1] = 1.f;

            instanceDescs[i].Transform[2][0] = dir.y;
            instanceDescs[i].Transform[2][2] = dir.x;

            instanceDescs[i].Transform[0][3] = cube_pos.x;
            instanceDescs[i].Transform[1][3] = cube_pos.y;
            instanceDescs[i].Transform[2][3] = cube_pos.z;
            instanceDescs[i].InstanceMask = 1;
            instanceDescs[i].AccelerationStructure = m_bottomLevelAccelerationStructure->GetGPUVirtualAddress();
        }
    }
    
    auto AllocateUploadBuffer = [](ID3D12Device *pDevice, void *pData, UINT64 datasize, ID3D12Resource **ppResource, const wchar_t *resourceName = nullptr)
    {
        auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(datasize);
        DXASSERT(pDevice->CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(ppResource)), "Can't create upload buffer");
        if (resourceName)
        {
            (*ppResource)->SetName(resourceName);
        }
        void *pMappedData;
        (*ppResource)->Map(0, nullptr, &pMappedData);
        memcpy(pMappedData, pData, datasize);
        (*ppResource)->Unmap(0, nullptr);
    };
    AllocateUploadBuffer(m_cpD3DDev.Get(), &instanceDescs[0], sizeof(instanceDescs), &instanceDescsBuff, L"InstanceDescs");

    // Bottom Level Acceleration Structure desc
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
    {
        bottomLevelBuildDesc.Inputs = bottomLevelInputs;
        bottomLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();
        bottomLevelBuildDesc.DestAccelerationStructureData = m_bottomLevelAccelerationStructure->GetGPUVirtualAddress();
    }

    // Top Level Acceleration Structure desc
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
    {
        topLevelInputs.InstanceDescs = instanceDescsBuff->GetGPUVirtualAddress();
        topLevelBuildDesc.Inputs = topLevelInputs;
        topLevelBuildDesc.DestAccelerationStructureData = m_topLevelAccelerationStructure->GetGPUVirtualAddress();
        topLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();
    }

    auto BuildAccelerationStructure = [&](auto *raytracingCommandList)
    {
        CD3DX12_RESOURCE_BARRIER geometry_uploads_begin[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_cpIB.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_GENERIC_READ),
            CD3DX12_RESOURCE_BARRIER::Transition(m_cpVB.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_GENERIC_READ)
        };

        CD3DX12_RESOURCE_BARRIER geometry_uploads_end[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_cpIB.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(m_cpVB.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        };

        m_cpCommList->ResourceBarrier(2, geometry_uploads_begin);
        raytracingCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
        auto BLAS_barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_bottomLevelAccelerationStructure.Get());
        m_cpCommList->ResourceBarrier(1, &BLAS_barrier);
        raytracingCommandList->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);
        m_cpCommList->ResourceBarrier(2, geometry_uploads_end);
    };

    // Build acceleration structure.
    BuildAccelerationStructure(m_cpCommList.Get());

// -=====================================================================================================

    // Create State Onbject
        // Create 7 subobjects that combine into a RTPSO:
    // Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
    // Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
    // This simple sample utilizes default shader association except for local root signature subobject
    // which has an explicit association specified purely for demonstration purposes.
    // 1 - DXIL library
    // 1 - Triangle hit group
    // 1 - Shader config
    // 2 - Local root signature and association
    // 1 - Global root signature
    // 1 - Pipeline config
    CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

    // DXIL library
    // This contains the shaders and their entrypoints for the state object.
    // Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
    auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void *)g_pRaytracingSample, ARRAYSIZE(g_pRaytracingSample));
    lib->SetDXILLibrary(&libdxil);
    // Define which shader exports to surface from the library.
    // If no shader exports are defined for a DXIL library subobject, all shaders will be surfaced.
    // In this sample, this could be omitted for convenience since the sample uses all shaders in the library. 
    {
        lib->DefineExport(c_raygenShaderName);
        lib->DefineExport(c_closestHitShaderName);
        lib->DefineExport(c_missShaderName);
    }

    // Triangle hit group
    // A hit group specifies closest hit, any hit and intersection shaders to be executed when a ray intersects the geometry's triangle/AABB.
    // In this sample, we only use triangle geometry with a closest hit shader, so others are not set.
    auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitGroup->SetClosestHitShaderImport(c_closestHitShaderName);
    hitGroup->SetHitGroupExport(c_hitGroupName);
    hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    // Shader config
    // Defines the maximum sizes in bytes for the ray payload and attribute structure.
    auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payloadSize = 4 * sizeof(float);   // float4 color
    UINT attributeSize = 2 * sizeof(float); // float2 barycentrics
    shaderConfig->Config(payloadSize, attributeSize);

    // Local root signature to be used in a ray gen shader.
    {
        auto localRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
        // Shader association
        auto rootSignatureAssociation = raytracingPipeline.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
        rootSignatureAssociation->AddExport(c_raygenShaderName);
    }

    // This is a root signature that enables a shader to have unique arguments that come from shader tables.

    // Global root signature
    // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
    auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    globalRootSignature->SetRootSignature(m_cpRootSignature[2].Get());

    auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    UINT maxRecursionDepth = 1; // ~ primary rays only. 
    pipelineConfig->Config(maxRecursionDepth);

    // Create the state object.
    DXASSERT(m_cpD3DDev->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_cpStateObject)), L"Couldn't create DirectX Raytracing state object.\n");

    BuildShaderTables();
}

void RaytracingSample::createRTRootSignatures()
{
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;

    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(m_cpD3DDev->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

    // Global Root Signature
    // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
    {
        CD3DX12_DESCRIPTOR_RANGE1 UAVDescriptor;
        UAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

        CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1); // srv's: texture, vb & ib

        CD3DX12_ROOT_PARAMETER1 rootParameters[4];
        rootParameters[0].InitAsDescriptorTable(1, &UAVDescriptor);
        rootParameters[1].InitAsShaderResourceView(0);
        rootParameters[2].InitAsConstants(24, 0, 0, D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[3].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL); // srv : stone texture

        D3D12_STATIC_SAMPLER_DESC sampler[1] = {};
        sampler[0].Filter = D3D12_FILTER_ANISOTROPIC;//D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler[0].MipLODBias = 0;
        sampler[0].MaxAnisotropy = 16;
        sampler[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler[0].MinLOD = 0.0f;
        sampler[0].MaxLOD = D3D12_FLOAT32_MAX;
        sampler[0].ShaderRegister = 0;
        sampler[0].RegisterSpace = 0;
        sampler[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC globalRootSignatureDesc;
        globalRootSignatureDesc.Init_1_1(ARRAYSIZE(rootParameters), rootParameters, 1, sampler);

        if (FAILED(D3D12SerializeVersionedRootSignature(&globalRootSignatureDesc, &blob, &error)))
        {
            MessageBox(0, static_cast<char *>(error->GetBufferPointer()), 
                "Error: Can't serialize RT global root signature", MB_OK | MB_ICONERROR);
        }
        DXASSERT(m_cpD3DDev->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_cpRootSignature[2])),
            "Can't greate RT root signature");
    }
}

void RaytracingSample::BuildShaderTables()
{
    void *rayGenShaderIdentifier;
    void *missShaderIdentifier;
    void *hitGroupShaderIdentifier;

    auto GetShaderIdentifiers = [&](auto *stateObjectProperties)
    {
        rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName);
        missShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName);
        hitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_hitGroupName);
    };

    // Get shader identifiers.
    UINT shaderIdentifierSize;
    {
        ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
        DXASSERT(m_cpStateObject.As(&stateObjectProperties), "");
        GetShaderIdentifiers(stateObjectProperties.Get());
        shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    }
    
    auto create_uploaded_resource = [device = m_cpD3DDev.Get()](UINT numRecords, UINT bufferSize, const wchar_t *resourceName) -> ID3D12Resource *
    {
        auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        ID3D12Resource *resource = nullptr;
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize * numRecords);
        DXASSERT(device->CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&resource)), "Can't create commited resource");
        resource->SetName(resourceName);
        return resource;
    };

    // Ray gen shader table
    {
        UINT numShaderRecords = 1;
        UINT shaderRecordSize = shaderIdentifierSize;
        m_rayGenShaderTable = create_uploaded_resource(numShaderRecords, shaderRecordSize, L"RayGenShaderTable");

        char *ptr = nullptr;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        DXASSERT(m_rayGenShaderTable->Map(0, &readRange, reinterpret_cast<void **>(&ptr)), "Failed to map upload resource");
        memcpy(ptr, rayGenShaderIdentifier, shaderIdentifierSize);
    }

    // Miss shader table
    {
        UINT numShaderRecords = 1;
        UINT shaderRecordSize = shaderIdentifierSize;
        m_missShaderTable = create_uploaded_resource(numShaderRecords, shaderRecordSize, L"MissShaderTable");

        char *ptr = nullptr;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        DXASSERT(m_missShaderTable->Map(0, &readRange, reinterpret_cast<void **>(&ptr)), "Failed to map upload resource");
        memcpy(ptr, missShaderIdentifier, shaderIdentifierSize);
    }

    // Hit group shader table
    {
        UINT numShaderRecords = 1;
        UINT shaderRecordSize = shaderIdentifierSize;
        m_hitGroupShaderTable = create_uploaded_resource(numShaderRecords, shaderRecordSize, L"HitGroupShaderTable");

        char *ptr = nullptr;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        DXASSERT(m_hitGroupShaderTable->Map(0, &readRange, reinterpret_cast<void **>(&ptr)), "Failed to map upload resource");
        memcpy(ptr, hitGroupShaderIdentifier, shaderIdentifierSize);
    }
}

// Update camera matrices passed into the shader.
void RaytracingSample::UpdateCameraMatrices()
{
    auto frameIndex = m_frameIndex % m_framesCount;
    XMMATRIX viewProj = m_spCamera->getViewProjMatrix();
    m_RTC[frameIndex].projectionToWorld = XMMatrixInverse(nullptr, m_spCamera->getViewProjMatrix());
    m_RTC[frameIndex].cameraPosition = m_spCamera->getPosition();
    m_RTC[frameIndex].viewDirection = m_spCamera->getDirectionVector();
}