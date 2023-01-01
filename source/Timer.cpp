#include "Timer.h"

gTimer::gTimer()
{
#ifdef USE_WIN_TIMER
	QueryPerformanceFrequency( &m_freq );
	QueryPerformanceCounter( &m_last );
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#else
	m_lastTimePoint = std::chrono::high_resolution_clock::now();
#endif
}

gTimer::~gTimer()
{

}

float gTimer::getDelta()
{
#ifdef USE_WIN_TIMER
	LARGE_INTEGER current;
	QueryPerformanceCounter( &current );

	float delta = static_cast<float>(current.QuadPart - m_last.QuadPart) / m_freq.QuadPart;
	m_last = current;
#else
	std::chrono::steady_clock::time_point _now = std::chrono::high_resolution_clock::now();
	std::chrono::duration< float > tp = _now - m_lastTimePoint;
	m_lastTimePoint = _now;
	float delta = tp.count();
#endif
	return delta;
}

void  gTimer::reset()
{
#ifdef USE_WIN_TIMER
	QueryPerformanceCounter(&m_last);
#else
	std::chrono::steady_clock::time_point m_lastTimePoint = std::chrono::high_resolution_clock::now();
#endif
}
