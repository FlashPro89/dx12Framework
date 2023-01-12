#pragma once

#ifndef _TILED_RESOURCE_SAMPLE_H_
#define _TILED_RESOURCE_SAMPLE_H_

#include "DX12Framework.h"

class ImGuiSample :
    public DX12Framework
{
public:
	ImGuiSample(std::string name, DX12FRAMEBUFFERING buffering =
		DX12FRAMEBUFFERING::DX12FB_DOUBLE, bool useWARP = false);
	~ImGuiSample();

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
};

#endif