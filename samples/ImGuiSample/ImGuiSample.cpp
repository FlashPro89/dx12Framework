#include "ImGuiSample.h"
#include <string>
#include "imgui/imgui.h"
#include "imgui_node_editor/imgui_node_editor.h"
// ------------------------------------
//
//		*** class ImGuiSample ***
//
// ------------------------------------

namespace ed = ax::NodeEditor;
namespace
{
    struct LinkInfo
    {
        ed::LinkId Id;
        ed::PinId  InputId;
        ed::PinId  OutputId;
    };
    ImVector<LinkInfo>   m_Links;                // List of live links. It is dynamic unless you want to create read-only view over nodes.
    int                  m_NextLinkId = 100;     // Counter to help generate link ids. In real application this will probably based on pointer to user data structure.
    ax::NodeEditor::EditorContext *m_context = nullptr;
    bool firstframe = true; // Used to position the nodes on startup
}

ImGuiSample::ImGuiSample(std::string name,
    DX12Framework::DX12FRAMEBUFFERING buffering, bool useWARP) :
    DX12Framework(name, buffering, useWARP)
{
}

ImGuiSample::~ImGuiSample()
{ 
}

bool ImGuiSample::initialize()
{
	if (!initDefault())
		return false;
    if (!initImGui())
        return false;

    ed::Config config;
    config.SettingsFile = "Simple.json";
    m_context = ed::CreateEditor(&config);

    endCommandList();
    executeCommandList();
    WaitForGpu();

    m_spWindow->showWindow(true);

	return true;
}

void ImGuiSample::finalize()
{
    ed::DestroyEditor(m_context);
    DX12Framework::finalize();
}

bool ImGuiSample::beginCommandList()
{
    if (FAILED(m_cpCommAllocator->Reset()))
        return false;

    if (FAILED(m_cpCommList->Reset(m_cpCommAllocator.Get(), m_cpPipelineState[0].Get())))
        return false;

    return true;
}

bool ImGuiSample::populateCommandList()
{
    // Set necessary state.
    m_cpCommList->SetGraphicsRootSignature(m_cpRootSignature[0].Get());

    ID3D12DescriptorHeap *ppHeaps[] = { m_cpSRVHeap.Get() };
    m_cpCommList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    CD3DX12_GPU_DESCRIPTOR_HANDLE h(m_cpSRVHeap->GetGPUDescriptorHandleForHeapStart(),
        0, m_srvDescriptorSize);

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

    beginImGui();

    //sample_gui();
    my_sample_gui();


    endImGui();


    // Indicate that the back buffer will now be used to present.
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_cpRenderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_cpCommList->ResourceBarrier(1, &barrier);

    firstframe = false;

    return true;
}

