#include <windows.h>
#include <wrl.h>
#include "util.h"
#include "input.h"
#include "RingUploadBuffer.h"

//#include <d3d12.h>
#include <d3d12sdklayers.h>
#include "d3dx12.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#pragma comment( lib , "d3d12.lib" )
#pragma comment( lib , "d3dcompiler.lib" )
#pragma comment( lib , "dxguid.lib" )
#pragma comment( lib , "dxgi.lib" )

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT2 texcoord;
    XMFLOAT4 color;
};

const UINT texDim = 256;

const UINT width = 800;
const UINT height = 600;
const UINT FrameCount = 2;
const float aspectRatio = (float)width / (float)height;

gInput* input = 0;

size_t rtvDescriptorSize = 0;
size_t dsvDescriptorSize = 0;
size_t srvDescriptorSize = 0;

D3D_DRIVER_TYPE                     driverType = D3D_DRIVER_TYPE_NULL;
ComPtr< ID3D12Device >              pD3DDev;
ComPtr< ID3D12CommandQueue >        pCommQueue;
ComPtr< ID3D12CommandAllocator >    pCommAllocator;
ComPtr< ID3D12GraphicsCommandList > pCommList;
ComPtr< IDXGISwapChain3 >           pSwapChain;
ComPtr< ID3D12DescriptorHeap >      pRTVHeap;
ComPtr< ID3D12DescriptorHeap >      pCBVSrvHeap;
ComPtr< ID3D12DescriptorHeap >      pDSVHeap;
ComPtr< ID3D12DescriptorHeap >      pSRVHeap;
ComPtr< ID3D12RootSignature >       pRootSignature;
ComPtr< ID3D12PipelineState >       pPipelineState;
ComPtr< ID3D12Resource >            pRenderTargets[FrameCount];
ComPtr< ID3D12Resource >            pDepthStencil;
ComPtr< ID3D12Resource >            textures[16];
ComPtr< ID3D12Resource >            textureUploads[16];

std::shared_ptr<gRingUploadBuffer> spRingBuffer;


// App resources
ComPtr<ID3D12Resource> pVertexBuffer;
D3D12_VERTEX_BUFFER_VIEW pVertexBufferView;

// Syncronization objects
UINT frameIndex = 0;
HANDLE fenceEvent;
ComPtr<ID3D12Fence> pFence;
UINT64 fenceValue;

D3D12_VIEWPORT viewport;
D3D12_RECT scissorRect;

