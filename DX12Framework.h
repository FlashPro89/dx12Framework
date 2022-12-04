#pragma once

#ifndef _DX12_FRAMEWORK_H_
#define _DX12_FRAMEWORK_H_

#include <memory.h>
#include <d3d12sdklayers.h>
#include "d3dx12.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <windows.h>
#include <wrl.h>
#include "util.h"
#include "input.h"
#include "Timer.h"
#include "Camera.h"
#include "RingUploadBuffer.h"
#include "DDSTextureLoader12.h"

#define MAXFRAMESNUM 2
#define MAXTEXTURESNUM 4096
#define MAXTEXTUREUPLOADSNUM 4096
#define MAXVERTEXBUFFERSNUM 256
#define MAXINDEXBUFFERSNUM 2048
#define MAXCONSTANTBUFFERSNUM 2048
#define MAXPIPELINESTATESNUM 256
#define MAXROOTSIGNATURESNUM 256

using namespace DirectX;
using Microsoft::WRL::ComPtr;

#ifdef _DEBUG
#define DXASSERT(hr,msg) assert(SUCCEEDED(hr) && msg )
#define ASSERT(r,msg) assert( r && msg )
#else
#define ASSERT(r,msg) r
#define DXASSERT(hr) hr
#endif

#define WINDOW_STYLE ( WS_OVERLAPPED | WS_SYSMENU | WS_MINIMIZEBOX | WS_CAPTION )
#define MAXFRAMEBUFFERSCOUNT 4

class DX12Framework
{
public:
	enum class DX12FRAMEBUFFERING
	{
		DX12FB_DOUBLE,
		DX12FB_TRIPLE,
		DX12FB_QUADRUPLE
	};

	struct DX12WINDOWPARAMS
	{
		DX12WINDOWPARAMS() : name(""), width(0), height(0), 
			fullscreen(false), x(0), y(0),handle(0) {};
		DX12WINDOWPARAMS(std::string _name, unsigned short _width, unsigned short _height,
			bool _fullscreen = false, unsigned _x = 0, unsigned short _y = 0) :
			name(_name), width(_width), height(_height), 
			fullscreen(_fullscreen), x(_x), y(_y),handle(0) {}

		std::string name;
		unsigned short width;
		unsigned short height;
		unsigned short x;
		unsigned short y;
		bool fullscreen;
		HWND handle;
	};

protected:
	// ***  make friendly to class DX12Window;
	bool initDefault(); 	
public:
	// ***  make friendly to class DX12Window;
	virtual bool initialize() = 0;

	class DX12Window
	{
	public:
		DX12Window();
		~DX12Window();

		// window
		void showWindow(bool show);
		void setWindowParameters(const DX12WINDOWPARAMS& parameters);
		const DX12WINDOWPARAMS& getWindowParameters() const; // hWnd casted to pointer
		bool updateWindow();
		void setTitle(std::string title);

	protected:
		DX12Window(const DX12Window&) {};

		bool createWindow(const DX12WINDOWPARAMS& parameters);
		void adjustRect(RECT& rect);

		DX12WINDOWPARAMS m_parameters;

		friend 	bool DX12Framework::initialize();
		friend 	bool DX12Framework::initDefault();

	};

	DX12Framework(std::string name, DX12FRAMEBUFFERING buffering = 
		DX12FRAMEBUFFERING::DX12FB_DOUBLE, bool useWARP = false);
	virtual ~DX12Framework() { finalize(); }

	virtual bool update() = 0;
	virtual bool render() = 0;

	static bool run( DX12Framework* sample );

protected:
	std::string m_name;
	DXGI_ADAPTER_DESC m_adapterDesc;

	void GetHardwareAdapter(IDXGIFactory1* pFactory,
		IDXGIAdapter1** ppAdapter,
		bool requestHighPerformanceAdapter);
	UINT createDebugLayerIfNeeded();
	bool createDefaultCommandQueue();
	bool createDevice(ComPtr<IDXGIFactory4> factory);
	bool createSwapChain(ComPtr<IDXGIFactory4> factory);
	bool createDescriptorHeaps();
	bool createFrameResources();
	bool createDefaultRootSignature();
	bool createDefaultPipelineState();
	bool createDefaultCommandList();
	bool createFence();
	bool initRingBuffer(size_t size);
	bool initInput();

	void WaitForPreviousFrame();
	void WaitForGpu();
	void MoveToNextFrame();

	//upload to default mem pool
	bool uploadSubresources( ID3D12Resource* pResource, UINT subResNum,
		const D3D12_SUBRESOURCE_DATA* srDataArray );

	void finalize();

	bool m_useWARPDevice; //default : false

	D3D_DRIVER_TYPE                     m_driverType = D3D_DRIVER_TYPE_NULL;
	ComPtr< ID3D12Device >              m_cpD3DDev;
	ComPtr< ID3D12CommandQueue >        m_cpCommQueue;
	ComPtr< ID3D12CommandAllocator >    m_cpCommAllocator;
	ComPtr< ID3D12GraphicsCommandList > m_cpCommList;
	ComPtr< IDXGISwapChain3 >           m_cpSwapChain;
	ComPtr< ID3D12DescriptorHeap >      m_cpRTVHeap;
	ComPtr< ID3D12DescriptorHeap >      m_cpCBVSrvHeap;
	ComPtr< ID3D12DescriptorHeap >      m_cpDSVHeap;
	ComPtr< ID3D12DescriptorHeap >      m_cpSRVHeap;
	ComPtr< ID3D12RootSignature >       m_cpRootSignature[MAXROOTSIGNATURESNUM];
	ComPtr< ID3D12PipelineState >       m_cpPipelineState[MAXPIPELINESTATESNUM];
	ComPtr< ID3D12Resource >            m_cpRenderTargets[MAXFRAMESNUM];
	ComPtr< ID3D12Resource >            m_cpDepthStencil;
	ComPtr< ID3D12Resource >            m_cpTextures[MAXTEXTURESNUM];
	ComPtr< ID3D12Resource >            m_cpTextureUploads[MAXTEXTURESNUM];

	// App resources
	ComPtr<ID3D12Resource> m_cpVertexBuffers[MAXVERTEXBUFFERSNUM];
	D3D12_VERTEX_BUFFER_VIEW m_cpVertexBufferViews;
	ComPtr<ID3D12Resource> m_cpIndexBuffers[MAXINDEXBUFFERSNUM];
	D3D12_VERTEX_BUFFER_VIEW m_cpIndexBufferViews[MAXINDEXBUFFERSNUM];

	// Syncronization objects
	UINT m_framesCount;
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_cpFence;
	UINT64 m_fenceValues[MAXFRAMEBUFFERSCOUNT];

	// Rendering region
	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;

	// Descriptor heaps
	UINT m_rtvDescriptorSize;
	UINT m_dsvDescriptorSize;
	UINT m_srvDescriptorSize;

	//util objects
	std::shared_ptr<gCamera> m_spCamera;
	std::shared_ptr<gInput> m_spInput;
	std::shared_ptr<gTimer> m_spTimer;
	std::shared_ptr<DX12Window> m_spWindow;
	std::shared_ptr<gRingUploadBuffer> m_spRingBuffer;
};

#endif

