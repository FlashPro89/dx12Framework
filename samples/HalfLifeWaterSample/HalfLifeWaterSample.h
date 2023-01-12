#pragma once

#ifndef _TILED_RESOURCE_SAMPLE_H_
#define _TILED_RESOURCE_SAMPLE_H_

#include "DX12Framework.h"

class HalfLifeWaterSample :
    public DX12Framework
{
public:
	HalfLifeWaterSample(std::string name, DX12FRAMEBUFFERING buffering =
		DX12FRAMEBUFFERING::DX12FB_DOUBLE, bool useWARP = false);
	~HalfLifeWaterSample();

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

	struct WaterCoefs
	{
		float time = {};
		float amplitude = 0.1f;
		float frequency = 2.5f;
		float speed = 150.f;
		float phase = 0.f;
	}water_coefs;
};

#endif