#include "DX12Framework.h"
#include <codecvt>
#include <thread>

#pragma comment( lib , "d3d12.lib" )
#pragma comment( lib , "d3dcompiler.lib" )
#pragma comment( lib , "dxguid.lib" )
#pragma comment( lib , "dxgi.lib" )

// WndProc
LRESULT WINAPI _wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ------------------------------------
//
//		*** class DX12Framework::DX12Window ***
//
// ------------------------------------

DX12Framework::DX12Window::DX12Window()
{
}

DX12Framework::DX12Window::~DX12Window()
{
    if (m_parameters.handle)
        DestroyWindow((HWND)m_parameters.handle);
    UnregisterClass(L"DX12_FRAMEWORK_WND_CLS", GetModuleHandle(0));
}

void DX12Framework::DX12Window::showWindow(bool show)
{
    ShowWindow((HWND)m_parameters.handle, show ? SW_SHOW : SW_HIDE);
    updateWindow();
}

void DX12Framework::DX12Window::setWindowParameters( const DX12WINDOWPARAMS& parameters )
{
    m_parameters = parameters;
    RECT rect;
    adjustRect(rect);

    HWND hWnd = m_parameters.handle;

    if (!parameters.fullscreen)
    {
        SetWindowPos(hWnd, 0, m_parameters.x, m_parameters.y, 
            rect.right - rect.left, rect.bottom - rect.top, 0);
    }
    else
    {   // ignore x, y window coord in fullscreen
        SetWindowPos(hWnd, 0, 0, 0, rect.right, rect.bottom, SWP_NOZORDER);
    }

    SetWindowText(hWnd, m_parameters.name.c_str());
}

const DX12Framework::DX12WINDOWPARAMS& DX12Framework::DX12Window::getWindowParameters() const
{
    return this->m_parameters;
}

bool DX12Framework::DX12Window::updateWindow()
{
    return UpdateWindow(static_cast<HWND>(m_parameters.handle));
}

void DX12Framework::DX12Window::setTitle( std::wstring title )
{
    if(m_parameters.handle != 0)
        SetWindowText(m_parameters.handle, title.c_str());
}

bool DX12Framework::DX12Window::createWindow(const DX12WINDOWPARAMS& parameters)
{
    m_parameters = parameters;

    HWND hWnd = 0;

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, _wndProc, 0L, 0L,
                  GetModuleHandle(NULL), NULL, LoadCursor(NULL, IDC_ARROW), NULL, NULL,
                  L"DX12_FRAMEWORK_WND_CLS", NULL };

    ASSERT(RegisterClassEx(&wc), "Ошибка при регистрации класса окна!");

    RECT rect;
    adjustRect(rect);

    hWnd = CreateWindowEx(0, L"DX12_FRAMEWORK_WND_CLS", m_parameters.name.c_str(), WINDOW_STYLE,
        0, 0, rect.right, rect.bottom, 0, 0, 0, 0);

    ASSERT( hWnd !=0, "Ошибка при создании окна!" );

    m_parameters.handle = hWnd;

    return true;
}

void DX12Framework::DX12Window::adjustRect(RECT& _rect)
{
    DWORD wStyle = WINDOW_STYLE;
    RECT rect;
    rect.left = m_parameters.x;
    rect.top = m_parameters.y;
    rect.right = m_parameters.width + m_parameters.x;
    rect.bottom = m_parameters.height + m_parameters.y;
    AdjustWindowRect(&rect, wStyle, false);

    _rect = rect;
}

// ------------------------------------
//
//		*** class DX12Framework ***
//
 

DX12Framework::DX12Framework( std::wstring name, DX12FRAMEBUFFERING buffering, 
    bool useWARP ) :
	m_name(name), m_useWARPDevice(useWARP),
    m_frameIndex(0)
{
    switch (buffering)
    {
    case DX12FRAMEBUFFERING::DX12FB_QUADRUPLE:
        m_framesCount = 4;
    case DX12FRAMEBUFFERING::DX12FB_TRIPLE:
        m_framesCount = 3;
    case DX12FRAMEBUFFERING::DX12FB_DOUBLE:
    default:
        m_framesCount = 2;
    }
    
    m_spTimer = std::make_shared<gTimer>();
}

