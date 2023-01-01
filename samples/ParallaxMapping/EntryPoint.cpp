#include "ParallaxMapping.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    ParallaxMappingSample sample("ParallaxMappingSample", DX12Framework::DX12FRAMEBUFFERING::DX12FB_TRIPLE);
    DX12Framework::run(&sample);

	return 0;
}