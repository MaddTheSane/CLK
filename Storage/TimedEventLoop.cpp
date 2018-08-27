//
//  TimedEventLoop.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 29/07/2016.
//  Copyright 2016 Thomas Harte. All rights reserved.
//

#include "TimedEventLoop.hpp"
#include "../NumberTheory/Factors.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

using namespace Storage;

TimedEventLoop::TimedEventLoop(unsigned int input_clock_rate) :
	input_clock_rate_(input_clock_rate) {}

void TimedEventLoop::run_for(const Cycles cycles) {
	int remaining_cycles = cycles.as_int();
#ifndef NDEBUG
	int cycles_advanced = 0;
#endif

	while(cycles_until_event_ <= remaining_cycles) {
#ifndef NDEBUG
		cycles_advanced += cycles_until_event_;
#endif
		advance(cycles_until_event_);
		remaining_cycles -= cycles_until_event_;
		cycles_until_event_ = 0;
		process_next_event();
	}

	if(remaining_cycles) {
		cycles_until_event_ -= remaining_cycles;
#ifndef NDEBUG
		cycles_advanced += remaining_cycles;
#endif
		advance(remaining_cycles);
	}

	assert(cycles_advanced == cycles.as_int());
	assert(cycles_until_event_ > 0);
}

unsigned int TimedEventLoop::get_cycles_until_next_event() {
	return static_cast<unsigned int>(std::max(cycles_until_event_, 0));
}

unsigned int TimedEventLoop::get_input_clock_rate() {
	return input_clock_rate_;
}

void TimedEventLoop::reset_timer() {
	subcycles_until_event_ = 0.0;
	cycles_until_event_ = 0;
}

void TimedEventLoop::jump_to_next_event() {
	reset_timer();
	process_next_event();
}

void TimedEventLoop::set_next_event_time_interval(Time interval) {
	// Calculate [interval]*[input clock rate] + [subcycles until this event]
	double double_interval = interval.get<double>() * static_cast<double>(input_clock_rate_) + subcycles_until_event_;

	// So this event will fire in the integral number of cycles from now, putting us at the remainder
	// number of subcycles
	const int addition = static_cast<int>(double_interval);
	cycles_until_event_ += addition;
	subcycles_until_event_ = fmod(double_interval, 1.0);

	assert(cycles_until_event_ >= 0);
	assert(subcycles_until_event_ >= 0.0);
}

Time TimedEventLoop::get_time_into_next_event() {
	// TODO: calculate, presumably as [length of interval] - ([cycles left] + [subcycles left])
	Time zero;
	return zero;
}