bool DX12Framework::run( DX12Framework* sample )
{
    if (!sample)
        return false;

    if (!sample->initialize())
        return false;

#define MT

#ifdef MT
    bool run = false;
    bool renderEnded = false;

    auto renderingLoop
    {
        [&]()
        {
            while (run != false)
            {
                if (sample->update())
                    if (!sample->render())
                        PostQuitMessage(-7);
            }
            renderEnded = true;
        }
    };
    run = true;
    std::thread renderThread(renderingLoop);

    renderThread.detach();

#endif

    MSG msg = { 0 };
    while (true)
    {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
#ifdef MT
                run = false;
                while (renderEnded != true) //wait while render ended
                    Sleep(100);
#endif
                return 0;
            };

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

#ifndef MT
        if (sample->update())
            if (!sample->render())
                PostQuitMessage(-7);
#endif

    }
}

bool DX12Framework::initDefault()
{
    //-------------------------------------------------------------
    // Create window
    //-------------------------------------------------------------
    if (!m_spWindow)
    {
        m_spWindow = std::make_shared<DX12Window>();
        ASSERT(m_spWindow->createWindow(DX12WINDOWPARAMS(m_name, 800, 600)),
            L"Failed create window!");
    }

    //-------------------------------------------------------------
    // Use debug layer if needeed
    //-------------------------------------------------------------
    UINT dxgiFactoryFlags = 0; createDebugLayerIfNeeded();

    //-------------------------------------------------------------
    // Create DXGI factory
    //-------------------------------------------------------------
    ComPtr < IDXGIFactory4 > factory;
    DXASSERT(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),
        "Cannot create DXGI Factory1!");
    DXASSERT(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)),
        "Cannot create DXGI Factory2!");

    //-------------------------------------------------------------
    // Create d3d12 device
    //-------------------------------------------------------------
    ASSERT(createDevice(factory), "Cannot create D3D12 device!");

    //-------------------------------------------------------------
    // Create command queues
    //-------------------------------------------------------------
    ASSERT(createDefaultCommandQueue(), "Cannot create D3D12 default command queue!");

    //-------------------------------------------------------------
    // Create default swap chain
    //-------------------------------------------------------------
    ASSERT(createSwapChain(factory), "Cannot create D3D12 swap chain!");

    //-------------------------------------------------------------
    // Create descriptor heaps
    //-------------------------------------------------------------
    ASSERT(createDescriptorHeaps(), "Cannot create D3D12 descriptor heaps!");

    //-------------------------------------------------------------
    // Create frame resources
    //-------------------------------------------------------------
    ASSERT(createFrameResources(), "Cannot create D3D12 frame resources!");

    //-------------------------------------------------------------
    // Create default assets
    //-------------------------------------------------------------
    ASSERT(createDefaultRootSignature(), "Cannot create D3D12 default root signature!");
    ASSERT(createDefaultPipelineState(), "Cannot create D3D12 default pipeline state!");

    //-------------------------------------------------------------
    // Create command lists
    //-------------------------------------------------------------
    ASSERT(createDefaultCommandList(), "Cannot create D3D12 default command list!");

    //-------------------------------------------------------------
    // Create fence
    //-------------------------------------------------------------
    ASSERT(createFence(), "Cannot create D3D12 fence!");

    //-------------------------------------------------------------
    // Create Ring Upload Buffer
    //-------------------------------------------------------------
    m_spRingBuffer = std::make_shared<gRingUploadBuffer>(m_cpD3DDev);

    // Setup vieport & scissor rect
    {
        UINT width = m_spWindow->getWindowParameters().width;
        UINT height = m_spWindow->getWindowParameters().height;

        m_viewport.TopLeftX = m_viewport.TopLeftY = 0;
        m_viewport.Width = static_cast<float>(width);
        m_viewport.Height = static_cast<float>(height);
        m_viewport.MaxDepth = 1.f;
        m_viewport.MinDepth = 0.f;

        m_scissorRect.left = m_scissorRect.top = 0;
        m_scissorRect.bottom = height;
        m_scissorRect.right = width;
    }

    // Set Window Title:
    //using convert_typeX = std::codecvt_utf8<wchar_t>;
    //std::wstring_convert<convert_typeX, wchar_t> converterX;

    std::wstring sAdapter = (m_adapterDesc.Description);
    std::wstring sTitle = m_name.c_str() + std::wstring(L" ( ") +
        sAdapter + std::wstring(L" )");
    m_spWindow->setTitle(sTitle);

    return true;
}

