//
//  Flywheel.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 11/02/2016.
//  Copyright 2016 Thomas Harte. All rights reserved.
//

#ifndef Flywheel_hpp
#define Flywheel_hpp

#include <cstdlib>

namespace Outputs {
namespace CRT {

/*!
	Provides timing for a two-phase signal consisting of a retrace phase followed by a scan phase,
	announcing the start and end of retrace and providing the abiliy to read the current
	scanning position.

	The @c Flywheel will attempt to converge with timing implied by synchronisation pulses.
*/
struct Flywheel {
	/*!
		Constructs an instance of @c Flywheel.

		@param standard_period The expected amount of time between one synchronisation and the next.
		@param retrace_time The amount of time it takes to complete a retrace.
		@param sync_error_window The permitted deviation of sync timings from the norm.
	*/
	Flywheel(unsigned int standard_period, unsigned int retrace_time, unsigned int sync_error_window) :
		standard_period_(standard_period),
		retrace_time_(retrace_time),
		sync_error_window_(sync_error_window),
		counter_(0),
		counter_before_retrace_(standard_period - retrace_time),
		expected_next_sync_(standard_period),
		number_of_surprises_(0) {}

	enum SyncEvent {
		/// Indicates that no synchronisation events will occur in the queried window.
		None,
		/// Indicates that the next synchronisation event will be a transition into retrce.
		StartRetrace,
		/// Indicates that the next synchronisation event will be a transition out of retrace.
		EndRetrace
	};
	/*!
		Asks the flywheel for the first synchronisation event that will occur in a given time period,
		indicating whether a synchronisation request occurred at the start of the query window.

		@param sync_is_requested @c true indicates that the flywheel should act as though having
		received a synchronisation request now; @c false indicates no such event was detected.

		@param cycles_to_run_for The number of cycles to look ahead.

		@param cycles_advanced After this method has completed, contains the amount of time until
		the returned synchronisation event.

		@returns The next synchronisation event.
	*/
	inline SyncEvent get_next_event_in_period(bool sync_is_requested, unsigned int cycles_to_run_for, unsigned int *cycles_advanced) {
		// do we recognise this hsync, thereby adjusting future time expectations?
		if(sync_is_requested) {
			if(counter_ < sync_error_window_ || counter_ > expected_next_sync_ - sync_error_window_) {
				unsigned int time_now = (counter_ < sync_error_window_) ? expected_next_sync_ + counter_ : counter_;
				expected_next_sync_ = (3*expected_next_sync_ + time_now) >> 2;
			} else {
				number_of_surprises_++;

				if(counter_ < retrace_time_ + (expected_next_sync_ >> 1)) {
					expected_next_sync_ = (3*expected_next_sync_ + standard_period_ + sync_error_window_) >> 2;
				} else {
					expected_next_sync_ = (3*expected_next_sync_ + standard_period_ - sync_error_window_) >> 2;
				}
			}
		}

		SyncEvent proposed_event = SyncEvent::None;
		unsigned int proposed_sync_time = cycles_to_run_for;

		// will we end an ongoing retrace?
		if(counter_ < retrace_time_ && counter_ + proposed_sync_time >= retrace_time_) {
			proposed_sync_time = retrace_time_ - counter_;
			proposed_event = SyncEvent::EndRetrace;
		}

		// will we start a retrace?
		if(counter_ + proposed_sync_time >= expected_next_sync_) {
			proposed_sync_time = expected_next_sync_ - counter_;
			proposed_event = SyncEvent::StartRetrace;
		}

		*cycles_advanced = proposed_sync_time;
		return proposed_event;
	}

	/*!
		Advances a nominated amount of time, applying a previously returned synchronisation event
		at the end of that period.

		@param cycles_advanced The amount of time to run for.

		@param event The synchronisation event to apply after that period.
	*/
	inline void apply_event(unsigned int cycles_advanced, SyncEvent event) {
		counter_ += cycles_advanced;

		switch(event) {
			default: return;
			case StartRetrace:
				counter_before_retrace_ = counter_ - retrace_time_;
				counter_ = 0;
			return;
		}
	}

	/*!
		Returns the current output position; while in retrace this will go down towards 0, while in scan
		it will go upward.

		@returns The current output position.
	*/
	inline unsigned int get_current_output_position() {
		if(counter_ < retrace_time_) {
			unsigned int retrace_distance = (counter_ * standard_period_) / retrace_time_;
			if(retrace_distance > counter_before_retrace_) return 0;
			return counter_before_retrace_ - retrace_distance;
		}

		return counter_ - retrace_time_;
	}

	/*!
		@returns the amount of time since retrace last began. Time then counts monotonically up from zero.
	*/
	inline unsigned int get_current_time() {
		return counter_;
	}

	/*!
		@returns whether the output is currently retracing.
	*/
	inline bool is_in_retrace() {
		return counter_ < retrace_time_;
	}

	/*!
		@returns the expected length of the scan period (excluding retrace).
	*/
	inline unsigned int get_scan_period() {
		return standard_period_ - retrace_time_;
	}

	/*!
		@returns the expected length of a complete scan and retrace cycle.
	*/
	inline unsigned int get_standard_period() {
		return standard_period_;
	}

	/*!
		@returns the number of synchronisation events that have seemed surprising since the last time this method was called;
		a low number indicates good synchronisation.
	*/
	inline unsigned int get_and_reset_number_of_surprises() {
		unsigned int result = number_of_surprises_;
		number_of_surprises_ = 0;
		return result;
	}

	/*!
		@returns `true` if a sync is expected soon or the time at which it was expected was recent.
	*/
	inline bool is_near_expected_sync() {
		return abs(static_cast<int>(counter_) - static_cast<int>(expected_next_sync_)) < static_cast<int>(standard_period_) / 50;
	}

	private:
		unsigned int standard_period_;			// the normal length of time between syncs
		const unsigned int retrace_time_;		// a constant indicating the amount of time it takes to perform a retrace
		const unsigned int sync_error_window_;	// a constant indicating the window either side of the next expected sync in which we'll accept other syncs

		unsigned int counter_;					// time since the _start_ of the last sync
		unsigned int counter_before_retrace_;	// the value of _counter immediately before retrace began
		unsigned int expected_next_sync_;		// our current expection of when the next sync will be encountered (which implies velocity)

		unsigned int number_of_surprises_;		// a count of the surprising syncs

		/*
			Implementation notes:

			Retrace takes a fixed amount of time and runs during [0, _retrace_time).

			For the current line, scan then occurs from [_retrace_time, _expected_next_sync), at which point
			retrace begins and the internal counter is reset.

			All synchronisation events that occur within (-_sync_error_window, _sync_error_window) of the
			expected synchronisation time will cause a proportional adjustment in the expected time for the next
			synchronisation. Other synchronisation events are clamped as though they occurred in that range.
		*/
};

}
}

#endif /* Flywheel_hpp */
