#include "TiledResourceSample.h"
#include <stdlib.h>
#include <string>
#include <codecvt>

constexpr UINT reservedWidth = 512;
constexpr UINT reservedHeight = 512;
constexpr UINT bufferWidth = 2048;
constexpr UINT bufferHeight = 2048;
constexpr UINT RESERVER_RES_DHEAP_OFFSET = 7;

#define FILTER(x,y)(x&0x1 ^ y&0x1) != 0
#define HEAP_SIZE(numTiles)(numTiles / 2 + 1 ) * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT

// ------------------------------------
//
//		*** class TiledResourceSample ***
//
// ------------------------------------

TiledResourceSample::TiledResourceSample(std::string name,
    DX12Framework::DX12FRAMEBUFFERING buffering, bool useWARP) :
    DX12Framework(name, buffering, useWARP),
    m_cursourPosition(0, bufferHeight/2)
{
}

TiledResourceSample::~TiledResourceSample()
{ 
}

void TiledResourceSample::createSRVTex2D(ID3D12Resource* pResourse, UINT heapOffsetInDescriptors)
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

bool TiledResourceSample::createReservedResource()
{
    HRESULT hr = S_OK;
    //---------------------------------------
    // Test reserved resource
    //---------------------------------------
    ID3D12Resource* pReservedResource;
    auto reservedDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, reservedWidth, reservedHeight);
    reservedDesc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
    reservedDesc.MipLevels = 1;
    hr = m_cpD3DDev->CreateReservedResource(
        &reservedDesc,
        D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&pReservedResource)
    );
    if (FAILED(hr))
        return false;

    m_cpReservedResource = pReservedResource;
    createSRVTex2D(m_cpReservedResource.Get(), RESERVER_RES_DHEAP_OFFSET );

    // Test reserved vbuffer
    {
        ComPtr< ID3D12Resource> cpReservedBufferResource;
        ID3D12Resource* pReservedResource;
        auto reservedBuffDesc = CD3DX12_RESOURCE_DESC::Buffer( 44 * 0xFFFFF );
        //reservedBuffDesc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
        hr = m_cpD3DDev->CreateReservedResource(
            &reservedDesc,
            D3D12_RESOURCE_STATES::D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&pReservedResource)
        );
        if (FAILED(hr))
            return false;

        cpReservedBufferResource = pReservedResource;
    }
    
    //---------------------------------------
    // Get Resource Tiling 
    //---------------------------------------
    UINT numTiles = 0;
    D3D12_PACKED_MIP_INFO packedMipInfo;
    D3D12_TILE_SHAPE tileShape;
    UINT subresourceCount = reservedDesc.MipLevels == 0 ? 1 : reservedDesc.MipLevels;
    std::vector<D3D12_SUBRESOURCE_TILING> tilings(subresourceCount);
    m_cpD3DDev->GetResourceTiling(m_cpReservedResource.Get(), &numTiles, &packedMipInfo,
        &tileShape, &subresourceCount, 0, &tilings[0]);

    //---------------------------------------
    //  Prepare to update tile map coords & ranges   
    //---------------------------------------
    D3D12_TILED_RESOURCE_COORDINATE startCoordinates[1024];
    D3D12_TILE_REGION_SIZE regionSize[1024];
    D3D12_TILE_RANGE_FLAGS rangeFlags[1024]; // For first subres: FLAG_NONE
    UINT heapRangeOffset[1024];
    UINT rangeTileCount[1024];
    UINT tilesUsed = 0;
    UINT lastTile = tilings[0].WidthInTiles * tilings[0].HeightInTiles / 2;

    UINT heapOffset = 0;

    for( UINT y = 0; y < tilings[0].HeightInTiles; y++ )
    { 
        for (UINT x = 0; x < tilings[0].WidthInTiles; x++ )
        {
            startCoordinates[tilesUsed].X = x;
            startCoordinates[tilesUsed].Y = y;
            startCoordinates[tilesUsed].Z = 0;
            startCoordinates[tilesUsed].Subresource = 0;

            regionSize[tilesUsed].Height = 1;
            regionSize[tilesUsed].Width = 1;
            regionSize[tilesUsed].Depth = 1;
            regionSize[tilesUsed].NumTiles = 1;
            regionSize[tilesUsed].UseBox = true; // no packed mips yet

            rangeTileCount[tilesUsed] = 1;
            rangeFlags[tilesUsed] = D3D12_TILE_RANGE_FLAG_NONE;

            if ( FILTER(x,y) )
            {
                heapRangeOffset[tilesUsed] = lastTile;
            }
            else
            {
                heapRangeOffset[tilesUsed] = heapOffset++;
            }
            tilesUsed++;
        }
    }

    //---------------------------------------
    //  Create heap
    //---------------------------------------
    const UINT heapSize = (tilesUsed + 1) * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    //const UINT heapSize = (numTiles+1)  * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    CD3DX12_HEAP_DESC heapDesc(heapSize, D3D12_HEAP_TYPE_DEFAULT, 0,
        D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES);
    hr = m_cpD3DDev->CreateHeap(&heapDesc, IID_PPV_ARGS(&m_cpReservedHeap));
    if (FAILED(hr))
        return false;
    
    //---------------------------------------
    //  Update Tile Mapping   
    //---------------------------------------
    m_cpCommQueue->UpdateTileMappings(
        m_cpReservedResource.Get(),
        tilesUsed,//num regions to update
        &startCoordinates[0],
        &regionSize[0],
        m_cpReservedHeap.Get(),
        tilesUsed,//num ranges
        &rangeFlags[0],
        &heapRangeOffset[0],
        &rangeTileCount[0],
        D3D12_TILE_MAPPING_FLAG_NONE
    );


    //---------------------------------------
    //  Create and fill texture buffer
    //---------------------------------------
    m_textureBuffer = std::unique_ptr<unsigned char[]>
                { new unsigned char[static_cast<size_t>(bufferWidth * bufferHeight*4)] };

    unsigned char* bufferCur = reinterpret_cast<unsigned char*>(m_textureBuffer.get());

    float val = 0;
    unsigned char cval = 0;
    
    for (UINT64 y = 0; y < bufferHeight; y++)
    {
        for (UINT64 x = 0; x < bufferWidth; x++)
        {
            val = static_cast<float>(x * y)
                / static_cast<float>(bufferWidth * bufferHeight);
            cval = static_cast<unsigned char>((val) * 255 + 0);
            cval >>= 4;
            cval <<= 4;
            bufferCur[0] = cval;
            bufferCur[1] = cval;
            bufferCur[2] = cval;
            bufferCur[3] = cval;
            bufferCur += 4;
        }
    }

    //---------------------------------------
    //  Change state to PIXEL_SHADER_RESOURCE  
    //---------------------------------------
    CD3DX12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(m_cpReservedResource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_cpCommList->ResourceBarrier(1, &barrier);

    //---------------------------------------
    //  Upload data
    //---------------------------------------
    fillReservedTextureFromBuffer();


    return true;
}