void DX12Framework::GetHardwareAdapter(IDXGIFactory1* pFactory,
	IDXGIAdapter1** ppAdapter,
	bool requestHighPerformanceAdapter)
{
    *ppAdapter = nullptr;

    ComPtr<IDXGIAdapter1> adapter;

    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    //if(false)
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

            break;

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            /*
            ID3D12Device* pDevice = 0;
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice))))
            {
                pDevice->Release();
                break;
            }
            */
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

            break;

            // Check to see whether the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            /*
            ID3D12Device* pDevice = 0;
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice))))
            {
                break;
            }
            */
        }
    }

    *ppAdapter = adapter.Detach();
}

UINT DX12Framework::createDebugLayerIfNeeded()
{
#if defined(_DEBUG)
    // Enable the D3D12 debug layer.
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
    }

    return DXGI_CREATE_FACTORY_DEBUG;
#endif
    return 0;
}

bool DX12Framework::createDefaultCommandQueue()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    return SUCCEEDED(m_cpD3DDev->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_cpCommQueue)));
}

bool DX12Framework::createDevice(ComPtr<IDXGIFactory4> factory)
{


    // Select Hardware or WARP adapter type
    if (m_useWARPDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter))))
            return false;

        if (FAILED(D3D12CreateDevice(warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_cpD3DDev))))
            return false;
        
        warpAdapter->GetDesc(&m_adapterDesc);

    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter, true);

        if (FAILED(D3D12CreateDevice(hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_cpD3DDev))))
            return false;
        hardwareAdapter->GetDesc(&m_adapterDesc);
    }
    return true;
}

bool DX12Framework::createSwapChain(ComPtr<IDXGIFactory4> factory)
{
    // Get window parameters
    auto wParams = m_spWindow->getWindowParameters();
    HWND hWnd = wParams.handle;

    m_viewport.TopLeftX = m_viewport.TopLeftY = 0;
    m_viewport.Width = wParams.width;
    m_viewport.Height = wParams.height;

    m_scissorRect.left = m_scissorRect.top = 0;
    m_scissorRect.bottom = wParams.width;
    m_scissorRect.right = wParams.height;

    ComPtr<IDXGISwapChain> swapChain;
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = MAXFRAMESNUM;
    swapChainDesc.BufferDesc.Width = static_cast<UINT>(m_viewport.Width);
    swapChainDesc.BufferDesc.Height = static_cast<UINT>(m_viewport.Height);
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.OutputWindow = hWnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;

    if (FAILED(factory->CreateSwapChain(m_cpCommQueue.Get(), &swapChainDesc, &swapChain)))
        return false;

    // This realization does not support fullscreen transitions.
    if (FAILED(factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER)))
        return false;

    swapChain.As(&m_cpSwapChain);
    m_frameIndex = m_cpSwapChain->GetCurrentBackBufferIndex();

    return true;
}

bool DX12Framework::createDescriptorHeaps()
{
    //-------------------------------------------------------------
    // Describe and create a render target view (RTV) descriptor heap.
    //-------------------------------------------------------------
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = MAXFRAMESNUM;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(m_cpD3DDev->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_cpRTVHeap))))
        return false;
    m_rtvDescriptorSize = m_cpD3DDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    //-------------------------------------------------------------
    // Describe and create a depth stencil view (DSV) descriptor heap.
    //-------------------------------------------------------------
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(m_cpD3DDev->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_cpDSVHeap))))
        return false;
    m_dsvDescriptorSize = m_cpD3DDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    //-------------------------------------------------------------
    // Describe and create a shader resource view (SRV) heap for the texture.
    //-------------------------------------------------------------
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    UINT maxDescriptorsNum = MAXTEXTURESNUM + MAXCONSTANTBUFFERSNUM +
        MAXVERTEXBUFFERSNUM + MAXINDEXBUFFERSNUM;
    srvHeapDesc.NumDescriptors = maxDescriptorsNum;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_cpD3DDev->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_cpSRVHeap))))
        return false;
    m_srvDescriptorSize = m_cpD3DDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    return true;
}

