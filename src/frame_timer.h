#pragma once

#include <Windows.h>

#include <chrono>

// One-shot wait used by the frame loop. The Present interval and sync interval
// are decided elsewhere; this only wakes the thread near a steady-clock deadline.
class FrameTimer
{
public:
	FrameTimer();
	~FrameTimer();

	FrameTimer(const FrameTimer&) = delete;
	FrameTimer& operator=(const FrameTimer&) = delete;

	// Null when this process has to keep the millisecond MsgWait timeout.
	[[nodiscard]] HANDLE Handle() const { return waitable_ ? timer_ : nullptr; }
	[[nodiscard]] const char* ModeName() const { return modeName_; }

	// Replaces any armed deadline. Returns false when the deadline is already due.
	bool ArmUntil(std::chrono::steady_clock::time_point deadline);
	void Cancel();

private:
	HANDLE timer_ = nullptr;
	bool waitable_ = false;
	bool raisedResolution_ = false;
	const char* modeName_ = "millisecond_timeout";
};