void TiledResourceSample::fillReservedTextureFromBuffer()
{
    //---------------------------------------
    //  Change state to RESOURCE_STATE_COPY_DEST  
    //---------------------------------------
    CD3DX12_RESOURCE_BARRIER barrier =

        CD3DX12_RESOURCE_BARRIER::Transition(m_cpReservedResource.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST );
    m_cpCommList->ResourceBarrier(1, &barrier);

    //---------------------------------------
    //  Get Resource Tiling 
    //---------------------------------------
    D3D12_RESOURCE_DESC reservedDesc = m_cpReservedResource->GetDesc();
    UINT numTiles = 0;
    D3D12_PACKED_MIP_INFO packedMipInfo;
    D3D12_TILE_SHAPE tileShape;
    UINT subresourceCount = reservedDesc.MipLevels == 0 ? 1 : reservedDesc.MipLevels;
    std::vector<D3D12_SUBRESOURCE_TILING> tilings(subresourceCount);
    m_cpD3DDev->GetResourceTiling(m_cpReservedResource.Get(), &numTiles, &packedMipInfo,
        &tileShape, &subresourceCount, 0, &tilings[0]);
    
    UINT tileSizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; // tile size in bytes

    for (UINT ty = 0; ty < tilings[0].HeightInTiles; ty++)
    {
        for (UINT tx = 0; tx < tilings[0].WidthInTiles; tx++)
        {
            if (FILTER(tx, ty))
                continue;
            
            //---------------------------------------
            // Allocate memory in ring buffer
            //---------------------------------------
            ADDRESS uploadOffset;
            void* upload = m_spRingBuffer->allocate( m_frameIndex, tileSizeInBytes, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, &uploadOffset );

            //---------------------------------------
            // Upload data to reserved texture
            //---------------------------------------
            auto buffer = reinterpret_cast<unsigned char*>(m_textureBuffer.get());
            auto numTilesInSubresource = tilings[0].WidthInTiles * tilings[0].HeightInTiles;
            UINT tileRow = tileShape.WidthInTexels;
            UINT tileCol = tileShape.HeightInTexels;

            UINT tileId = tx + ty * tileRow;
            auto tileOffset = tilings[0].StartTileIndexInOverallResource + tileId;
            unsigned char* uploadCur = reinterpret_cast<unsigned char*>(upload);

            UINT uy = static_cast<UINT>(m_cursourPosition.y) + tileCol * ty;

            UINT px;
            UINT py;
            size_t pos;
            for (UINT y = uy; y < uy + tileRow; y++)
            {
                px = static_cast<UINT> (m_cursourPosition.x) + tileRow * tx;
                py = (y)*bufferWidth;
                pos = (px + py) * 4;

                memcpy( uploadCur, &buffer[pos], tileRow * 4 );
                uploadCur += tileRow * 4;
            }
            copyTile(tx, ty, 0, 0, uploadOffset);
        }
    }

    //---------------------------------------
    //  Change state to PIXEL_SHADER_RESOURCE  
    //---------------------------------------
    barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(m_cpReservedResource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_cpCommList->ResourceBarrier(1, &barrier);
}

void TiledResourceSample::copyTile( UINT tx, UINT ty, UINT tz, UINT subresource, ADDRESS uploadOffset )
{
    //---------------------------------------
    //  Get Resource Tiling 
    //---------------------------------------
    D3D12_RESOURCE_DESC reservedDesc = m_cpReservedResource->GetDesc();
    UINT numTiles = 0;
    D3D12_PACKED_MIP_INFO packedMipInfo;
    D3D12_TILE_SHAPE tileShape;
    UINT subresourceCount = reservedDesc.MipLevels == 0 ? 1 : reservedDesc.MipLevels;
    std::vector<D3D12_SUBRESOURCE_TILING> tilings(subresourceCount);
    m_cpD3DDev->GetResourceTiling(m_cpReservedResource.Get(), &numTiles, &packedMipInfo,
        &tileShape, &subresourceCount, 0, &tilings[0]);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    auto desc = m_cpReservedResource->GetDesc();
    UINT numRows;
    UINT64 rowSizeInBytes;
    UINT64 totalBytes;
    m_cpD3DDev->GetCopyableFootprints(&desc, subresource, subresource+1, uploadOffset,
        &footprint, &numRows, &rowSizeInBytes, &totalBytes);

    //---------------------------------------
    //  Copy tile 
    //---------------------------------------   
    D3D12_BOX box;

    box.left = 0;
    box.right = tileShape.WidthInTexels;

    box.top = 0;
    box.bottom = tileShape.HeightInTexels;

    box.front = tz;
    box.back = ( tz+1)* tileShape.DepthInTexels;

    footprint.Footprint.Width = tileShape.WidthInTexels;
    footprint.Footprint.Height = tileShape.HeightInTexels;
    footprint.Footprint.Depth = tileShape.DepthInTexels;
    footprint.Footprint.RowPitch = tileShape.WidthInTexels * 4;

    CD3DX12_TEXTURE_COPY_LOCATION Dst( m_cpReservedResource.Get(), subresource );
    CD3DX12_TEXTURE_COPY_LOCATION Src( m_spRingBuffer->getResource(), footprint );
    m_cpCommList->CopyTextureRegion( &Dst, tx * tileShape.WidthInTexels, 
        ty * tileShape.HeightInTexels, 0, &Src, &box );

    
}

bool TiledResourceSample::initialize()
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
    m_spCamera->setPosition(XMFLOAT3(0, 0, -1.5f));
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


    // create reserved resource
    if (!createReservedResource())
        return false;

    endCommandList();
    executeCommandList();
    WaitForGpu();

    m_spWindow->showWindow(true);

	return true;
}


