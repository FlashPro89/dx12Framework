#include "TiledResourcesSample.h"
#include "DX12MeshGenerator.h"
#include "DDSTextureLoader12.h"
#include <stdlib.h>
#include <string>

XMMATRIX mWVP;

// ------------------------------------
//
//		*** class TiledResourceSample ***
//
// ------------------------------------

TiledResourcesSample::TiledResourcesSample(std::string name, 
	DX12Framework::DX12FRAMEBUFFERING buffering, bool useWARP) :
	DX12Framework( name, buffering, useWARP )
{

}

TiledResourcesSample::~TiledResourcesSample()
{ 
}

void TiledResourcesSample::createSRVTex2D(ID3D12Resource* pResourse, UINT heapOffsetInDescriptors)
{
    assert(pResourse != 0 && "null ID3D12Resource ptr!");

    // Describe and create a SRV for the texture.
    D3D12_RESOURCE_DESC desc = pResourse->GetDesc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;

    CD3DX12_CPU_DESCRIPTOR_HANDLE h = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_cpSRVHeap->GetCPUDescriptorHandleForHeapStart(),
        heapOffsetInDescriptors, m_srvDescriptorSize);

    m_cpD3DDev->CreateShaderResourceView(pResourse, &srvDesc, h);
}

bool TiledResourcesSample::initialize()
{
	if (!initDefault())
		return false;
	if (!initInput())
		return false;
    m_spCamera = std::make_shared<gCamera>(m_spInput.get());
    m_spCamera->setPosition(XMFLOAT3(0, 0, -10.f));
    m_spCamera->lookAt(XMFLOAT3(0, 0, 0));
    m_spCamera->setMovementSpeed(5.f);
        
    if (!createRootSignatureAndPSO())
        return false;

    if (!m_spRingBuffer->initialize(0x7FFFFF))
        return false;

    DX12MeshGenerator<0, 12, 24, 32> meshGen(m_spRingBuffer);
    DX12MeshGeneratorOffets offs;
    bool r = meshGen.buildIndexedCube(offs, 0.4f, 0.4f, 0.4f);

    
    struct vert
    {
        float x, y, z;
        float nx, ny, nz;
        float tx, ty, tz;
        float tu, tv;
    };

    vert* _v = reinterpret_cast<vert*>(offs.vdata);
    WORD* _i = reinterpret_cast<WORD*>(offs.idata);
    FILE* f = 0;

    auto err = fopen_s(&f, "out_box.txt", "wt");
    for (int i = 0; i < 24; i++)
    {
        fprintf(f, "v%i: xyz: %f %f %f norm: %f %f %f tan: %f %f %f tc: %f %f\n",
            i, _v->x, _v->y, _v->z, _v->nx, _v->ny, _v->nz,
            _v->tx, _v->ty, _v->tz, _v->tu, _v->tv);
        _v++;
    }

    for (int i = 0, c=0; i < 36; i += 3,c++)
    {
        fprintf(f, "t%i: %i %i %i\n",
            c,_i[0], _i[1], _i[2] );
        _i += 3;
    }

    fclose(f);

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

    D3D12_SUBRESOURCE_DATA srData = {};
    srData.pData = offs.vdata;
    srData.RowPitch = static_cast<LONG_PTR>(offs.vDataSize);
    srData.SlicePitch = offs.vDataSize;

    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_cpVB.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        UpdateSubresources(m_cpCommList.Get(), m_cpVB.Get(), m_spRingBuffer->getResource(),
            offs.vDataOffset, 0, 1, &srData);
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

    srData = {};
    srData.pData = offs.idata;
    srData.RowPitch = static_cast<LONG_PTR>(offs.iDataSize);
    srData.SlicePitch = offs.iDataSize;

    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_cpIB.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        UpdateSubresources(m_cpCommList.Get(), m_cpIB.Get(), m_spRingBuffer->getResource(),
            offs.iDataOffset, 0, 1, &srData);
        m_cpCommList->ResourceBarrier(1, &barrier);
    }
    //----------------------------------------------

    m_vb.BufferLocation = m_cpVB->GetGPUVirtualAddress();
    m_vb.SizeInBytes = offs.vDataSize;
    m_vb.StrideInBytes = 44; // 44bytes stride

    m_ib.BufferLocation = m_cpIB->GetGPUVirtualAddress();
    m_ib.SizeInBytes = offs.iDataSize;
    m_ib.Format = DXGI_FORMAT_R16_UINT; 

    //Test load dds:
    std::unique_ptr<uint8_t[]> ddsData;
    std::vector<D3D12_SUBRESOURCE_DATA> subResDataVector;
    ID3D12Resource* pResource;

    HRESULT tlResult = LoadDDSTextureFromFile(m_cpD3DDev.Get(), L"island_v1_tex.dds",
        &pResource, ddsData, subResDataVector);

    uploadSubresources(pResource, static_cast<UINT>(subResDataVector.size()), &subResDataVector[0]);
    createSRVTex2D(pResource, 0);

    m_cpTexture = pResource;
    

    m_spWindow->showWindow(true);
    endCommandList();
    executeCommandList();
    WaitForGpu();

	return true;
}


