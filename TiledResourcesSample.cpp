#include "TiledResourcesSample.h"
#include "DX12MeshGenerator.h"
#include "DDSTextureLoader12.h"
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

TiledResourcesSample::TiledResourcesSample(std::wstring name,
    DX12Framework::DX12FRAMEBUFFERING buffering, bool useWARP) :
    DX12Framework(name, buffering, useWARP),
    m_cursourPosition(0, bufferHeight/2),
    m_positionChandged(true),
    m_fillingSize(0),
    m_left(false),
    m_up(false),
    m_lightRotRadius( 5.f ),
    m_lightRotAngle( 0.f )
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

bool TiledResourcesSample::createReservedResource()
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
            //heapRangeOffset[tilesUsed] = heapOffset++;
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

/*
* void TiledResourcesSample::fillReservedTextureFromBuffer()
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
    
    UINT sizeInBytes = numTiles * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; // reserved heap size
    void* upload = 0;

    ADDRESS uploadOffset;
    upload = m_spRingBuffer->allocate( m_frameIndex, sizeInBytes, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, &uploadOffset);
  
    //---------------------------------------
    // Upload data to reserved texture
    //---------------------------------------
    auto buffer = reinterpret_cast<unsigned char*>(m_textureBuffer.get());
    auto tileOffset = tilings[0].StartTileIndexInOverallResource;
    auto numTilesInSubresource = tilings[0].WidthInTiles * tilings[0].HeightInTiles;
    UINT tileRow = tileShape.WidthInTexels;
    UINT tileCol= tileShape.HeightInTexels;

    unsigned char* uploadCur = reinterpret_cast<unsigned char*>(upload);

    UINT count = 0;
    uploadCur += tilings[0].StartTileIndexInOverallResource * D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

    UINT rx = tileRow * tilings[0].WidthInTiles;
    UINT ry = tileCol * tilings[0].HeightInTiles;
    UINT uy = static_cast<UINT>(m_cursourPosition.y);

    count = 0;
    for (UINT y = uy; y < uy + ry; y++)
    {
        UINT px = static_cast<UINT> (m_cursourPosition.x);
        UINT py = (y)*bufferWidth;
        size_t pos = (px + py) * 4;

        memcpy(uploadCur, &buffer[pos], rx * 4);
        uploadCur += rx * 4;
        count += rx * 4;
    }

    for (UINT ty = 0; ty < tilings[0].HeightInTiles; ty++)
    {
        for (UINT tx = 0; tx < tilings[0].WidthInTiles; tx++)
        {
            if ( tx&0x1 ^ ty&0x1 != 0)
                continue;
            copyTile(tx, ty, 0, 0, uploadOffset);
        }
    }
    

    uploadCur = reinterpret_cast<unsigned char*>(upload);
    memset(uploadCur, 0xFF, fillingSize * 4);

    //---------------------------------------
    //  Change state to PIXEL_SHADER_RESOURCE  
    //---------------------------------------
    barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(m_cpReservedResource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_cpCommList->ResourceBarrier(1, &barrier);

    m_positionChandged = false;
}
*/

void TiledResourcesSample::fillReservedTextureFromBuffer()
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

            //if ( (tx & 0x1 ^ ty & 0x1) != 0)
            //    continue;
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

            //UINT count = 0; // for debug
            UINT px;
            UINT py;
            size_t pos;
            for (UINT y = uy; y < uy + tileRow; y++)
            {
                px = static_cast<UINT> (m_cursourPosition.x) + tileRow * tx;
                py = (y)*bufferWidth;
                pos = (px + py) * 4;

                memcpy( uploadCur, &buffer[pos], tileRow * 4 );
                //memset(uploadCur, ( (tx << 3) + (ty <<3 ) ), tileRow * 4);
                uploadCur += tileRow * 4;
                //count += tileRow * 4;
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

    m_positionChandged = false;
}

void TiledResourcesSample::copyTile( UINT tx, UINT ty, UINT tz, UINT subresource, ADDRESS uploadOffset )
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
    //box.left = tx * tileShape.WidthInTexels;
    //box.right = (tx+1) * tileShape.WidthInTexels;

    //box.top = ty * tileShape.HeightInTexels;
    //box.bottom = ( ty + 1 ) * tileShape.HeightInTexels;

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

