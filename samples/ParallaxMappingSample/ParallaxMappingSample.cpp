#include "ParallaxMappingSample.h"
#include <stdlib.h>
#include <string>
#include <codecvt>

// ------------------------------------
//
//		*** class ParallaxMappingSample ***
//
// ------------------------------------

ParallaxMappingSample::ParallaxMappingSample(std::string name,
    DX12Framework::DX12FRAMEBUFFERING buffering, bool useWARP) :
    DX12Framework(name, buffering, useWARP),
    m_lightRotRadius( 4.f ),
    m_lightRotAngle( 0.f ),
    m_lightRotAngle2( 0.f )
{
}

ParallaxMappingSample::~ParallaxMappingSample()
{ 
}

bool ParallaxMappingSample::initialize()
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

    if (!m_spRingBuffer->initialize(0x3FFFFFF)) // 64MBytes
        return false;

    DX12MeshGenerator<0, 12, 24, 36> meshGen(m_spRingBuffer);
    DX12MeshGeneratorOffets offs;
    bool r = meshGen.buildIndexedCube(offs, 0.4f, 0.4f, 0.4f,false);

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
    }
    //----------------------------------------------

    m_vb.BufferLocation = m_cpVB->GetGPUVirtualAddress();
    m_vb.SizeInBytes = offs.vDataSize;
    m_vb.StrideInBytes = 44; // 44bytes stride

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

    {   // Normal Map with height in alpha channel
        std::unique_ptr<uint8_t[]> ddsData;
        std::vector<D3D12_SUBRESOURCE_DATA> subResDataVector;
        ID3D12Resource* pResource;

        HRESULT tlResult = LoadDDSTextureFromFile(m_cpD3DDev.Get(), "../textures/stones_NM_height.dds",
            &pResource, ddsData, subResDataVector);
        if (FAILED(tlResult))
        {
            MessageBox(0, "Cannot load normal map!", "Error", MB_OK | MB_ICONERROR);
            return false;
        }

        uploadSubresources(pResource, static_cast<UINT>(subResDataVector.size()), &subResDataVector[0]);
        createSRVTex2D(pResource, 1);
        m_cpNormalMap = pResource;
    }

    endCommandList();
    executeCommandList();
    WaitForGpu();

    m_spWindow->showWindow(true);

	return true;
}


bool ParallaxMappingSample::beginCommandList()
{
    if (FAILED(m_cpCommAllocator->Reset()))
        return false;

    if (FAILED(m_cpCommList->Reset(m_cpCommAllocator.Get(), m_cpPipelineState[1].Get())))
        return false;

    return true;
}

