//
//  MSXDSK.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 07/01/2018.
//  Copyright 2018 Thomas Harte. All rights reserved.
//

#ifndef MSXDSK_hpp
#define MSXDSK_hpp

#include "MFMSectorDump.hpp"

#include <string>

namespace Storage {
namespace Disk {

/*!
	Provides a @c DiskImage descriging an MSX-style disk image:
	a sector dump of appropriate proportions.
*/
class MSXDSK: public MFMSectorDump {
	public:
		MSXDSK(const std::string &file_name);
		HeadPosition get_maximum_head_position() override;
		int get_head_count() override;

	private:
		long get_file_offset_for_position(Track::Address address) override;

		int head_count_;
		int track_count_;
};

}
}

#endif /* MSXDSK_hpp */
