//
//  AcornADF.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 25/09/2016.
//  Copyright © 2016 Thomas Harte. All rights reserved.
//

#ifndef AcornADF_hpp
#define AcornADF_hpp

#include "MFMSectorDump.hpp"

#include <string>

namespace Storage {
namespace Disk {

/*!
	Provides a @c Disk containing an ADF disk image — a decoded sector dump of an Acorn ADFS disk.
*/
class AcornADF: public MFMSectorDump {
	public:
		/*!
			Construct an @c AcornADF containing content from the file with name @c file_name.

			@throws ErrorCantOpen if this file can't be opened.
			@throws ErrorNotAcornADF if the file doesn't appear to contain an Acorn .ADF format image.
		*/
		AcornADF(const std::string &file_name);

		enum {
			ErrorNotAcornADF,
		};

		int get_head_position_count() override;
		int get_head_count() override;

	private:
		long get_file_offset_for_position(Track::Address address) override;
};

}
}

#endif /* AcornADF_hpp */
