#pragma once

#ifndef _RING_UPLOAD_BURRER_H_
#define _RING_UPLOAD_BUFFER_H_

#include <d3d12.h>
//#include <d3d12sdklayers.h>
#include "d3dx12.h"
//#include <dxgi1_6.h>
//#include <d3dcompiler.h>
//#include <DirectXMath.h>

#include <windows.h>
#include <wrl.h>
#include <queue>

//using namespace DirectX;
using Microsoft::WRL::ComPtr;

#ifdef _WIN64
typedef  UINT64 ADDRESS;
#else
typedef UINT32 ADDRESS;
#endif

struct RingUploadResourceBufferOffset
{
	RingUploadResourceBufferOffset(UINT _frameId, ADDRESS _offset, ADDRESS _size )
	{
		frameId = _frameId; resourceStartOffset = _offset; resourceEndOffset = _offset + _size - 1;
	};

	UINT64 size() { return resourceEndOffset - resourceStartOffset + 1; }
	UINT frameId;
	UINT64 resourceStartOffset;
	UINT64 resourceEndOffset;
};

class gRingUploadBuffer
{
public:
	gRingUploadBuffer(ComPtr<ID3D12Device> cpDevice);
	~gRingUploadBuffer();

	bool initialize( ADDRESS uploadBufferSize );
	void* allocate( UINT frameId, ADDRESS size, ADDRESS align, ADDRESS* pOutOffset = nullptr );
	UINT64 getAvailableAllocationSize(ADDRESS align) const;

	void frameEnded( UINT frameId ); // free allocations linked with frame id and previous frames id
	void clearQueue();

	ID3D12Resource* getResource() const;

	size_t getQueueSize() const;

	//operator ID3D12Resource* () const { return m_cpUploadBuffer.Get(); };

protected:
	gRingUploadBuffer() {};
	gRingUploadBuffer(const gRingUploadBuffer&) {};
	ComPtr<ID3D12Device> m_cpD3DDevice;
	ComPtr<ID3D12Resource> m_cpUploadBuffer;
	UINT64 m_uploadBufferSize;
	UINT64 m_currentBufferPos;
	void* m_lpMappedData;
	std::queue<RingUploadResourceBufferOffset> m_uploadsQueue;
};

#endif

