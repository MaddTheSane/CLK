//
//  Target.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 09/03/2018.
//  Copyright 2018 Thomas Harte. All rights reserved.
//

#ifndef Analyser_Static_AmstradCPC_Target_h
#define Analyser_Static_AmstradCPC_Target_h

#include "../../../Reflection/Enum.hpp"
#include "../../../Reflection/Struct.hpp"
#include "../StaticAnalyser.hpp"
#include <string>

namespace Analyser {
namespace Static {
namespace AmstradCPC {

struct Target: public Analyser::Static::Target, public Reflection::StructImpl<Target> {
	ReflectableEnum(Model, CPC464, CPC664, CPC6128);
	Model model = Model::CPC464;
	std::string loading_command;

	Target() : Analyser::Static::Target(Machine::AmstradCPC) {
		if(needs_declare()) {
			DeclareField(model);
			AnnounceEnum(Model);
		}
	}
};

}
}
}


#endif /* Analyser_Static_AmstradCPC_Target_h */
