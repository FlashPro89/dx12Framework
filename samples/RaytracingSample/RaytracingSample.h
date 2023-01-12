#pragma once

#ifndef _TILED_RESOURCE_SAMPLE_H_
#define _TILED_RESOURCE_SAMPLE_H_

#include "DX12Framework.h"

class RaytracingSample :
    public DX12Framework
{
public:
	RaytracingSample(std::string name, DX12FRAMEBUFFERING buffering =
		DX12FRAMEBUFFERING::DX12FB_DOUBLE, bool useWARP = false);
	~RaytracingSample();

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

	void initRaytracingResources();
	void createRTRootSignatures();
	void BuildShaderTables();
	void UpdateCameraMatrices();

	ComPtr<ID3D12Resource> m_raytracingOutput;
	ComPtr<ID3D12Resource> m_bottomLevelAccelerationStructure;
	ComPtr<ID3D12Resource> m_topLevelAccelerationStructure;
	ComPtr<ID3D12Resource> instanceDescsBuff;
	ComPtr<ID3D12Resource> scratchResource;

	ComPtr<ID3D12Resource> m_missShaderTable;
	ComPtr<ID3D12Resource> m_hitGroupShaderTable;
	ComPtr<ID3D12Resource> m_rayGenShaderTable;

	struct RaytracingSampleConstants
	{
		XMMATRIX projectionToWorld;
		XMVECTOR cameraPosition;
		XMVECTOR viewDirection;
	}m_RTC[4];

	D3D12_VERTEX_BUFFER_VIEW m_vb;
	D3D12_INDEX_BUFFER_VIEW m_ib;
	ComPtr<ID3D12Resource> m_cpVB;
	ComPtr<ID3D12Resource> m_cpIB;
	ComPtr<ID3D12Resource> m_cpTexture;
	ComPtr<ID3D12Resource> m_cpNormalMap;
};

#endif