bool TiledResourcesSample::initialize()
{
	if (!initDefault())
		return false;
	if (!initInput())
		return false;

    //XMMATRIX mTang, mInvertedTang;
    //XMFLOAT3X3 mfInvertedTang, mfTang = { 0, 0, -1,  -1, 0, 0,   0, 1, 0 };
    //mTang = XMLoadFloat3x3(&mfTang);
    //mInvertedTang = XMMatrixInverse(nullptr, mTang);
    //XMStoreFloat3x3( &mfInvertedTang, mInvertedTang );
   
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    m_cpD3DDev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    bool tilingSupport = options.TiledResourcesTier != D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;
    //ASSERT(tilingSupport, "Tiled Resources Not Supported By Video Adapter!");

    m_spCamera = std::make_shared<gCamera>(m_spInput.get());
    m_spCamera->setPosition(XMFLOAT3(0, 0, -5.f));
    m_spCamera->lookAt(XMFLOAT3(1, 1, 0));
    m_spCamera->setMovementSpeed(5.f);
    m_spCamera->setNearPlane(0.2f);
        
    if (!createRootSignatureAndPSO())
        return false;

    if (!m_spRingBuffer->initialize(0x3FFFFFF)) // 64MBytes
        return false;

    DX12MeshGenerator<0, 12, 24, 36> meshGen(m_spRingBuffer);
    DX12MeshGeneratorOffets offs;
    bool r = meshGen.buildIndexedCube(offs, 0.4f, 0.4f, 0.4f,false);


    //struct vert
    //{
    //    float x, y, z;
    //    float nx, ny, nz;
    //    float tx, ty, tz;
    //    float tu, tv;
    //};

    //
    //vert* _v = reinterpret_cast<vert*>(offs.vdata);
    //WORD* _i = reinterpret_cast<WORD*>(offs.idata);
    //FILE* f = 0;

    //auto err = fopen_s(&f, "out_box.txt", "wt");
    //for (int i = 0; i < 24; i++)
    //{
    //    fprintf(f, "v%i: xyz: %f %f %f norm: %f %f %f tan: %f %f %f tc: %f %f\n",
    //        i, _v->x, _v->y, _v->z, _v->nx, _v->ny, _v->nz,
    //        _v->tx, _v->ty, _v->tz, _v->tu, _v->tv);
    //    _v++;
    //}

    //for (int i = 0, c=0; i < 36; i += 3,c++)
    //{
    //    fprintf(f, "t%i: %i %i %i\n",
    //        c,_i[0], _i[1], _i[2] );
    //    _i += 3;
    //}

    //fclose(f);


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

    //D3D12_SUBRESOURCE_DATA srData = {};
    //srData.pData = offs.vdata;
    //srData.RowPitch = static_cast<LONG_PTR>(offs.vDataSize);
    //srData.SlicePitch = offs.vDataSize;

    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_cpVB.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        //UpdateSubresources(m_cpCommList.Get(), m_cpVB.Get(), m_spRingBuffer->getResource(),
        //    offs.vDataOffset, 0, 1, &srData);
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

    //srData = {};
    //srData.pData = offs.idata;
    //srData.RowPitch = static_cast<LONG_PTR>(offs.iDataSize);
    //srData.SlicePitch = offs.iDataSize;

    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_cpIB.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        //UpdateSubresources(m_cpCommList.Get(), m_cpIB.Get(), m_spRingBuffer->getResource(),
        //   offs.iDataOffset, 0, 1, &srData);
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

    //Test load diffuse and normalmap dds:

    //std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    //std::string str = converter.to_bytes(L"../../��������/rocks.dds");
    //std::wstring wstr = converter.from_bytes( str.c_str() );

    //int size = static_cast<int>(strlen(str.c_str()) + 1);
    //WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &str[0], size, NULL, NULL);


    {   // Base texture
        std::unique_ptr<uint8_t[]> ddsData;
        std::vector<D3D12_SUBRESOURCE_DATA> subResDataVector;
        ID3D12Resource* pResource;

        HRESULT tlResult = LoadDDSTextureFromFile(m_cpD3DDev.Get(), L"..\\..\\��������\\stones.dds",
            &pResource, ddsData, subResDataVector);
        if (FAILED(tlResult))
        {
            MessageBox(0, L"Cannot load texture!", L"Error", MB_OK | MB_ICONERROR);
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

        HRESULT tlResult = LoadDDSTextureFromFile(m_cpD3DDev.Get(), L"..\\..\\��������\\stones_NM_height.dds",
            &pResource, ddsData, subResDataVector);
        if (FAILED(tlResult))
        {
            MessageBox(0, L"Cannot load normal map!", L"Error", MB_OK | MB_ICONERROR);
            return false;
        }

        uploadSubresources(pResource, static_cast<UINT>(subResDataVector.size()), &subResDataVector[0]);
        createSRVTex2D(pResource, 1);
        createSRVTex2D(pResource, RESERVER_RES_DHEAP_OFFSET + 1);

        m_cpNormalMap = pResource;
    }

    // create reserved resource
    if (!createReservedResource())
        return false;

    endCommandList();
    executeCommandList();
    WaitForGpu();

    m_spWindow->showWindow(true);

	return true;
}


bool TiledResourcesSample::beginCommandList()
{
    if (FAILED(m_cpCommAllocator->Reset()))
        return false;

    if (FAILED(m_cpCommList->Reset(m_cpCommAllocator.Get(), m_cpPipelineState[1].Get())))
    //if (FAILED(m_cpCommList->Reset(m_cpCommAllocator.Get(), nullptr)))
        return false;

    return true;
}

bool TiledResourcesSample::populateCommandList()
{
    // Set necessary state.
    m_cpCommList->SetGraphicsRootSignature(m_cpRootSignature[1].Get());

    //test set pipeline state:
    //m_cpCommList->SetPipelineState(m_cpPipelineState[1].Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_cpSRVHeap.Get() };
    m_cpCommList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    CD3DX12_GPU_DESCRIPTOR_HANDLE h(m_cpSRVHeap->GetGPUDescriptorHandleForHeapStart(), 
        0, m_srvDescriptorSize );
    CD3DX12_GPU_DESCRIPTOR_HANDLE h2(m_cpSRVHeap->GetGPUDescriptorHandleForHeapStart(), 
        RESERVER_RES_DHEAP_OFFSET, m_srvDescriptorSize);


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
    m_cpCommList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

    m_cpCommList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cpCommList->IASetVertexBuffers(0, 1, &m_vb);
    m_cpCommList->IASetIndexBuffer(&m_ib);

    if (m_positionChandged)
        fillReservedTextureFromBuffer();

    XMFLOAT4X4 fmWVP;
    XMFLOAT4X4 fmW;
    XMMATRIX mWVP, mVP, mTranslation, mRotationX, mRotationY, mRotationZ;
    mVP = m_spCamera->getViewProjMatrix();

    //draw cube with DDS testure
    mTranslation = XMMatrixTranslation(0.f, 0.f, 0.f);
    mWVP = mTranslation * mVP;
    XMStoreFloat4x4( &fmWVP, XMMatrixTranspose(mWVP) );
    XMStoreFloat4x4( &fmW, XMMatrixTranspose(mTranslation) );

    // set light dir
    float lightPos[4] = { cosf(m_lightRotAngle) * m_lightRotRadius, 0.f, 
        sinf(m_lightRotAngle) * m_lightRotRadius, 0.4f };
    m_cpCommList->SetGraphicsRoot32BitConstants( 0, 4, &lightPos, 32 );

    // set view dir
    XMFLOAT3 viewDir;
    XMStoreFloat3( &viewDir, m_spCamera->getDirectionVector());
    m_cpCommList->SetGraphicsRoot32BitConstants( 0, 3, &viewDir, 36 );

    m_cpCommList->SetGraphicsRootDescriptorTable( 3, h );
    m_cpCommList->SetGraphicsRoot32BitConstants( 0, 16, &fmWVP, 0 );
    m_cpCommList->SetGraphicsRoot32BitConstants( 0, 16, &fmW, 16 );
    m_cpCommList->DrawIndexedInstanced( 36, 1, 0, 0, 0 );

    // draw cube with reserved texture
    mTranslation = XMMatrixTranslation( 1.f, 0.f, 0.f );
    mWVP = mTranslation * mVP;
    XMStoreFloat4x4(&fmWVP, XMMatrixTranspose(mWVP));
    XMStoreFloat4x4(&fmW, XMMatrixTranspose(mTranslation));

    //h.Offset(1, m_srvDescriptorSize);
    m_cpCommList->SetGraphicsRootDescriptorTable(3, h2);
    m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmWVP, 0);
    m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmW, 16);
    m_cpCommList->DrawIndexedInstanced(36, 1, 0, 0, 0);

    // draw third cube with DDS texture
    mTranslation = XMMatrixTranslation(2.f, 0.f, 0.f);
    mWVP = mTranslation * mVP;
    XMStoreFloat4x4(&fmWVP, XMMatrixTranspose(mWVP));
    XMStoreFloat4x4(&fmW, XMMatrixTranspose(mTranslation));
    //h.Offset(-1, m_srvDescriptorSize);
    m_cpCommList->SetGraphicsRootDescriptorTable(3, h);
    m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmWVP, 0);
    m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmW, 16);
    m_cpCommList->DrawIndexedInstanced(36, 1, 0, 0, 0);

    // upper level
    mTranslation = XMMatrixTranslation(0.f, 1.f, 0.f);
    mWVP = mTranslation * mVP;
    XMStoreFloat4x4(&fmWVP, XMMatrixTranspose(mWVP));
    XMStoreFloat4x4(&fmW, XMMatrixTranspose(mTranslation));

    //m_cpCommList->SetGraphicsRootDescriptorTable(3, h);
    m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmWVP, 0);
    m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmW, 16);
    m_cpCommList->DrawIndexedInstanced(36, 1, 0, 0, 0);

    // draw cube with reserved texture
    mTranslation = XMMatrixTranslation(1.f, 1.f, 0.f);
    mRotationX = XMMatrixRotationX(-m_lightRotAngle * 0.1f);
    mRotationY = XMMatrixRotationY(-m_lightRotAngle * 0.1f); // XM_PIDIV2/2
    mRotationZ = XMMatrixRotationZ(-m_lightRotAngle * 0.1f);
    
    mWVP = mRotationX * mRotationY * mRotationZ * mTranslation * mVP;
    //mWVP =  mRotationY * mTranslation * mVP;
    XMStoreFloat4x4(&fmWVP, XMMatrixTranspose(mWVP));
    XMStoreFloat4x4(&fmW, XMMatrixTranspose(mRotationX * mRotationY * mRotationZ * mTranslation));

    //m_cpCommList->SetGraphicsRootDescriptorTable(3, h);
    m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmWVP, 0);
    m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmW, 16);
    m_cpCommList->DrawIndexedInstanced(36, 1, 0, 0, 0);

    // draw third cube with DDS texture
    mTranslation = XMMatrixTranslation(2.f, 1.f, 0.f);
    mWVP = mTranslation * mVP;
    XMStoreFloat4x4(&fmWVP, XMMatrixTranspose(mWVP));
    XMStoreFloat4x4(&fmW, XMMatrixTranspose(mTranslation));

    //m_cpCommList->SetGraphicsRootDescriptorTable(3, h);
    m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmWVP, 0);
    m_cpCommList->SetGraphicsRoot32BitConstants(0, 16, &fmW, 16);
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
    constexpr float curSpeedX = bufferWidth*0.2f;
    constexpr float curSpeedY = bufferHeight * 0.2f;
    float dt = m_spTimer->getDelta();
   
    m_lightRotAngle += dt * 0.4f;

    if (m_spInput)
        m_spInput->update();

    if (m_spCamera)
        m_spCamera->tick(dt);

    /*
    if (m_spInput->isKeyPressed(DIK_LEFT))
    {
        m_positionChandged = true;
        m_cursourPosition.x -= curSpeed * dt;
    }
    if (m_spInput->isKeyPressed(DIK_RIGHT))
    {
        m_positionChandged = true;
        m_cursourPosition.x += curSpeed * dt;
    }
    if (m_spInput->isKeyPressed(DIK_UP))
    {
        m_positionChandged = true;
        m_cursourPosition.y -= curSpeed * dt;
    }
    if (m_spInput->isKeyPressed(DIK_DOWN))
    {
        m_positionChandged = true;
        m_cursourPosition.y += curSpeed * dt;
    }
    */

    if( m_left )
        m_cursourPosition.x -= curSpeedX * dt;
    else
        m_cursourPosition.x += curSpeedX * dt;

    if( m_up )
        m_cursourPosition.y -= curSpeedY * dt;
    else
        m_cursourPosition.y += curSpeedY * dt;
    m_positionChandged = true;

    //if (m_spInput->isKeyPressed(DIK_O))
    //{
    //    m_positionChandged = true;
    //    m_fillingSize -= 128 * 16;
    //}
    //if (m_spInput->isKeyPressed(DIK_P))
    //{
    //    m_positionChandged = true;
    //    fillingSize += 128 * 16;
    //}
    //if (fillingSize < 128 * 16)
    //    fillingSize = 128 * 16;
    //if (fillingSize > reservedWidth * reservedHeight)
    //    fillingSize = reservedWidth * reservedHeight;

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


    DX12WINDOWPARAMS wParams = m_spWindow->getWindowParameters();
    wParams.name = L"Cur: " + std::to_wstring(m_cursourPosition.x) + L" "
                           + std::to_wstring(m_cursourPosition.y);
    wParams.x = 100; wParams.y = 100;
    //m_spWindow->setWindowParameters(wParams);

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

    CD3DX12_DESCRIPTOR_RANGE1 ranges[1]; // diffuse + nmap
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

    CD3DX12_ROOT_PARAMETER1 rootParameters[4];
    rootParameters[0].InitAsConstants(40, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX); // wvp + w + lightVec + eyeDir
    rootParameters[1].InitAsConstants(16, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX); // not use, delete it
    rootParameters[2].InitAsConstants(4, 2, 0, D3D12_SHADER_VISIBILITY_VERTEX); // not use, delete it
    rootParameters[3].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL); // srv descriptor

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

    //const char vs_source[] =
    //    "struct VS_INPUT                    \n"
    //    "{                                   \n"
    //    "    float3 pos : POSITION;          \n"
    //    "    float3 normal : NORMAL;          \n"
    //    "    float3 tangent : TANGENT;      \n"
    //    "    float2 texCoord : TEXCOORD;     \n"
    //    "};                                 \n\n"

    //    "struct VS_OUTPUT                       \n"
    //    "{                                      \n"
    //    "    float4 pos : SV_POSITION;          \n"
    //    "    float2 texCoord : TEXCOORD0;        \n"
    //    "    float4 lightVec : TEXCOORD1;       \n"
    //    "    float3 normal : TEXCOORD2;          \n"
    //    "    float3 binormal : TEXCOORD3;          \n"
    //    "    float3 tangent : TEXCOORD4;          \n"
    //    "    float3 viewDir : TEXCOORD5;       \n"
    //    "}; \n\n"

    //    "struct sConstantBuffer                 \n"
    //    "{                                      \n"
    //    "    float4x4 wvpMat;                   \n"
    //    "    float4x4 wMat;                     \n"
    //    "    float4 lightVec;                   \n"  
    //    "    float3 viewDir;                    \n"
    //    "};                                     \n\n"
    //    "ConstantBuffer<sConstantBuffer> myCBuffer : register(b0); \n\n"
    //    "VS_OUTPUT main(VS_INPUT input)         \n"
    //    "{                                      \n"
    //    "   VS_OUTPUT output; \n"
    //    "   output.pos = mul( float4( input.pos, 1.0f), myCBuffer.wvpMat );\n"
    //    "   output.normal = normalize( mul( float4(input.normal,0.f), myCBuffer.wMat ) ).xyz; \n"
    //    "   output.tangent = normalize( mul( float4(input.tangent,0.f), myCBuffer.wMat ) ).xyz; \n"
    //    //"   output.tangent = input.tangent; \n"
    //    "   output.binormal = cross( output.tangent, output.normal ); \n"
    //    "   output.texCoord = input.texCoord;  \n"
    //    "   output.lightVec.xyz = normalize( myCBuffer.lightVec.xyz);   \n"
    //    "   output.lightVec.w = myCBuffer.lightVec.w;   \n"
    //    "   output.viewDir = myCBuffer.viewDir;     \n"
    //    "   return output;                          \n"
    //    "}\n";

    //const char ps_source[] =
    //    "struct PSInput                         \n"
    //    "{                                      \n"
    //    "    float4 position : SV_POSITION;     \n"
    //    "    float2 uv : TEXCOORD0;              \n"
    //    "    float4 lightVec : TEXCOORD1;       \n"
    //    "    float3 normal : TEXCOORD2;          \n"
    //    "    float3 binormal : TEXCOORD3;          \n"
    //    "    float3 tangent : TEXCOORD4;          \n"
    //    "    float3 viewDir : TEXCOORD5;       \n"
    //    "};                                     \n\n"

    //    "Texture2D g_texture : register(t0);       \n"
    //    "Texture2D g_normMap : register(t1);       \n"
    //    "SamplerState g_sampler : register(s0);     \n\n"

    //    " // lightVec.w = ambient intensivity \n"
    //    "float4 main(PSInput input) : SV_TARGET     \n"
    //    "{                                          \n"
    //    "    //input.normal.z = -input.normal.z;      \n"
    //    "    float4 diffuseSample = g_texture.Sample(g_sampler, input.uv); \n"
    //    "    float3 normalSample =  g_normMap.Sample(g_sampler, input.uv).xyz   * 2.f - 1.f; \n"
    //    "    float3x3 mTangentSpace = float3x3( input.tangent, input.binormal, input.normal ); \n"
    //    "    normalSample = mul( normalSample, mTangentSpace ); \n"
    //    //"    normalize( normalSample ); \n"
    //    //"  float4 diffuseComponent =  input.lightVec.w + max( dot( input.normal, input.lightVec.xyz ), 0.f ); \n"
    //    "    float4 diffuseComponent = input.lightVec.w + max(dot( normalSample, input.lightVec.xyz), 0.f); \n"
    //    "    float4 finalColor = diffuseComponent;\n"// * diffuseSample;\n"
    //    "    return finalColor;                                         \n"
    //    "    //return float4( (normalSample.xyz+1) * 0.5f, 1.f );\n"
    //    //"  return g_texture.SampleLevel( g_sampler, input.uv, 0); \n"
    //    "} \n";


    const char vs_source[] =
        "struct VS_INPUT                    \n"
        "{                                   \n"
        "    float3 pos : POSITION;          \n"
        "    float3 normal : NORMAL;          \n"
        "    float3 tangent : TANGENT;      \n"
        "    float2 texCoord : TEXCOORD;     \n"
        "};                                 \n\n"

        "struct VS_OUTPUT                       \n"
        "{                                      \n"
        "    float4 pos : SV_POSITION;          \n"
        "    float2 texCoord : TEXCOORD0;        \n"
        "    float4 lightVecTS : TEXCOORD1;       \n"
        "    float3 normal : TEXCOORD2;          \n"
        "    float3 binormal : TEXCOORD3;          \n"
        "    float3 tangent : TEXCOORD4;          \n"
        "    float3 viewDirTS : TEXCOORD5;       \n"
        //"    float2 parallaxOffset : TEXCOORD6;       \n"
        "}; \n\n"

        "struct sConstantBuffer                 \n"
        "{                                      \n"
        "    float4x4 wvpMat;                   \n"
        "    float4x4 wMat;                     \n"
        "    float4 lightVec;                   \n"
        "    float3 viewDir;                    \n"
        "};                                     \n\n"
        "ConstantBuffer<sConstantBuffer> myCBuffer : register(b0); \n\n"
        "VS_OUTPUT main(VS_INPUT input)         \n"
        "{                                      \n"
        "   VS_OUTPUT output; \n"
        "   output.pos = mul( float4( input.pos, 1.0f), myCBuffer.wvpMat );\n"
        "   output.texCoord = input.texCoord;  \n"

        "   output.normal = normalize( mul( input.normal, (float3x3)myCBuffer.wMat ) ).xyz; \n"
        "   output.tangent = normalize( mul( input.tangent, (float3x3)myCBuffer.wMat ) ).xyz; \n"
        "   output.binormal = normalize( cross( output.tangent, output.normal ) ); \n"

        "   float3x3 mTangentSpace = float3x3( output.tangent, output.binormal, output.normal ); \n"
        "   output.lightVecTS.xyz = mul( mTangentSpace, -normalize( myCBuffer.lightVec.xyz ) );   \n"
        "   output.lightVecTS.w = myCBuffer.lightVec.w;   \n"
        "   output.viewDirTS = mul( mTangentSpace, normalize( myCBuffer.viewDir ) );     \n"
        "   \n"
        //"   float2 parallaxDirection = normalize(output.viewDirTS.xy);  \n"
        //"   float viewDirLength = length(output.viewDirTS);    \n"
        //"   float parallaxLength = sqrt(viewDirLength * viewDirLength - output.viewDirTS.z * output.viewDirTS.z) / output.viewDirTS.z; \n"
        //"   output.parallaxOffset = parallaxDirection * parallaxLength; \n"
        //"   output.parallaxOffset *= 0.02f; \n"
        "   \n"
        "   return output;                          \n"
        "}\n";

    const char ps_source[] =
        "struct PSInput                         \n"
        "{                                      \n"
        "    float4 position : SV_POSITION;     \n"
        "    float2 uv : TEXCOORD0;              \n"
        "    float4 lightVecTS : TEXCOORD1;       \n"
        "    float3 normal : TEXCOORD2;          \n"
        "    float3 binormal : TEXCOORD3;          \n"
        "    float3 tangent : TEXCOORD4;          \n"
        "    float3 viewDirTS : TEXCOORD5;       \n"
        //"    float2 parallaxOffset : TEXCOORD6;       \n"
        "};                                     \n\n"

        "Texture2D g_texture : register(t0);       \n"
        "Texture2D g_normMap : register(t1);       \n"
        "SamplerState g_sampler : register(s0);     \n\n"

        " // lightVec.w = ambient intensivity \n"
        "float4 main(PSInput input) : SV_TARGET     \n"
        "{                                          \n"
        "// Simple Parallax Mappint implementation: \n"
        "   const float sfHeightBias = 0.01;      \n"
        "   float fCurrentHeight = g_normMap.Sample(g_sampler, input.uv).w;      \n"
        "   float fHeight = fCurrentHeight * 0.005f + sfHeightBias;      \n"
        "   fHeight /= input.viewDirTS.z;      \n"
        "   float2 texSample = input.uv + input.viewDirTS.xy * fHeight;      \n"

        "    float4 diffuseSample = g_texture.Sample(g_sampler, texSample); \n"
        "    float4 normalSample =  g_normMap.Sample(g_sampler, texSample); \n"
        //"    float4 normalSample =  g_normMap.Sample(g_sampler, input.uv); \n"
        "    normalSample.xyz =  normalSample.xyz  * 2.f - 1.f;        \n"
        "    float power = 24; \n"
        "    float4 diffuseComponent = input.lightVecTS.w + saturate( dot( normalSample.xyz, input.lightVecTS.xyz) ) * input.lightVecTS.w; \n"
        "    float4 specularComponent = 0.7f * pow( saturate( dot( reflect( input.viewDirTS, normalSample.xyz ), input.lightVecTS.xyz ) ), power ); \n"
        "    float4 finalColor = ( specularComponent + diffuseComponent ) * diffuseSample;\n"
        "    return finalColor;                                         \n"
        "    //return float4( (normalSample.xyz+1) * 0.5f, 1.f );\n"
        //"  return g_texture.SampleLevel( g_sampler, input.uv, 0); \n"
        "} \n";


    ID3DBlob* errorBlob;

    if (FAILED(D3DCompile(vs_source, sizeof(vs_source), "vs_tiled", nullptr,
        nullptr, "main", "vs_5_1", compileFlags, 0, &vertexShader, &errorBlob)))
    {
        MessageBoxA(0, reinterpret_cast<const char*>(errorBlob->GetBufferPointer()), "Vertex Shader Error", MB_OK);
        errorBlob->Release();
        return false;
    }
    if (errorBlob)
    {
        if( errorBlob->GetBufferSize() > 0)
            MessageBoxA(0, reinterpret_cast<const char*>(errorBlob->GetBufferPointer()), "Vertex Shader Warning", MB_OK);
        errorBlob->Release();
    }

    if (FAILED(D3DCompile(ps_source, sizeof(ps_source), "ps_tiled", nullptr,
        nullptr, "main", "ps_5_1", compileFlags, 0, &pixelShader, &errorBlob)))
    {
        MessageBoxA(0, reinterpret_cast<const char*>(errorBlob->GetBufferPointer()), "Pixel Shader Error", MB_OK);
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
