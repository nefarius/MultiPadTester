#include "frame_timer.h"

#include <mmsystem.h>

#include <chrono>

namespace
{
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

using HundredNanoseconds = std::chrono::duration<int64_t, std::ratio<1, 10000000>>;

HANDLE CreateTimer(const DWORD flags)
{
	return CreateWaitableTimerExW(nullptr, nullptr, flags, TIMER_ALL_ACCESS);
}
}

FrameTimer::FrameTimer()
{
	timer_ = CreateTimer(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION);
	if (timer_)
	{
		waitable_ = true;
		modeName_ = "high_resolution_timer";
		return;
	}

	timer_ = CreateTimer(0);
	if (!timer_)
		return;

	if (timeBeginPeriod(1) == TIMERR_NOERROR)
	{
		raisedResolution_ = true;
		waitable_ = true;
		modeName_ = "timer_resolution_1ms";
	}
	else
	{
		CloseHandle(timer_);
		timer_ = nullptr;
	}
}

FrameTimer::~FrameTimer()
{
	if (timer_)
	{
		CancelWaitableTimer(timer_);
		CloseHandle(timer_);
	}
	if (raisedResolution_)
		timeEndPeriod(1);
}

bool FrameTimer::ArmUntil(const std::chrono::steady_clock::time_point deadline)
{
	if (!waitable_)
		return false;

	const auto remaining = deadline - std::chrono::steady_clock::now();
	if (remaining <= std::chrono::steady_clock::duration::zero())
		return false;

	// Round up so the timer does not fire early and make the loop spin.
	// The 1 ms fallback cannot wait a fraction of a millisecond.
	HundredNanoseconds ticks{1};
	if (raisedResolution_)
	{
		const auto milliseconds = std::chrono::ceil<std::chrono::milliseconds>(remaining);
		const auto wait = milliseconds.count() < 1 ? std::chrono::milliseconds{1} : milliseconds;
		ticks = std::chrono::duration_cast<HundredNanoseconds>(wait);
	}
	else
	{
		ticks = std::chrono::ceil<HundredNanoseconds>(remaining);
		if (ticks.count() < 1)
			ticks = HundredNanoseconds{1};
	}

	LARGE_INTEGER due{};
	due.QuadPart = -ticks.count();
	return SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE) != FALSE;
}

void FrameTimer::Cancel()
{
	if (timer_)
		CancelWaitableTimer(timer_);
}
