#pragma once

#ifndef _TIMER_H_
#define _TIMER_H_

#define USE_WIN_TIMER WIN32

#ifdef USE_WIN_TIMER
#include <windows.h>
#else
#include <chrono>
#endif

// cross-platform hi-res game timer
class gTimer
{
public:
	gTimer();
	~gTimer();

	float getDelta(); 
	void reset();
protected:
#ifdef USE_WIN_TIMER
	LARGE_INTEGER m_last;
	LARGE_INTEGER m_freq;
#else
	std::chrono::steady_clock::time_point m_lastTimePoint;
#endif
};

#endif