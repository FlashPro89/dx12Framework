#pragma once

#ifndef _TILED_RESOURCE_SAMPLE_H_
#define _TILED_RESOURCE_SAMPLE_H_

#include "DX12Framework.h"

class ParallaxMappingSample :
    public DX12Framework
{
public:
	ParallaxMappingSample(std::string name, DX12FRAMEBUFFERING buffering =
		DX12FRAMEBUFFERING::DX12FB_DOUBLE, bool useWARP = false);
	~ParallaxMappingSample();

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

	D3D12_VERTEX_BUFFER_VIEW m_vb;
	D3D12_INDEX_BUFFER_VIEW m_ib;
	ComPtr<ID3D12Resource> m_cpVB;
	ComPtr<ID3D12Resource> m_cpIB;
	ComPtr<ID3D12Resource> m_cpTexture;
	ComPtr<ID3D12Resource> m_cpNormalMap;

	float m_lightRotRadius;
	float m_lightRotAngle;
	float m_lightRotAngle2{};
};

#endif