bool DX12Framework::createFrameResources()
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_cpRTVHeap->GetCPUDescriptorHandleForHeapStart());
    
    //-------------------------------------------------------------
    // Create a RTV for each frame.
    //-------------------------------------------------------------
    for (UINT n = 0; n < MAXFRAMESNUM; n++)
    {
        if (FAILED(m_cpSwapChain->GetBuffer(n, IID_PPV_ARGS(&m_cpRenderTargets[n]))))
            return false;

        m_cpD3DDev->CreateRenderTargetView(m_cpRenderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescriptorSize);

        std::wstring name = L"m_cpRenderTargets[" + std::to_wstring(n) + L"]";
        m_cpRenderTargets[n].Get()->SetName((name.c_str()));
    }

    //-------------------------------------------------------------
    // Create a DSV
    //-------------------------------------------------------------
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

        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT,
            static_cast<UINT>(m_viewport.Width), static_cast<UINT>(m_viewport.Height),
            1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_cpDSVHeap->GetCPUDescriptorHandleForHeapStart());

        if (FAILED(m_cpD3DDev->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthOptimizedClearValue,
            IID_PPV_ARGS(&m_cpDepthStencil))))
            return false;

        //NAME_D3D12_OBJECT(pDepthStencil);
        m_cpDepthStencil.Get()->SetName(L"m_cpDepthStencil");
        m_cpD3DDev->CreateDepthStencilView(m_cpDepthStencil.Get(),
            &depthStencilDesc, dsvHandle);
    }
    return true;
}

bool DX12Framework::createDefaultRootSignature()
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(m_cpD3DDev->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

    CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

    CD3DX12_ROOT_PARAMETER1 rootParameters[2];
    rootParameters[1].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);

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

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    if (FAILED(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error)))
        return false;

    if (FAILED(m_cpD3DDev->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(), IID_PPV_ARGS(&m_cpRootSignature[0]))))
        return false;

    return true;
}

bool DX12Framework::createDefaultPipelineState()
{
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
        "    float2 texCoord : TEXCOORD;     "
      "  float4 color : COLOR;              "
        "};   "

        "struct VS_OUTPUT                       "
        "{                                      "
        "   float4 pos : SV_POSITION;           "
        "    float2 texCoord : TEXCOORD;        "
      "    float4 color : COLOR;              "
        "};                                     "


        "VS_OUTPUT main(VS_INPUT input)         "
        "{                                      "
        "   VS_OUTPUT output;                   "
        "   output.pos = input.pos;             "
        "   output.texCoord = input.texCoord;  "
        "   output.color = input.color;        "
        "   return output;                     "
        "}";

    const char ps_source[] =
        "struct PSInput                         "
        "{                                      "
        "    float4 position : SV_POSITION;     "
        "    float2 uv : TEXCOORD;              "
      "    float4 color : COLOR;              "
        "};                                     "

        "Texture2D g_texture : register(t0);       "
        "SamplerState g_sampler : register(s0);     "

        "float4 main(PSInput input) : SV_TARGET     "
        "{                                          "
        "    return g_texture.Sample(g_sampler, input.uv); "
        "} ";


    ID3DBlob* errorBlob;

    if (FAILED(D3DCompile(vs_source, sizeof(vs_source), "vs_default", nullptr,
        nullptr, "main", "vs_5_0", compileFlags, 0, &vertexShader, &errorBlob)))
        return false;

    if (FAILED(D3DCompile(ps_source, sizeof(ps_source), "ps_default", nullptr,
        nullptr, "main", "ps_5_0", compileFlags, 0, &pixelShader, &errorBlob)))
        return false;

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
    psoDesc.pRootSignature = m_cpRootSignature[0].Get();
    psoDesc.VS = { reinterpret_cast<UINT8*>(vertexShader->GetBufferPointer()), vertexShader->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<UINT8*>(pixelShader->GetBufferPointer()), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    if (FAILED(m_cpD3DDev->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_cpPipelineState[0]))))
        return false;

    return true;
}

bool DX12Framework::createDefaultCommandList()
{
    if (FAILED(m_cpD3DDev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cpCommAllocator))))
        return false;

    return (SUCCEEDED(m_cpD3DDev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_cpCommAllocator.Get(), m_cpPipelineState[0].Get(), IID_PPV_ARGS(&m_cpCommList))));
}