void WaitForCommandsComplete()
{
    HRESULT hr;

    // Signal and increment the fence value.
    static UINT64 commFenceValue;
    constexpr UINT64 fence = 0xFFFFFFFFFFFFFFFE;
    pCommQueue->Signal(pFence.Get(), fence);

    // Wait until the previous frame is finished.
    if (pFence->GetCompletedValue() < fence)
    {
        hr = pFence->SetEventOnCompletion(fence, fenceEvent);
        if (FAILED(hr))
            throw("Fence->SetEventOnCompletion() failed!");
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

void WaitForPreviousFrame()
{
    HRESULT hr;
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. More advanced samples 
    // illustrate how to use fences for efficient resource usage.

    // Signal and increment the fence value.
    const UINT64 fence = fenceValue;
    pCommQueue->Signal( pFence.Get(), fence );
    fenceValue++;

    // Wait until the previous frame is finished.
    if ( pFence->GetCompletedValue() < fence)
    {
        hr = pFence->SetEventOnCompletion( fence, fenceEvent );
        if (FAILED(hr))
            throw("Fence->SetEventOnCompletion() failed!");
        WaitForSingleObject( fenceEvent, INFINITE );
    }

    frameIndex = pSwapChain->GetCurrentBackBufferIndex();
}

void PopulateCommandList()
{
    HRESULT hr;
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    hr = pCommAllocator->Reset();
    if (FAILED(hr))
        throw("CommandAllocator->Reset() failed!");

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    hr = pCommList->Reset(pCommAllocator.Get(), pPipelineState.Get());
    if (FAILED(hr))
        throw("CommandList->Reset() failed!");

    // Set necessary state.
    pCommList->SetGraphicsRootSignature(pRootSignature.Get());

    ID3D12DescriptorHeap* ppHeaps[] = { pSRVHeap.Get() };
    pCommList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    
    CD3DX12_GPU_DESCRIPTOR_HANDLE h( pSRVHeap->GetGPUDescriptorHandleForHeapStart() );

    pCommList->RSSetViewports(1, &viewport);
    pCommList->RSSetScissorRects(1, &scissorRect);

    // Indicate that the back buffer will be used as a render target.
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(pRenderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    pCommList->ResourceBarrier(1, &barrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(pRTVHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(pDSVHeap->GetCPUDescriptorHandleForHeapStart());
   
    pCommList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.4f, 0.2f, 1.0f };
    pCommList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    pCommList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, 0 );
 

    pCommList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pCommList->IASetVertexBuffers(0, 1, &pVertexBufferView);

    for (int i = 0; i < 8; i++)
    {
        pCommList->SetGraphicsRootDescriptorTable(1, h);
        h.Offset(srvDescriptorSize);
        pCommList->DrawInstanced(6, 2, i * 6, 0);
    }

    //render 2nd quad
    //h.Offset(srvDescriptorSize);
    //pCommList->SetGraphicsRootDescriptorTable(1, h);
    //pCommList->DrawInstanced(6, 2, 6, 0);


    // Indicate that the back buffer will now be used to present.
    barrier = CD3DX12_RESOURCE_BARRIER::Transition( pRenderTargets[frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    pCommList->ResourceBarrier(1, &barrier);

    hr = pCommList->Close();
    if (FAILED(hr))
        throw("CommandList->Close() failed!");
}

void frame_render()
{
    HRESULT hr;
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { pCommList.Get() };
    pCommQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    hr = pSwapChain->Present(1, 0);
    if (FAILED(hr))
        throw("SwapChain->Present() failed!");

    WaitForPreviousFrame();
}

bool frame_move()
{
	return true;
}

void cleanUp()
{
	if (input)
	{
		input->close();
		delete input;
		input = 0;
	}

    // Wait for the GPU to be done with all resources.
    WaitForPreviousFrame();
    CloseHandle(fenceEvent);
}

void GetHardwareAdapter(
    IDXGIFactory1* pFactory,
    IDXGIAdapter1** ppAdapter,
    bool requestHighPerformanceAdapter)
{
    *ppAdapter = nullptr;

    ComPtr<IDXGIAdapter1> adapter;

    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        for (
            UINT adapterIndex = 0;
            DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(
                adapterIndex,
                requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                IID_PPV_ARGS(&adapter));
            ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            //continue;

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
    }
    else
    {
        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                // If you want a software adapter, pass in "/warp" on the command line.
                continue;
            }

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
    }

    *ppAdapter = adapter.Detach();
}

void initDX12()
{
    viewport.TopLeftX = viewport.TopLeftY = 0;
    viewport.Width = width;
    viewport.Height = height;

    scissorRect.left = scissorRect.top = 0;
    scissorRect.bottom = height;
    scissorRect.right = width;

	UINT dxgiFactoryFlags = 0;
	HRESULT hr;

    //-------------------------------------------------------------
#if defined(_DEBUG)
    // Enable the D3D12 debug layer.
    {

        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
        }
    }
#endif

	//-------------------------------------------------------------

	ComPtr < IDXGIFactory4 > factory;
	CreateDXGIFactory1(IID_PPV_ARGS(&factory));
	hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));

	if (FAILED(hr))
		throw("Cannot create DXGI Factory!");

	//-------------------------------------------------------------

	ComPtr < IDXGIAdapter1 > hardwareAdapter;
	GetHardwareAdapter( factory.Get(), &hardwareAdapter, true );

	hr = D3D12CreateDevice( hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&pD3DDev));

	if (FAILED(hr))
		throw("Cannot create DX12 device!");

	DXGI_ADAPTER_DESC1 description;
	hardwareAdapter->GetDesc1(&description);
	///wnd_setTitle( (const char*)description.Description );

	//-------------------------------------------------------------

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	hr = pD3DDev->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pCommQueue));

	if (FAILED(hr))
		throw("Cannot create D3D12 command queue!");

    //-------------------------------------------------------------
	ComPtr<IDXGISwapChain> swapChain;
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.BufferDesc.Width = width;
    swapChainDesc.BufferDesc.Height = height;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;

    hr = factory->CreateSwapChain( pCommQueue.Get(), &swapChainDesc, &swapChain );
	if (FAILED(hr))
		throw("Cannot create d3d12 swap chain!");

	swapChain.As(&pSwapChain);
	pSwapChain->GetCurrentBackBufferIndex();

	//-------------------------------------------------------------

	hr = factory->MakeWindowAssociation( hwnd, DXGI_MWA_NO_ALT_ENTER);
	if (FAILED(hr))
		throw("Cannot make window association!");

	frameIndex = pSwapChain->GetCurrentBackBufferIndex();

	//-------------------------------------------------------------
    // Describe and create a render target view (RTV) descriptor heap.

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = pD3DDev->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS( &pRTVHeap) );
    if (FAILED(hr))
        throw("Cannot make RTVHeapDescriptor!");
    rtvDescriptorSize = pD3DDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    //-------------------------------------------------------------
    // Describe and create a depth stencil view (DSV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = pD3DDev->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&pDSVHeap));
    if (FAILED(hr))
        throw("Cannot make DSVHeapDescriptor!");
    dsvDescriptorSize = pD3DDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    //-------------------------------------------------------------
    // Describe and create a shader resource view (SRV) heap for the texture.
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 16; // ? 5
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = pD3DDev->CreateDescriptorHeap( &srvHeapDesc, IID_PPV_ARGS(&pSRVHeap) );
    if (FAILED(hr))
        throw("Cannot make SRV descriptor!");

    srvDescriptorSize = pD3DDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    //-------------------------------------------------------------
    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( pRTVHeap->GetCPUDescriptorHandleForHeapStart() );
        // Create a RTV for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            hr = pSwapChain->GetBuffer(n, IID_PPV_ARGS(&pRenderTargets[n]));
            if (FAILED(hr))
                throw("Cannot get RT buffer!");

            pD3DDev->CreateRenderTargetView( pRenderTargets[n].Get(), nullptr, rtvHandle );
            rtvHandle.Offset( 1, rtvDescriptorSize);
            
        }

        // Create a DSV
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
            depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
            depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

            D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
            depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
            depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
            depthOptimizedClearValue.DepthStencil.Stencil = 0;

            D3D12_HEAP_PROPERTIES props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height,
                1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
           

            //test
            //depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC();
            CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(pDSVHeap->GetCPUDescriptorHandleForHeapStart());
            //pD3DDev->CreateDepthStencilView(pDepthStencil.Get(), &depthStencilDesc, dsvHandle );

            //   /*
            hr = pD3DDev->CreateCommittedResource(
                &props,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &depthOptimizedClearValue,
                IID_PPV_ARGS(&pDepthStencil) );
           //   */
            

            if (FAILED(hr))
                throw("Cannot create DSV!");

            //NAME_D3D12_OBJECT(pDepthStencil);
            pDepthStencil.Get()->SetName(L"pDepthStencil");
            pD3DDev->CreateDepthStencilView(pDepthStencil.Get(), &depthStencilDesc, pDSVHeap->GetCPUDescriptorHandleForHeapStart());
        }
    }

    hr = pD3DDev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCommAllocator));
    if (FAILED(hr))
        throw("Cannot make D3D12 command allcator!");
}