bool TiledResourcesSample::beginCommandList()
{
    if (FAILED(m_cpCommAllocator->Reset()))
        return false;

    if (FAILED(m_cpCommList->Reset(m_cpCommAllocator.Get(), m_cpPipelineState[1].Get())))
        return false;

    return true;
}

bool TiledResourcesSample::populateCommandList()
{
    // Set necessary state.
    m_cpCommList->SetGraphicsRootSignature(m_cpRootSignature[1].Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_cpSRVHeap.Get() };
    m_cpCommList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    CD3DX12_GPU_DESCRIPTOR_HANDLE h(m_cpSRVHeap->GetGPUDescriptorHandleForHeapStart());

    m_cpCommList->RSSetViewports(1, &m_viewport);
    m_cpCommList->RSSetScissorRects(1, &m_scissorRect);

    // Indicate that the back buffer will be used as a render target.
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_cpRenderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_cpCommList->ResourceBarrier(1, &barrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_cpRTVHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_cpDSVHeap->GetCPUDescriptorHandleForHeapStart());

    //With DS
    m_cpCommList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.4f, 0.2f, 1.0f };
    m_cpCommList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_cpCommList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, 0);

    m_cpCommList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cpCommList->IASetVertexBuffers(0, 1, &m_vb);
    m_cpCommList->IASetIndexBuffer(&m_ib);

    XMFLOAT4X4 mvp;
    XMStoreFloat4x4(&mvp, mWVP);

    m_cpCommList->SetGraphicsRootDescriptorTable(1, h);
    m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &mvp, 0);

    //int face = 2;
    //m_cpCommList->DrawIndexedInstanced(6, 1, face*6, 0, 0);
    m_cpCommList->DrawIndexedInstanced(36, 1, 0, 0, 0);

    // Indicate that the back buffer will now be used to present.
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_cpRenderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_cpCommList->ResourceBarrier(1, &barrier);

    return true;
}

bool TiledResourcesSample::endCommandList()
{
    if (FAILED(m_cpCommList->Close()))
        return false;
    return true;
}

void TiledResourcesSample::executeCommandList()
{
    ID3D12CommandList* ppCommandLists[] = { m_cpCommList.Get() };
    m_cpCommQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}


bool TiledResourcesSample::executeCommandListAndPresent()
{
    
    executeCommandList();

    if (FAILED(m_cpSwapChain->Present(1, 0)))
        return false;

    return true;
}