bool ParallaxMappingSample::populateCommandList()
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
    XMFLOAT4X4 fmW;
    XMMATRIX mWVP, mVP, mTranslation;
    mVP = m_spCamera->getViewProjMatrix();

    // ----------------------------------------------------
    // draw cubes:
    mTranslation = XMMatrixTranslation(0.f, 0.f, 0.f);
    mWVP = mTranslation * mVP;
    XMStoreFloat4x4( &fmWVP, XMMatrixTranspose(mWVP) );
    XMStoreFloat4x4( &fmW, XMMatrixTranspose(mTranslation) );

    // setup light dir
    XMMATRIX mLightDirRotation = XMMatrixRotationRollPitchYaw(m_lightRotAngle, m_lightRotAngle / 2.f, 0.f);
    XMFLOAT3 fLightDirDefault = XMFLOAT3(0.f, 0.f, 1.f);
    XMVECTOR vLightDirDefault = XMLoadFloat3(&fLightDirDefault);
    XMVECTOR vLightDir = XMVector3Transform(vLightDirDefault, mLightDirRotation);
    XMFLOAT3 fLightDir;
    XMStoreFloat3(&fLightDir, vLightDir);

    m_cpCommList->SetGraphicsRoot32BitConstants( 0, 4, &fLightDir, 32 );

    // set view dir
    XMFLOAT3 viewDir;
    XMStoreFloat3( &viewDir, m_spCamera->getDirectionVector());
    m_cpCommList->SetGraphicsRoot32BitConstants( 0, 3, &viewDir, 36 );

    // draw light
    {
        constexpr float light_scale = 0.2f;
        XMMATRIX mSc = XMMatrixScaling(light_scale, light_scale, light_scale);
        mTranslation = XMMatrixTranslation(-m_lightRotRadius * fLightDir.x, -m_lightRotRadius * fLightDir.y, -m_lightRotRadius * fLightDir.z);
        mWVP = mSc * mTranslation * mVP;
        XMStoreFloat4x4(&fmWVP, XMMatrixTranspose(mWVP));
        XMStoreFloat4x4(&fmW, XMMatrixTranspose(mTranslation));

        m_cpCommList->SetGraphicsRootDescriptorTable(3, h);
        m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmWVP, 0);
        m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmW, 16);
        m_cpCommList->DrawIndexedInstanced(36, 1, 0, 0, 0);
    }

    // draw cubes
    const float shoulder = 1.f;
    for (float x = -shoulder; x <= shoulder; x += 1.f)
    {
        for (float y = -shoulder; y <= shoulder; y += 1.f)
        {
            mTranslation = XMMatrixTranslation(x, y, 0.f);
            mWVP = mTranslation * mVP;
            XMStoreFloat4x4(&fmWVP, XMMatrixTranspose(mWVP));
            XMStoreFloat4x4(&fmW, XMMatrixTranspose(mTranslation));

            m_cpCommList->SetGraphicsRootDescriptorTable(3, h);
            m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmWVP, 0);
            m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmW, 16);
            m_cpCommList->DrawIndexedInstanced(36, 1, 0, 0, 0);
        }
    }

    // Indicate that the back buffer will now be used to present.
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_cpRenderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_cpCommList->ResourceBarrier(1, &barrier);

    return true;
}

bool ParallaxMappingSample::endCommandList()
{
    if (FAILED(m_cpCommList->Close()))
        return false;
    return true;
}

void ParallaxMappingSample::executeCommandList()
{
    ID3D12CommandList* ppCommandLists[] = { m_cpCommList.Get() };
    m_cpCommQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}


bool ParallaxMappingSample::executeCommandListAndPresent()
{
    
    executeCommandList();

    if (FAILED(m_cpSwapChain->Present(1, 0)))
        return false;

    return true;
}

bool ParallaxMappingSample::update()
{
    constexpr float speed = 0.5f;
    float dt = m_spTimer->getDelta();
   
    static bool rotate = true;
    if (m_spInput->isKeyDown(DIK_SPACE))
        rotate = !rotate;

    if (rotate)
        m_lightRotAngle += dt * 0.5f;
    m_lightRotAngle2 += dt * 0.05f;

    if (m_spInput)
        m_spInput->update();

    if (m_spCamera)
        m_spCamera->tick(dt);

	return true;
}

bool ParallaxMappingSample::render()
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

bool ParallaxMappingSample::createRootSignatureAndPSO()
{

// ROOT SIGNATURE:

    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(m_cpD3DDev->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

    CD3DX12_DESCRIPTOR_RANGE1 ranges[1]; // diffuse + nmap
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

    CD3DX12_ROOT_PARAMETER1 rootParameters[4];
    rootParameters[0].InitAsConstants(40, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX); // wvp + w + lightVec + eyeDir
    rootParameters[1].InitAsConstants(16, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX); // not use, delete it
    rootParameters[2].InitAsConstants(4, 2, 0, D3D12_SHADER_VISIBILITY_VERTEX); // not use, delete it
    rootParameters[3].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL); // srv descriptor

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

    std::wstring w_fileName = L"../shaders/ParallaxMappingSample.hlsl";

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