bool DX12Framework::createFence()
{
    if (FAILED(m_cpD3DDev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_cpFence))))
        return false;

    for(UINT i = 0; i < m_framesCount;i++)
        m_fenceValues[i] = 1;

    // Create an event handle to use for frame synchronization.
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        if (FAILED(HRESULT_FROM_WIN32(GetLastError())))
            return false;
    }
    return true;
}

bool DX12Framework::initRingBuffer(size_t size)
{
    // Create ring upload buffer
    m_spRingBuffer = std::make_shared<gRingUploadBuffer>(m_cpD3DDev);
    return m_spRingBuffer->initialize(size);
}

bool DX12Framework::initInput()
{
    ASSERT(m_spWindow, "Window not created before DInput init!");

    HWND hWnd = m_spWindow->getWindowParameters().handle;
    ASSERT(hWnd, "Window not created!");

    m_spInput = std::make_shared<gInput>(hWnd);
    m_spInput->init();

    return true;
}

//todo: delete this:
void DX12Framework::WaitForPreviousFrame()
{
    HRESULT hr;
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. More advanced samples 
    // illustrate how to use fences for efficient resource usage.

    // Signal and increment the fence value.
    const UINT64 fence = m_fenceValues[0];
    m_cpCommQueue->Signal(m_cpFence.Get(), fence);
    m_fenceValues[0]++;

    // Wait until the previous frame is finished.
    if (m_cpFence->GetCompletedValue() < fence)
    {
        hr = m_cpFence->SetEventOnCompletion(fence, m_fenceEvent);
        if (FAILED(hr))
            throw("Fence->SetEventOnCompletion() failed!");
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    m_spRingBuffer->frameEnded(m_frameIndex);
    m_frameIndex = m_cpSwapChain->GetCurrentBackBufferIndex();
}

// Wait for pending GPU work to complete.
void DX12Framework::WaitForGpu()
{
    // Schedule a Signal command in the queue.
    DXASSERT(m_cpCommQueue->Signal(m_cpFence.Get(), m_fenceValues[m_frameIndex]),
        "Cannot Fence->Signal()!");

    // Wait until the fence has been processed.
    DXASSERT(m_cpFence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent),
        "Cannot Fence->SetEventNotification()!");
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

    // Increment the fence value for the current frame.
    m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void DX12Framework::MoveToNextFrame()
{
    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    DXASSERT(m_cpCommQueue->Signal(m_cpFence.Get(), currentFenceValue),
        "Cannot Fence->Signal()!");

    // Update the frame index.
    m_frameIndex = m_cpSwapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_cpFence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        DXASSERT(m_cpFence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent),
            "Cannot Fence->SetEventNotification()!");
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}

bool DX12Framework::uploadSubresources( ID3D12Resource* pResource, UINT subResNum,
    const D3D12_SUBRESOURCE_DATA* srDataArray )
{
    if (subResNum == 0)
        return false;

    const ADDRESS uploadBufferSize = GetRequiredIntermediateSize(pResource, 0, subResNum);

    auto desc = pResource->GetDesc();
    auto aligment = desc.Alignment;
    auto old = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT; //??

    //upload To RingBuffer
    ADDRESS uploadOffset;
    void* lpUploadPtr = m_spRingBuffer->allocate(0, uploadBufferSize,
        aligment, &uploadOffset);
/*
    if (!lpUploadPtr) // закончилась очередь загрузки
    {
        if (!endCommandList())
            exit(-1);
        WaitForPreviousFrame();
        if (!beginCommandList())
            exit(-1);

        spRingBuffer->clearQueue();
        lpUploadPtr = spRingBuffer->allocate(0, uploadBufferSize,
            aligment, &uploadOffset);

        if (!lpUploadPtr)
            exit(-1);
    }
    */

    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        pResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    UpdateSubresources(m_cpCommList.Get(), pResource, m_spRingBuffer->getResource(),
        uploadOffset, 0, subResNum, srDataArray);
    m_cpCommList->ResourceBarrier(1, &barrier);

    return true;
}

void DX12Framework::finalize()
{
    if (m_fenceEvent)
    {
        WaitForGpu();
        CloseHandle(m_fenceEvent);
        m_fenceEvent = 0;
    }
}
