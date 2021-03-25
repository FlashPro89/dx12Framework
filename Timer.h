#pragma once

#ifndef _TIMER_H_
#define _TIMER_H_

//#include <windows.h>
#include <chrono>

// cross-platform hi-res game timer
class gTimer
{
public:
	gTimer();
	~gTimer();

	float getDelta(); 
	void reset();
protected:
	//LARGE_INTEGER m_last;
	//LARGE_INTEGER m_freq;
	std::chrono::steady_clock::time_point m_lastTimePoint;
};

#endif