bool TiledResourceSample::beginCommandList()
{
    if (FAILED(m_cpCommAllocator->Reset()))
        return false;

    if (FAILED(m_cpCommList->Reset(m_cpCommAllocator.Get(), m_cpPipelineState[1].Get())))
        return false;

    return true;
}

bool TiledResourceSample::populateCommandList()
{
    // Set necessary state.
    m_cpCommList->SetGraphicsRootSignature(m_cpRootSignature[1].Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_cpSRVHeap.Get() };
    m_cpCommList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    CD3DX12_GPU_DESCRIPTOR_HANDLE h(m_cpSRVHeap->GetGPUDescriptorHandleForHeapStart(), 
        RESERVER_RES_DHEAP_OFFSET, m_srvDescriptorSize);

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

    fillReservedTextureFromBuffer();

    XMFLOAT4X4 fmWVP;
    XMMATRIX mWVP, mVP, mTranslation;
    mVP = m_spCamera->getViewProjMatrix();

    // ----------------------------------------------------
    // draw cube:
    mTranslation = XMMatrixTranslation(0.f, 0.f, 0.f);
    mWVP = mTranslation * mVP;
    XMStoreFloat4x4( &fmWVP, XMMatrixTranspose(mWVP) );

    m_cpCommList->SetGraphicsRootDescriptorTable( 1, h );
    m_cpCommList->SetGraphicsRoot32BitConstants( 0, 16, &fmWVP, 0 );
    m_cpCommList->DrawIndexedInstanced( 36, 1, 0, 0, 0 );

    // Indicate that the back buffer will now be used to present.
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_cpRenderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_cpCommList->ResourceBarrier(1, &barrier);

    return true;
}

