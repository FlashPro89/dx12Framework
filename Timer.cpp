#include "Timer.h"

gTimer::gTimer()
{
	//QueryPerformanceFrequency( &m_freq );
	//QueryPerformanceCounter( &m_last );
	//SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	m_lastTimePoint = std::chrono::high_resolution_clock::now();
}

gTimer::~gTimer()
{

}

float gTimer::getDelta()
{
	std::chrono::steady_clock::time_point _now = std::chrono::high_resolution_clock::now();

	//LARGE_INTEGER current;
	//QueryPerformanceCounter( &current );

	//float delta = ( (float)current.QuadPart - (float)m_last.QuadPart ) / (float)m_freq.QuadPart;
	//m_last = current;

	std::chrono::duration< float > tp = _now - m_lastTimePoint;
	m_lastTimePoint = _now;
	float delta = tp.count();

	return delta;
}

void  gTimer::reset()
{
	std::chrono::steady_clock::time_point m_lastTimePoint = std::chrono::high_resolution_clock::now();
	//QueryPerformanceCounter(&m_last);
}
