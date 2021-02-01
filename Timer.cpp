#include "Timer.h"

gTimer::gTimer()
{
	QueryPerformanceFrequency( &m_freq );
	QueryPerformanceCounter( &m_last );
}

gTimer::~gTimer()
{

}

float gTimer::getDelta()
{
	LARGE_INTEGER current;
	QueryPerformanceCounter( &current );

	float delta = ( (float)current.QuadPart - (float)m_last.QuadPart ) / (float)m_freq.QuadPart;
	m_last = current;
	return delta;
}

void  gTimer::reset()
{
	QueryPerformanceCounter(&m_last);
}