bool TiledResourceSample::endCommandList()
{
    if (FAILED(m_cpCommList->Close()))
        return false;
    return true;
}

void TiledResourceSample::executeCommandList()
{
    ID3D12CommandList* ppCommandLists[] = { m_cpCommList.Get() };
    m_cpCommQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}


bool TiledResourceSample::executeCommandListAndPresent()
{
    
    executeCommandList();

    if (FAILED(m_cpSwapChain->Present(1, 0)))
        return false;

    return true;
}

bool TiledResourceSample::update()
{
    constexpr float speed = 0.5f;
    constexpr float curSpeedX = bufferWidth*0.2f;
    constexpr float curSpeedY = bufferHeight * 0.2f;
    float dt = m_spTimer->getDelta();
   
    if (m_spInput)
        m_spInput->update();

    if (m_spCamera)
        m_spCamera->tick(dt);

    if (m_left)
        m_cursourPosition.x -= curSpeedX * dt;
    else
        m_cursourPosition.x += curSpeedX * dt;

    if (m_up)
        m_cursourPosition.y -= curSpeedY * dt;
    else
        m_cursourPosition.y += curSpeedY * dt;

    //128 texels - standart size of tile for R8G8B8A8_UNORM format
    if (m_cursourPosition.x > bufferWidth - reservedWidth)
    {
        m_cursourPosition.x = bufferWidth - reservedWidth;
        m_left = true;
    }
    if (m_cursourPosition.x < 0)
    {
        m_left = false;
        m_cursourPosition.x = 0;
    }
    if (m_cursourPosition.y > bufferHeight - reservedHeight)
    {
        m_cursourPosition.y = bufferHeight - reservedHeight;
        m_up = true;
    }
    if (m_cursourPosition.y < 0)
    {
        m_cursourPosition.y = 0;
        m_up = false;
    }

	return true;
}

bool TiledResourceSample::render()
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

bool TiledResourceSample::createRootSignatureAndPSO()
{

// ROOT SIGNATURE:

    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(m_cpD3DDev->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

    CD3DX12_DESCRIPTOR_RANGE1 ranges[1]; // diffuse
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

    CD3DX12_ROOT_PARAMETER1 rootParameters[2];
    rootParameters[0].InitAsConstants(16, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX); // wvp + w + lightVec + eyeDir
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
    ID3DBlob* errorBlob;

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
        if( errorBlob->GetBufferSize() > 0)
            MessageBoxA(0, reinterpret_cast<const char*>(errorBlob->GetBufferPointer()), "Vertex Shader Warning", MB_OK);
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
            MessageBoxA(0, reinterpret_cast<const char*>(errorBlob->GetBufferPointer()), "Pixel Shader Warning", MB_OK);
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
    psoDesc.VS = { reinterpret_cast<UINT8*>(vertexShader->GetBufferPointer()), vertexShader->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<UINT8*>(pixelShader->GetBufferPointer()), pixelShader->GetBufferSize() };
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
