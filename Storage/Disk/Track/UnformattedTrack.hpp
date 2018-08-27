//
//  UnformattedTrack.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 15/08/2017.
//  Copyright 2017 Thomas Harte. All rights reserved.
//

#ifndef UnformattedTrack_hpp
#define UnformattedTrack_hpp

#include "Track.hpp"

namespace Storage {
namespace Disk {

/*!
	A subclass of @c Track with no contents. Just an index hole.
*/
class UnformattedTrack: public Track {
	public:
		Event get_next_event() override;
		Time seek_to(const Time &time_since_index_hole) override;
		Track *clone() const override;
};

}
}

#endif /* UnformattedTrack_hpp */