bool beginCommandList()
{
    HRESULT hr;
    hr = pCommAllocator->Reset();
    if (FAILED(hr))
        return false;
    hr = pCommList->Reset(pCommAllocator.Get(), pPipelineState.Get());
    if (FAILED(hr))
        return false;
    return true;
}

bool endCommandList()
{
    HRESULT hr = pCommList->Close();
    if (FAILED(hr))
        return false;
    ID3D12CommandList* ppCommandLists[] = { pCommList.Get() };
    pCommQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    return true;
}

void createTexture( void* pixelData, UINT width, UINT height, BYTE pixelSize, DXGI_FORMAT format,
    ComPtr<ID3D12DescriptorHeap> heap, UINT heapOffsetInDescriptors )
{   
    ComPtr<ID3D12Resource> texture; // = textures[heapOffsetInDescriptors].Get();
    ComPtr<ID3D12Resource> textureUploadHeap; // = textureUploads[heapOffsetInDescriptors].Get();

    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels = 1;
    textureDesc.Format = format;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    CD3DX12_HEAP_PROPERTIES props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    HRESULT hr = pD3DDev->CreateCommittedResource(
        &props,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture));

    if (FAILED(hr))
        throw("Cannot create texture!");

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize( texture.Get(), 0, 1 );

    //upload To RingBuffer
    UINT64 uploadOffset;
    void* lpUploadPtr = spRingBuffer->allocate(heapOffsetInDescriptors, uploadBufferSize,
        D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, &uploadOffset);

    if (!lpUploadPtr) // закончилась очередь загрузки
    {
        if (!endCommandList())
            exit(-1);
        WaitForPreviousFrame();
        if (!beginCommandList())
            exit(-1);

        spRingBuffer->clearQueue();
        lpUploadPtr = spRingBuffer->allocate(heapOffsetInDescriptors, uploadBufferSize,
            D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, &uploadOffset);
        
        if (!lpUploadPtr) 
            exit(-1);
    }
    /*
    props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC rdesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

    // Create the GPU upload buffer.
    hr = pD3DDev->CreateCommittedResource(
        &props,
        D3D12_HEAP_FLAG_NONE,
        &rdesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&textureUploadHeap));
    if (FAILED(hr))
        throw("Cannot create upload buffer!");

    */

    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = pixelData;
    textureData.RowPitch = width * pixelSize;
    textureData.SlicePitch = textureData.RowPitch * height;

    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    UpdateSubresources( pCommList.Get(), texture.Get(), spRingBuffer->getResource(), uploadOffset, 0, 1, &textureData);
    pCommList->ResourceBarrier(1, &barrier);

    // Describe and create a SRV for the texture.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    CD3DX12_CPU_DESCRIPTOR_HANDLE h = CD3DX12_CPU_DESCRIPTOR_HANDLE(heap->GetCPUDescriptorHandleForHeapStart(),
        heapOffsetInDescriptors, srvDescriptorSize);

    pD3DDev->CreateShaderResourceView( texture.Get(), &srvDesc, h );

    textures[heapOffsetInDescriptors] = texture;
    textureUploads[heapOffsetInDescriptors] = textureUploadHeap;

}

