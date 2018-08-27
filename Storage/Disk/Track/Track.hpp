//
//  Track.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 22/09/2017.
//  Copyright 2017 Thomas Harte. All rights reserved.
//

#ifndef Track_h
#define Track_h

#include "../../Storage.hpp"
#include <tuple>

namespace Storage {
namespace Disk {

/*!
	Contains a head position, with some degree of sub-integral precision.
*/
class HeadPosition {
	public:
		/// Creates an instance decribing position @c value at a resolution of @c scale ticks per track.
		HeadPosition(int value, int scale) : position_(value * (4/scale)) {}
		explicit HeadPosition(int value) : HeadPosition(value, 1) {}
		HeadPosition() : HeadPosition(0) {}

		/// @returns the whole number part of the position.
		int as_int() const { return position_ >> 2; }
		/// @returns n where n/2 is the head position.
		int as_half() const { return position_ >> 1; }
		/// @returns n where n/4 is the head position.
		int as_quarter() const { return position_; }

		/// @returns the head position at maximal but unspecified precision.
		int as_largest() const { return as_quarter(); }

		HeadPosition &operator +=(const HeadPosition &rhs) {
			position_ += rhs.position_;
			return *this;
		}
		bool operator ==(const HeadPosition &rhs) const {
			return position_ == rhs.position_;
		}
		bool operator !=(const HeadPosition &rhs) const {
			return position_ != rhs.position_;
		}
		bool operator <(const HeadPosition &rhs) const {
			return position_ < rhs.position_;
		}
		bool operator <=(const HeadPosition &rhs) const {
			return position_ <= rhs.position_;
		}
		bool operator >(const HeadPosition &rhs) const {
			return position_ > rhs.position_;
		}
		bool operator >=(const HeadPosition &rhs) const {
			return position_ >= rhs.position_;
		}

	private:
		int position_ = 0;
};

/*!
	Models a single track on a disk as a series of events, each event being of arbitrary length
	and resulting in either a flux transition or the sensing of an index hole.

	Subclasses should implement @c get_next_event.
*/
class Track {
	public:
		/*!
			Describes the location of a track, implementing < to allow for use as a set key.
		*/
		struct Address {
			int head;
			HeadPosition position;

			bool operator < (const Address &rhs) const {
				int largest_position = position.as_largest();
				int rhs_largest_position = rhs.position.as_largest();
				return std::tie(head, largest_position) < std::tie(rhs.head, rhs_largest_position);
			}
			Address(int head, HeadPosition position) : head(head), position(position) {}
		};

		/*!
			Describes a detectable track event: either a flux transition or the passing of the index hole,
			along with the length of time between the previous event and its occurance.

			The sum of all lengths of time across an entire track should be 1; if an event is said to be
			1/3 away then that means 1/3 of a rotation.
		*/
		struct Event {
			enum {
				IndexHole, FluxTransition
			} type;
			Time length;
		};

		/*!
			@returns the next event that will be detected during rotation of this disk.
		*/
		virtual Event get_next_event() = 0;

		/*!
			Jumps to the event latest offset that is less than or equal to the input time.

			@returns the time jumped to.
		*/
		virtual Time seek_to(const Time &time_since_index_hole) = 0;

		/*!
			The virtual copy constructor pattern; returns a copy of the Track.
		*/
		virtual Track *clone() const = 0;
};

}
}

#endif /* Track_h */
