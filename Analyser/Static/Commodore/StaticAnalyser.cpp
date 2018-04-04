//
//  CommodoreAnalyser.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 06/09/2016.
//  Copyright © 2016 Thomas Harte. All rights reserved.
//

#include "StaticAnalyser.hpp"

#include "Disk.hpp"
#include "File.hpp"
#include "Tape.hpp"
#include "Target.hpp"
#include "../../../Storage/Cartridge/Encodings/CommodoreROM.hpp"

#include <sstream>

using namespace Analyser::Static::Commodore;

static std::vector<std::shared_ptr<Storage::Cartridge::Cartridge>>
		Vic20CartridgesFrom(const std::vector<std::shared_ptr<Storage::Cartridge::Cartridge>> &cartridges) {
	std::vector<std::shared_ptr<Storage::Cartridge::Cartridge>> vic20_cartridges;

	for(const auto &cartridge : cartridges) {
		const auto &segments = cartridge->get_segments();

		// only one mapped item is allowed
		if(segments.size() != 1) continue;

		// which must be 16 kb in size
		Storage::Cartridge::Cartridge::Segment segment = segments.front();
		if(segment.start_address != 0xa000) continue;
		if(!Storage::Cartridge::Encodings::CommodoreROM::isROM(segment.data)) continue;

		vic20_cartridges.push_back(cartridge);
	}

	return vic20_cartridges;
}

void Analyser::Static::Commodore::AddTargets(const Media &media, std::vector<std::unique_ptr<Analyser::Static::Target>> &destination) {
	std::unique_ptr<Target> target(new Target);
	target->machine = Machine::Vic20;	// TODO: machine estimation
	target->confidence = 0.5; // TODO: a proper estimation

	int device = 0;
	std::vector<File> files;
	bool is_disk = false;

	// strip out inappropriate cartridges
	target->media.cartridges = Vic20CartridgesFrom(media.cartridges);

	// check disks
	for(auto &disk : media.disks) {
		std::vector<File> disk_files = GetFiles(disk);
		if(!disk_files.empty()) {
			is_disk = true;
			files.insert(files.end(), disk_files.begin(), disk_files.end());
			target->media.disks.push_back(disk);
			if(!device) device = 8;
		}
	}

	// check tapes
	for(auto &tape : media.tapes) {
		std::vector<File> tape_files = GetFiles(tape);
		tape->reset();
		if(!tape_files.empty()) {
			files.insert(files.end(), tape_files.begin(), tape_files.end());
			target->media.tapes.push_back(tape);
			if(!device) device = 1;
		}
	}

	if(!files.empty()) {
		target->memory_model = Target::MemoryModel::Unexpanded;
		std::ostringstream string_stream;
		string_stream << "LOAD\"" << (is_disk ? "*" : "") << "\"," << device << ",";
  		if(files.front().is_basic()) {
			string_stream << "0";
		} else {
			string_stream << "1";
		}
		string_stream << "\nRUN\n";
		target->loading_command = string_stream.str();

		// make a first guess based on loading address
		switch(files.front().starting_address) {
			default:
				printf("Starting address %04x?\n", files.front().starting_address);
			case 0x1001:
				target->memory_model = Target::MemoryModel::Unexpanded;
			break;
			case 0x1201:
				target->memory_model = Target::MemoryModel::ThirtyTwoKB;
			break;
			case 0x0401:
				target->memory_model = Target::MemoryModel::EightKB;
			break;
		}

		// General approach: increase memory size conservatively such that the largest file found will fit.
//		for(File &file : files) {
//			std::size_t file_size = file.data.size();
//			bool is_basic = file.is_basic();

			/*if(is_basic)
			{
				// BASIC files may be relocated, so the only limit is size.
				//
				// An unexpanded machine has 3583 bytes free for BASIC;
				// a 3kb expanded machine has 6655 bytes free.
				if(file_size > 6655)
					target->vic20.memory_model = Vic20MemoryModel::ThirtyTwoKB;
				else if(target->vic20.memory_model == Vic20MemoryModel::Unexpanded && file_size > 3583)
					target->vic20.memory_model = Vic20MemoryModel::EightKB;
			}
			else
			{*/
//			if(!file.type == File::NonRelocatableProgram)
//			{
				// Non-BASIC files may be relocatable but, if so, by what logic?
				// Given that this is unknown, take starting address as literal
				// and check against memory windows.
				//
				// (ignoring colour memory...)
				// An unexpanded Vic has memory between 0x0000 and 0x0400; and between 0x1000 and 0x2000.
				// A 3kb expanded Vic fills in the gap and has memory between 0x0000 and 0x2000.
				// A 32kb expanded Vic has memory in the entire low 32kb.
//				uint16_t starting_address = file.starting_address;

				// If anything above the 8kb mark is touched, mark as a 32kb machine; otherwise if the
				// region 0x0400 to 0x1000 is touched and this is an unexpanded machine, mark as 3kb.
//				if(starting_address + file_size > 0x2000)
//					target->memory_model = Target::MemoryModel::ThirtyTwoKB;
//				else if(target->memory_model == Target::MemoryModel::Unexpanded && !(starting_address >= 0x1000 || starting_address+file_size < 0x0400))
//					target->memory_model = Target::MemoryModel::ThirtyTwoKB;
//			}
//		}
	}

	if(!target->media.empty())
		destination.push_back(std::move(target));
}