void ImGuiSample::sample_gui()
{
    static bool show_nodes_wnd = true;
    if (show_nodes_wnd)
    {
        auto io = ImGui::GetIO();
        ImGui::Begin("Nodes editor", &show_nodes_wnd); 

        // Node Editor Widget
        ed::SetCurrentEditor(m_context);
        ed::Begin("My Editor", ImVec2(0.0, 0.0f));
        int uniqueId = 1;

        // Start drawing nodes.
        ed::BeginNode(uniqueId++);
        ImGui::Text("Node A");
        ed::BeginPin(uniqueId++, ed::PinKind::Input);
        ImGui::Text("-> In");
        ed::EndPin();
        ImGui::SameLine();
        ed::BeginPin(uniqueId++, ed::PinKind::Output);
        ImGui::Text("Out ->");
        ed::EndPin();
        ed::EndNode();

        auto header_id = uniqueId++;
        ed::BeginNode(header_id);
        ImGui::Text("Node B");

        // Pins Row
        ed::BeginPin(uniqueId++, ed::PinKind::Input);
        ImGui::Text("-> In");
        ed::EndPin();
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(35, 0)); //  magic number - Crude & simple way to nudge over the output pin. Consider using layout and springs
        ImGui::SameLine();
        ed::BeginPin(uniqueId++, ed::PinKind::Output);
        ImGui::Text("Out ->");
        ed::EndPin();
        ed::EndNode(); // End of Tree Node Demo

        if (firstframe)
            ed::SetNodePosition(header_id, ImVec2(0, 50));

        // ==================================================================================================
        // Link Drawing Section

        for (auto &linkInfo : m_Links)
            ed::Link(linkInfo.Id, linkInfo.InputId, linkInfo.OutputId);

        // ==================================================================================================
        // Interaction Handling Section
        // This was coppied from BasicInteration.cpp. See that file for commented code.

        // Handle creation action ---------------------------------------------------------------------------
        if (ed::BeginCreate())
        {
            ed::PinId inputPinId, outputPinId;
            if (ed::QueryNewLink(&inputPinId, &outputPinId))
            {
                if (inputPinId && outputPinId)
                {
                    if (ed::AcceptNewItem())
                    {
                        m_Links.push_back({ ed::LinkId(m_NextLinkId++), inputPinId, outputPinId });
                        ed::Link(m_Links.back().Id, m_Links.back().InputId, m_Links.back().OutputId);
                    }
                }
            }
        }
        ed::EndCreate();

        // Handle deletion action ---------------------------------------------------------------------------
        if (ed::BeginDelete())
        {
            ed::LinkId deletedLinkId;
            while (ed::QueryDeletedLink(&deletedLinkId))
            {
                if (ed::AcceptDeletedItem())
                {
                    for (auto &link : m_Links)
                    {
                        if (link.Id == deletedLinkId)
                        {
                            m_Links.erase(&link);
                            break;
                        }
                    }
                }
            }
        }
        ed::EndDelete();

        ed::End();
        ed::SetCurrentEditor(nullptr);

        ImGui::End();
    }
}
void ImGuiSample::my_sample_gui()
{
    auto io = ImGui::GetIO();
    static bool show_nodes_wnd = true;
    ImGui::Begin("Nodes editor", &show_nodes_wnd);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
    ImGui::Separator();

    // Node Editor Widget
    ed::SetCurrentEditor(m_context);
    ed::Begin("My Editor", ImVec2(0.0, 0.0f));


    ed::BeginNode(777);
    ImGui::Text("Node 777");
    ImGui::Dummy(ImVec2(25, 0)); // Hacky magic number to space out the output pin.
    ImGui::SameLine();
    ed::BeginPin(778, ed::PinKind::Output);
    ImGui::Text("Out ->");
    ed::EndPin();
    ed::EndNode();

    ed::BeginNode(888);
    ImGui::Text("Node 888");
    ed::BeginPin(889, ed::PinKind::Input);
    ImGui::Text("-> In");
    ed::EndPin();
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(25, 0)); // Hacky magic number to space out the output pin.
    ed::EndNode();
   

    char tmp_str[256] = {};
    for (int i = 1; i < 11; i++)
    {
        sprintf_s(tmp_str, 256, "Node %d", i-1);

        ed::BeginNode(i);
        ImGui::Text(tmp_str);
        ed::BeginPin(i + 10, ed::PinKind::Input);
        ImGui::Text("-> In");
        ed::EndPin();
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(25, 0)); // Hacky magic number to space out the output pin.
        ImGui::SameLine();
        ed::BeginPin(i + 20, ed::PinKind::Output);
        ImGui::Text("Out ->");
        ed::EndPin();
        ed::EndNode();
    }


    if (firstframe)
    {
        for (int i = 0; i < 10; i++)
            ed::SetNodePosition(i+1, ImVec2(i * 100, i * 100));

        LinkInfo link_info;
        link_info.Id = 69;
        link_info.InputId = 11;
        link_info.OutputId = 30;
        m_Links.push_back(link_info);

        for (int i = 1; i < 10; i++)
        {
            LinkInfo link_info;
            link_info.Id = i + 70;
            link_info.InputId = i + 11;
            link_info.OutputId = i + 20;
            m_Links.push_back(link_info);
        }
    }

    for (auto &linkInfo : m_Links)
        ed::Link(linkInfo.Id, linkInfo.InputId, linkInfo.OutputId);
    // Handle creation action ---------------------------------------------------------------------------
    if (ed::BeginCreate())
    {
        ed::PinId inputPinId, outputPinId;
        if (ed::QueryNewLink(&inputPinId, &outputPinId))
        {
            if (inputPinId && outputPinId)
            {
                if (ed::AcceptNewItem())
                {
                    m_Links.push_back({ ed::LinkId(m_NextLinkId++), inputPinId, outputPinId });
                    ed::Link(m_Links.back().Id, m_Links.back().InputId, m_Links.back().OutputId);
                }
            }
        }
    }
    ed::EndCreate();
    // Handle deletion action ---------------------------------------------------------------------------
    if (ed::BeginDelete())
    {
        ed::LinkId deletedLinkId;
        while (ed::QueryDeletedLink(&deletedLinkId))
        {
            if (ed::AcceptDeletedItem())
            {
                for (auto &link : m_Links)
                {
                    if (link.Id == deletedLinkId)
                    {
                        m_Links.erase(&link);
                        break;
                    }
                }
            }
        }

        //ed::NodeId deletedNodeId;
        //while (ed::QueryDeletedNode(&deletedNodeId))
        //{
        //    if (ed::AcceptDeletedItem())
        //    {
        //        // ...
        //    }
        //}
    }
    ed::EndDelete();
    ed::End();
    ed::SetCurrentEditor(nullptr);

    ImGui::End();
}

bool ImGuiSample::endCommandList()
{
    if (FAILED(m_cpCommList->Close()))
        return false;
    return true;
}

void ImGuiSample::executeCommandList()
{
    ID3D12CommandList* ppCommandLists[] = { m_cpCommList.Get() };
    m_cpCommQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}


bool ImGuiSample::executeCommandListAndPresent()
{
    
    executeCommandList();

    if (FAILED(m_cpSwapChain->Present(1, 0)))
        return false;

    return true;
}

bool ImGuiSample::update()
{
    if (m_spInput)
        m_spInput->update();

    if (m_spCamera)
        m_spCamera->tick(m_spTimer->getDelta());

	return true;
}

bool ImGuiSample::render()
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

bool ImGuiSample::createRootSignatureAndPSO()
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

    std::wstring w_fileName = L"../shaders/ImGuiSample.hlsl";

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
