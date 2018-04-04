//
//  Target.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 09/03/2018.
//  Copyright © 2018 Thomas Harte. All rights reserved.
//

#ifndef Analyser_Static_Oric_Target_h
#define Analyser_Static_Oric_Target_h

namespace Analyser {
namespace Static {
namespace Oric {

struct Target: public ::Analyser::Static::Target {
	bool use_atmos_rom = false;
	bool has_microdrive = false;
};

}
}
}

#endif /* Analyser_Static_Oric_Target_h */