void initAssets()
{
    HRESULT hr;

    // Create ring upload buffer
    spRingBuffer = std::make_shared < gRingUploadBuffer >(pD3DDev);
    spRingBuffer->initialize(0xFFFFF);

    //----------------------------------------------
    //    Create the root signature.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(pD3DDev->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
            printf("WARNING: force use ROOT_SIGNATURE_VERSION_1_0!\n");
        }

        CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
        //ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].RegisterSpace = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;

        CD3DX12_ROOT_PARAMETER1 rootParameters[2];
        rootParameters[1].InitAsDescriptorTable( 1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL );
        //rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(ranges);
        //rootParameters[1].DescriptorTable.pDescriptorRanges = &ranges[0];
        //rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        //rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        rootParameters[0].InitAsConstantBufferView( 0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX );

        //rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // this is a constant buffer view root descriptor
        //rootParameters[0].Descriptor = rootCBVDescriptor; // this is the root descriptor for this root parameter
        //rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // our pixel shader will be the only shader accessing this parameter for now


        D3D12_STATIC_SAMPLER_DESC sampler[1] = {};
        sampler[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
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

        /*
        sampler[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler[1].MipLODBias = 0;
        sampler[1].MaxAnisotropy = 0;
        sampler[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler[1].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler[1].MinLOD = 0.0f;
        sampler[1].MaxLOD = D3D12_FLOAT32_MAX;
        sampler[1].ShaderRegister = 0;
        sampler[1].RegisterSpace = 0;
        sampler[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        sampler[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler[2].MipLODBias = 0;
        sampler[2].MaxAnisotropy = 0;
        sampler[2].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler[2].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler[2].MinLOD = 0.0f;
        sampler[2].MaxLOD = D3D12_FLOAT32_MAX;
        sampler[2].ShaderRegister = 0;
        sampler[2].RegisterSpace = 0;
        sampler[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        */

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
        if (FAILED(hr))
            throw("Cannot serialize versioned root signature!");

        hr = pD3DDev->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&pRootSignature));
        if (FAILED(hr))
            throw("Cannot create root signature!");
    }


    //------------------------------------------------------
    //  Create Pipeline state
    //------------------------------------------------------
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    
#if defined(_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    hr = D3DCompileFromFile(L"VertexShader.hlsl", nullptr, nullptr, "main", "vs_5_0", compileFlags, 0, &vertexShader, nullptr);
    if (FAILED(hr))
        throw("Cannot bild VS!");
    hr = D3DCompileFromFile(L"PixelShader.hlsl", nullptr, nullptr, "main", "ps_5_0", compileFlags, 0, &pixelShader, nullptr);
    if (FAILED(hr))
        throw("Cannot bild PS!");

    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = pRootSignature.Get();
    psoDesc.VS = { reinterpret_cast<UINT8*>(vertexShader->GetBufferPointer()), vertexShader->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<UINT8*>(pixelShader->GetBufferPointer()), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
   
    hr = pD3DDev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pPipelineState));
    if (FAILED(hr))
        throw("3DDevice->CreateGraphicsPipelineState() failed!");


    //------------------------------------------------------
    // Create the command list.
    //------------------------------------------------------
    hr = pD3DDev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pCommAllocator.Get(), pPipelineState.Get(), IID_PPV_ARGS(&pCommList));
    if (FAILED(hr))
        throw("3DDevice->CreateCommandList() failed!");
    
    // close list;
    hr = pCommList->Close();

    if (!beginCommandList())
        exit(-1);

    //----------------------------------------------
    // Create fence
    //----------------------------------------------
    hr = pD3DDev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence));
    if (FAILED(hr))
        throw("3DDevice->CreateFence() failed!");
    fenceValue = 1;

    // Create an event handle to use for frame synchronization.
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent == nullptr)
    {
        throw(HRESULT_FROM_WIN32(GetLastError()));
    }
   
    //------------------------------------------------------------
    // Create Vertex Buffer :
    //------------------------------------------------------------
    // Define the geometry for a triangle.
    const float uvscacle = 1.f;
    const float triscale = 0.2f;
    const float xoffset = 0.5f;
    const float yoffset = xoffset * aspectRatio;

    Vertex triangleVertices[] =
    {

        { { -triscale - xoffset, triscale * aspectRatio + yoffset, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale - xoffset, triscale * aspectRatio + yoffset, triscale }, { uvscacle, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { triscale - xoffset, -triscale * aspectRatio + yoffset, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },

        { { -triscale - xoffset, triscale * aspectRatio + yoffset, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale - xoffset, -triscale * aspectRatio + yoffset, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -triscale - xoffset, -triscale * aspectRatio + yoffset, triscale }, { 0.0f, uvscacle }, { 0.0f, 1.0f, 0.0f, 1.0f } },


        { { -triscale , triscale * aspectRatio + yoffset, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale , triscale * aspectRatio + yoffset, triscale }, { uvscacle, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { triscale , -triscale * aspectRatio + yoffset, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },

        { { -triscale , triscale * aspectRatio + yoffset, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale , -triscale * aspectRatio + yoffset, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -triscale , -triscale * aspectRatio + yoffset, triscale }, { 0.0f, uvscacle }, { 0.0f, 1.0f, 0.0f, 1.0f } },


        { { -triscale + xoffset , triscale * aspectRatio + yoffset, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale + xoffset, triscale * aspectRatio + yoffset, triscale }, { uvscacle, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { triscale + xoffset, -triscale * aspectRatio + yoffset, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },

        { { -triscale + xoffset, triscale * aspectRatio + yoffset, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale + xoffset, -triscale * aspectRatio + yoffset, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -triscale + xoffset, -triscale * aspectRatio + yoffset, triscale }, { 0.0f, uvscacle }, { 0.0f, 1.0f, 0.0f, 1.0f } },




        { { -triscale - xoffset, triscale * aspectRatio, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale - xoffset, triscale * aspectRatio, triscale }, { uvscacle, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { triscale - xoffset, -triscale * aspectRatio, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },

        { { -triscale - xoffset, triscale * aspectRatio, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale - xoffset, -triscale * aspectRatio, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -triscale - xoffset, -triscale * aspectRatio, triscale }, { 0.0f, uvscacle }, { 0.0f, 1.0f, 0.0f, 1.0f } },


        { { -triscale , triscale * aspectRatio, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale , triscale * aspectRatio, triscale }, { uvscacle, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { triscale , -triscale * aspectRatio, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },

        { { -triscale , triscale * aspectRatio, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale , -triscale * aspectRatio, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -triscale , -triscale * aspectRatio, triscale }, { 0.0f, uvscacle }, { 0.0f, 1.0f, 0.0f, 1.0f } },


        { { -triscale + xoffset , triscale * aspectRatio, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale + xoffset, triscale * aspectRatio, triscale }, { uvscacle, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { triscale + xoffset, -triscale * aspectRatio, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },

        { { -triscale + xoffset, triscale * aspectRatio, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale + xoffset, -triscale * aspectRatio, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -triscale + xoffset, -triscale * aspectRatio, triscale }, { 0.0f, uvscacle }, { 0.0f, 1.0f, 0.0f, 1.0f } },



        { { -triscale - xoffset, triscale * aspectRatio - yoffset, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale - xoffset, triscale * aspectRatio - yoffset, triscale }, { uvscacle, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { triscale - xoffset, -triscale * aspectRatio - yoffset, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },

        { { -triscale - xoffset, triscale * aspectRatio - yoffset, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale - xoffset, -triscale * aspectRatio - yoffset, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -triscale - xoffset, -triscale * aspectRatio - yoffset, triscale }, { 0.0f, uvscacle }, { 0.0f, 1.0f, 0.0f, 1.0f } },


        { { -triscale , triscale * aspectRatio - yoffset, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale , triscale * aspectRatio - yoffset, triscale }, { uvscacle, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { triscale , -triscale * aspectRatio - yoffset, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },

        { { -triscale , triscale * aspectRatio - yoffset, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale , -triscale * aspectRatio - yoffset, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -triscale , -triscale * aspectRatio - yoffset, triscale }, { 0.0f, uvscacle }, { 0.0f, 1.0f, 0.0f, 1.0f } },


        { { -triscale + xoffset , triscale * aspectRatio - yoffset, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale + xoffset, triscale * aspectRatio - yoffset, triscale }, { uvscacle, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { triscale + xoffset, -triscale * aspectRatio - yoffset, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },

        { { -triscale + xoffset, triscale * aspectRatio - yoffset, triscale }, { 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { { triscale + xoffset, -triscale * aspectRatio - yoffset, triscale }, { uvscacle, uvscacle }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { { -triscale + xoffset, -triscale * aspectRatio - yoffset, triscale }, { 0.0f, uvscacle }, { 0.0f, 1.0f, 0.0f, 1.0f } }

    };

    const UINT vertexBufferSize = sizeof(triangleVertices);

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

    hr = pD3DDev->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&pVertexBuffer));
    if (FAILED(hr))
        throw("3DDevice->CreateCommittedResource() failed!");

    UINT64 vbUploadOffset;
    void* vbUploadPtr = spRingBuffer->allocate(0, vertexBufferSize,
        D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, &vbUploadOffset);

    D3D12_SUBRESOURCE_DATA vData = {};
    vData.pData = triangleVertices;
    vData.RowPitch = vertexBufferSize;
    vData.SlicePitch = vertexBufferSize;

    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(pVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    UpdateSubresources( pCommList.Get(), pVertexBuffer.Get(), spRingBuffer->getResource(), vbUploadOffset, 0, 1, &vData);
    pCommList->ResourceBarrier(1, &barrier);
/*
    // Copy the triangle data to the vertex buffer.
    UINT8* pVertexDataBegin;
    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    hr = pVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
    if (FAILED(hr))
        throw("VertexBuffer->Map() failed!");

    memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
    pVertexBuffer->Unmap(0, nullptr);
*/

    // Initialize the vertex buffer view( default pool ).
    pVertexBufferView.BufferLocation = pVertexBuffer->GetGPUVirtualAddress();
    pVertexBufferView.StrideInBytes = sizeof(Vertex);
    pVertexBufferView.SizeInBytes = vertexBufferSize;

    //  Generate textures
    DWORD* tmp = new DWORD[texDim * texDim];

    DWORD val;
    for (int i = 0; i < texDim; i++)
    {
        for (int j = 0; j < texDim; j++)
        {
            int index = j + i * texDim;
            val = 0xFFFFFFFF & ( ~(i << 16) & ~(j << 8) );
            tmp[index] = val;
        }
    }
    createTexture(tmp, texDim, texDim, sizeof(DWORD), DXGI_FORMAT_R8G8B8A8_UNORM, pSRVHeap, 0);

    for (int i = 0; i < texDim; i++)
    {
        for (int j = 0; j < texDim; j++)
        {
            int index = j + i * texDim;
            //tmp[index] = ( (DWORD)((float)index / (float)(texDim * texDim)) ) * 0xFFFFFFFF;
            val = 0xFFFFFFFF & (~(i << 0) & ~(j << 8));
            tmp[index] = val;
        }
    }
    createTexture(tmp, texDim, texDim, sizeof(DWORD), DXGI_FORMAT_R8G8B8A8_UNORM, pSRVHeap, 1);

    for (int i = 0; i < texDim; i++)
    {
        for (int j = 0; j < texDim; j++)
        {
            int index = j + i * texDim;
            //tmp[index] = ( (DWORD)((float)index / (float)(texDim * texDim)) ) * 0xFFFFFFFF;
            val = 0xFFFFFFFF & (~(i << 16) & ~(j << 0));
            tmp[index] = val;
        }
    }
    createTexture(tmp, texDim, texDim, sizeof(DWORD), DXGI_FORMAT_R8G8B8A8_UNORM, pSRVHeap, 2);



    for (int i = 0; i < texDim; i++)
    {
        for (int j = 0; j < texDim; j++)
        {
            int index = j + i * texDim;
            val = (i & 0x40) ^ (j & 0x40) ? 0xFFFFFFFF : 0xFF000000;
            //val = 0xFF000000 ( ( ( i&0x40 ? 0xFF : 0x00)  << 16) & ( (j & 0x40 ? 0xFF : 0x00) << 8) );
            tmp[index] = val;
        }
    }
    createTexture(tmp, texDim, texDim, sizeof(DWORD), DXGI_FORMAT_R8G8B8A8_UNORM, pSRVHeap, 3);

    for (int i = 0; i < texDim; i++)
    {
        for (int j = 0; j < texDim; j++)
        {
            int index = j + i * texDim;
            val = (i & 0x20) ^ (j & 0x20) ? (0xFFFFFFFF & (~(i << 8) & ~(j << 0))) : (0xFFFFFFFF & (~(i << 8) & ~(j << 16)));
            //val = (i & 0x50) ^ (j & 0x50) ? 0xFF000000 : 0xFFFFFFFF;

            tmp[index] = val;
        }
    }
    createTexture(tmp, texDim, texDim, sizeof(DWORD), DXGI_FORMAT_R8G8B8A8_UNORM, pSRVHeap, 4);


    for (int i = 0; i < texDim; i++)
    {
        for (int j = 0; j < texDim; j++)
        {
            int index = j + i * texDim;
            
            val = 0xFFFFFFFF & (~(i << 0) & ~(j << 8) & ~(i << 8) & ~(j << 8));

            tmp[index] = val;
        }
    }
    createTexture(tmp, texDim, texDim, sizeof(DWORD), DXGI_FORMAT_R8G8B8A8_UNORM, pSRVHeap, 5);

    //create spiral
    memset( tmp, 0xFFFFFFFF, texDim * texDim * sizeof(DWORD));
    const float o = 3.1415f * 2 * 10;
    float astep = 0.01f;
    float rstep = 0.02f; //прирост радиуса
    float r = 0;
   
    for (float a = 0; a < o; a+= astep )
    {
        int x = (int)( texDim / 2 + cosf(a) * r );
        int y = (int)( texDim / 2 + sinf(a) * r );
        r += rstep;
        if ( (x < texDim) && (x >= 0) && (y < texDim) && (y >= 0))
        {
            tmp[y * texDim + x] = 0;
        }
    }
    createTexture(tmp, texDim, texDim, sizeof(DWORD), DXGI_FORMAT_R8G8B8A8_UNORM, pSRVHeap, 6);

    //create waves
    memset(tmp, 0xFFFFFFFF, texDim * texDim * sizeof(DWORD));
    const float sinScale = 5;
    const float cosScale = 5;

    for (int i = 0; i < texDim; i++)
    {
        for (int j = 0; j < texDim; j++)
        {
            int index = j + i * texDim;

            unsigned char s = (int)( sinf( ( (float)i / (float)texDim ) * sinScale) + 1.f ) * 127.5f;
            unsigned char c = (int) ( cosf( ( (float)j / (float)texDim ) * cosScale) + 1.f ) * 127.5f;

            val =  ( (UINT)s ) << 8;

            tmp[index] = val;
        }
    }
    createTexture(tmp, texDim, texDim, sizeof(DWORD), DXGI_FORMAT_R8G8B8A8_UNORM, pSRVHeap, 7);


    // Wait for the command list to execute; we are reusing the same command 
    // list in our main loop but for now, we just want to wait for setup to 
    // complete before continuing.
    if (!endCommandList())
        exit(-1);
    WaitForPreviousFrame();
    
}

void init()
{

	wnd_create( "DX12 Framework", width, height );
	wnd_setFrameMoveCallBack(frame_move);
	wnd_setFrameRenderCallBack(frame_render);
    wnd_setCleanUpCallBack(cleanUp);

	input = new gInput(hwnd);
	input->init();
	input->reset();

	initDX12();
    initAssets();

    ComPtr<ID3D12DescriptorHeap> p;

    wnd_show();
    wnd_update();
}

int main()
{
	try
	{
		init();

		MSG msg = { 0 };
		while (true)
		{
			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
				{
					cleanUp();
					return 0;
				};

				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			if (frame_move())
				frame_render();
		}
	}
	catch (const char* msg)
	{
		wnd_hide();
		MessageBox(0, msg, "DX12 Framework ",
			MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
	}
	cleanUp();

	return 0;
}