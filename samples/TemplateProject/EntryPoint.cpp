#include "TemplateProject.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    TemplateProject sample("TemplateProject", DX12Framework::DX12FRAMEBUFFERING::DX12FB_TRIPLE);
    DX12Framework::run(&sample);

	return 0;
}