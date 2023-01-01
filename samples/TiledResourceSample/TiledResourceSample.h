#pragma once

#ifndef _TILED_RESOURCE_SAMPLE_H_
#define _TILED_RESOURCE_SAMPLE_H_

#include "DX12Framework.h"

class TiledResourceSample :
    public DX12Framework
{
public:
	TiledResourceSample(std::string name, DX12FRAMEBUFFERING buffering =
		DX12FRAMEBUFFERING::DX12FB_DOUBLE, bool useWARP = false);
	~TiledResourceSample();

	void fillReservedTextureFromBuffer();
	void copyTile( UINT tx, UINT ty, UINT tz, UINT subresource, ADDRESS uploadOffset );

	bool initialize();

	bool beginCommandList();
	bool populateCommandList();
	bool endCommandList();
	void executeCommandList();
	bool executeCommandListAndPresent();

	bool update();
	bool render();
protected:
	bool createRootSignatureAndPSO();
	void createSRVTex2D(ID3D12Resource* pResourse, UINT heapOffsetInDescriptors);
	bool createReservedResource();

	D3D12_VERTEX_BUFFER_VIEW m_vb;
	D3D12_INDEX_BUFFER_VIEW m_ib;
	ComPtr<ID3D12Resource> m_cpVB;
	ComPtr<ID3D12Resource> m_cpIB;
	ComPtr<ID3D12Resource> m_cpTexture;
	ComPtr<ID3D12Resource> m_cpNormalMap;
	ComPtr<ID3D12Resource> m_cpReservedResource;
	ComPtr<ID3D12Heap> m_cpReservedHeap;
	ComPtr<ID3D12RootSignature> m_tiledRootSignature;
	std::unique_ptr<unsigned char[]> m_textureBuffer;
	
	XMFLOAT2 m_cursourPosition;
	bool m_left{ false };
	bool m_up{ false };
};

#endif