bool TiledResourcesSample::update()
{
    constexpr float speed = 0.5f;
    float dt = m_spTimer->getDelta();

    if (m_spInput)
        m_spInput->update();

    if (m_spCamera)
        m_spCamera->tick(dt);

    mWVP = m_spCamera->getViewProjMatrix();
    mWVP = XMMatrixTranspose(mWVP);

    XMFLOAT3 dir;
    XMStoreFloat3( &dir, m_spCamera->getOrientation());

    DX12WINDOWPARAMS wParams = m_spWindow->getWindowParameters();
    wParams.name = "Dir:" + std::to_string(dir.x) + " " 
                          + std::to_string(dir.y) + " "
                          + std::to_string(dir.z);
    //wParams.x = 100; wParams.y = 100;
    //m_spWindow->setWindowParameters(wParams);


    /*
    XMFLOAT3 eye(3.f, 3.f, -3.f), dir(-1.f, -1.f, 1.f), up(0, 1.f, 0);
    XMMATRIX mView = XMMatrixLookToLH(XMLoadFloat3(&eye), XMVector3Normalize( XMLoadFloat3(&dir)), XMLoadFloat3(&up));
    XMMATRIX mProj = XMMatrixPerspectiveFovLH(3.1415f / 4, 1.333f, 1.f, 1500.0f);
    mWVP = mView * mProj;
    mWVP = XMMatrixTranspose(mWVP);
    */
	return true;
}

bool TiledResourcesSample::render()
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

bool TiledResourcesSample::createRootSignatureAndPSO()
{

// ROOT SIGNATURE:

    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(m_cpD3DDev->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

    CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

    CD3DX12_ROOT_PARAMETER1 rootParameters[2];
    rootParameters[0].InitAsConstants(16, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    rootParameters[1].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC sampler[1] = {};
    sampler[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[0].MipLODBias = 0;
    sampler[0].MaxAnisotropy = 0;
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

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    const char vs_source[] =
        "struct VS_INPUT                    "
        "{                                   "
        "    float4 pos : POSITION;          "
        "    float4 norm : NORMAL;          "
        "    float4 tangent : TANGENT;      "
        "    float2 texCoord : TEXCOORD;     "
        "};                                 "

        "struct VS_OUTPUT                       "
        "{                                      "
        "    float4 pos : SV_POSITION;          "
        "    float2 texCoord : TEXCOORD;        "
        "}; "

        "struct sConstantBuffer                 "
        "{                                      "
        "    float4x4 wvpMat;                   "
        "};                                     "
        "ConstantBuffer<sConstantBuffer> myCBuffer : register(b0); "
        "VS_OUTPUT main(VS_INPUT input)         "
        "{                                      "
        "   VS_OUTPUT output; "
        "   float4 tPos = input.pos;"
        "   tPos.w = 1.0f;"
        "   output.pos = mul(tPos, myCBuffer.wvpMat); "
        "   output.texCoord = input.texCoord;  "
        "   return output;                     "
        "}";

    const char ps_source[] =
        "struct PSInput                         "
        "{                                      "
        "    float4 position : SV_POSITION;     "
        "    float2 uv : TEXCOORD;              "
        "};                                     "

        "Texture2D g_texture : register(t0);       "
        "SamplerState g_sampler : register(s0);     "

        "float4 main(PSInput input) : SV_TARGET     "
        "{                                          "
        "    return g_texture.Sample(g_sampler, input.uv); "
        "} ";


    ID3DBlob* errorBlob;

    if (FAILED(D3DCompile(vs_source, sizeof(vs_source), "vs_tiled", nullptr,
        nullptr, "main", "vs_5_1", compileFlags, 0, &vertexShader, &errorBlob)))
        return false;

    if (FAILED(D3DCompile(ps_source, sizeof(ps_source), "ps_tiled", nullptr,
        nullptr, "main", "ps_5_1", compileFlags, 0, &pixelShader, &errorBlob)))
        return false;

    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_cpRootSignature[1].Get();
    psoDesc.VS = { reinterpret_cast<UINT8*>(vertexShader->GetBufferPointer()), vertexShader->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<UINT8*>(pixelShader->GetBufferPointer()), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
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
