//
//  StaticAnalyser.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 23/08/2016.
//  Copyright © 2016 Thomas Harte. All rights reserved.
//

#include "StaticAnalyser.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

// Analysers
#include "Acorn/StaticAnalyser.hpp"
#include "AmstradCPC/StaticAnalyser.hpp"
#include "Atari/StaticAnalyser.hpp"
#include "Coleco/StaticAnalyser.hpp"
#include "Commodore/StaticAnalyser.hpp"
#include "MSX/StaticAnalyser.hpp"
#include "Oric/StaticAnalyser.hpp"
#include "ZX8081/StaticAnalyser.hpp"

// Cartridges
#include "../../Storage/Cartridge/Formats/BinaryDump.hpp"
#include "../../Storage/Cartridge/Formats/PRG.hpp"

// Disks
#include "../../Storage/Disk/DiskImage/Formats/AcornADF.hpp"
#include "../../Storage/Disk/DiskImage/Formats/CPCDSK.hpp"
#include "../../Storage/Disk/DiskImage/Formats/D64.hpp"
#include "../../Storage/Disk/DiskImage/Formats/G64.hpp"
#include "../../Storage/Disk/DiskImage/Formats/DMK.hpp"
#include "../../Storage/Disk/DiskImage/Formats/HFE.hpp"
#include "../../Storage/Disk/DiskImage/Formats/MSXDSK.hpp"
#include "../../Storage/Disk/DiskImage/Formats/OricMFMDSK.hpp"
#include "../../Storage/Disk/DiskImage/Formats/SSD.hpp"

// Tapes
#include "../../Storage/Tape/Formats/CAS.hpp"
#include "../../Storage/Tape/Formats/CommodoreTAP.hpp"
#include "../../Storage/Tape/Formats/CSW.hpp"
#include "../../Storage/Tape/Formats/OricTAP.hpp"
#include "../../Storage/Tape/Formats/TapePRG.hpp"
#include "../../Storage/Tape/Formats/TapeUEF.hpp"
#include "../../Storage/Tape/Formats/TZX.hpp"
#include "../../Storage/Tape/Formats/ZX80O81P.hpp"

// Target Platform Types
#include "../../Storage/TargetPlatforms.hpp"

using namespace Analyser::Static;

static Media GetMediaAndPlatforms(const std::string &file_name, TargetPlatform::IntType &potential_platforms) {
	Media result;

	// Get the extension, if any; it will be assumed that extensions are reliable, so an extension is a broad-phase
	// test as to file format.
	std::string::size_type final_dot = file_name.find_last_of(".");
	if(final_dot == std::string::npos) return result;
	std::string extension = file_name.substr(final_dot + 1);
	std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

#define Insert(list, class, platforms) \
	list.emplace_back(new Storage::class(file_name));\
	potential_platforms |= platforms;\
	TargetPlatform::TypeDistinguisher *distinguisher = dynamic_cast<TargetPlatform::TypeDistinguisher *>(list.back().get());\
	if(distinguisher) potential_platforms &= distinguisher->target_platform_type();

#define TryInsert(list, class, platforms) \
	try {\
		Insert(list, class, platforms) \
	} catch(...) {}

#define Format(ext, list, class, platforms) \
	if(extension == ext)	{		\
		TryInsert(list, class, platforms)	\
	}

	Format("80", result.tapes, Tape::ZX80O81P, TargetPlatform::ZX8081)										// 80
	Format("81", result.tapes, Tape::ZX80O81P, TargetPlatform::ZX8081)										// 81
	Format("a26", result.cartridges, Cartridge::BinaryDump, TargetPlatform::Atari2600)						// A26
	Format("adf", result.disks, Disk::DiskImageHolder<Storage::Disk::AcornADF>, TargetPlatform::Acorn)		// ADF
	Format("bin", result.cartridges, Cartridge::BinaryDump, TargetPlatform::AllCartridge)					// BIN
	Format("cas", result.tapes, Tape::CAS, TargetPlatform::MSX)												// CAS
	Format("cdt", result.tapes, Tape::TZX, TargetPlatform::AmstradCPC)										// CDT
	Format("col", result.cartridges, Cartridge::BinaryDump, TargetPlatform::ColecoVision)					// COL
	Format("csw", result.tapes, Tape::CSW, TargetPlatform::AllTape)											// CSW
	Format("d64", result.disks, Disk::DiskImageHolder<Storage::Disk::D64>, TargetPlatform::Commodore)		// D64
	Format("dmk", result.disks, Disk::DiskImageHolder<Storage::Disk::DMK>, TargetPlatform::MSX)				// DMK
	Format("dsd", result.disks, Disk::DiskImageHolder<Storage::Disk::SSD>, TargetPlatform::Acorn)			// DSD
	Format("dsk", result.disks, Disk::DiskImageHolder<Storage::Disk::CPCDSK>, TargetPlatform::AmstradCPC)	// DSK (Amstrad CPC)
	Format("dsk", result.disks, Disk::DiskImageHolder<Storage::Disk::MSXDSK>, TargetPlatform::MSX)			// DSK (MSX)
	Format("dsk", result.disks, Disk::DiskImageHolder<Storage::Disk::OricMFMDSK>, TargetPlatform::Oric)		// DSK (Oric)
	Format("g64", result.disks, Disk::DiskImageHolder<Storage::Disk::G64>, TargetPlatform::Commodore)		// G64
	Format(	"hfe",
			result.disks,
			Disk::DiskImageHolder<Storage::Disk::HFE>,
			TargetPlatform::Acorn | TargetPlatform::AmstradCPC | TargetPlatform::Commodore | TargetPlatform::Oric)
			// HFE (TODO: switch to AllDisk once the MSX stops being so greedy)
	Format("o", result.tapes, Tape::ZX80O81P, TargetPlatform::ZX8081)										// O
	Format("p", result.tapes, Tape::ZX80O81P, TargetPlatform::ZX8081)										// P
	Format("p81", result.tapes, Tape::ZX80O81P, TargetPlatform::ZX8081)										// P81

	// PRG
	if(extension == "prg") {
		// try instantiating as a ROM; failing that accept as a tape
		try {
			Insert(result.cartridges, Cartridge::PRG, TargetPlatform::Commodore)
		} catch(...) {
			try {
				Insert(result.tapes, Tape::PRG, TargetPlatform::Commodore)
			} catch(...) {}
		}
	}

	Format(	"rom",
			result.cartridges,
			Cartridge::BinaryDump,
			TargetPlatform::AcornElectron | TargetPlatform::ColecoVision | TargetPlatform::MSX)				// ROM
	Format("ssd", result.disks, Disk::DiskImageHolder<Storage::Disk::SSD>, TargetPlatform::Acorn)			// SSD
	Format("tap", result.tapes, Tape::CommodoreTAP, TargetPlatform::Commodore)								// TAP (Commodore)
	Format("tap", result.tapes, Tape::OricTAP, TargetPlatform::Oric)										// TAP (Oric)
	Format("tsx", result.tapes, Tape::TZX, TargetPlatform::MSX)												// TSX
	Format("tzx", result.tapes, Tape::TZX, TargetPlatform::ZX8081)											// TZX
	Format("uef", result.tapes, Tape::UEF, TargetPlatform::Acorn)											// UEF (tape)

#undef Format
#undef Insert
#undef TryInsert

	return result;
}

Media Analyser::Static::GetMedia(const std::string &file_name) {
	TargetPlatform::IntType throwaway;
	return GetMediaAndPlatforms(file_name, throwaway);
}

std::vector<std::unique_ptr<Target>> Analyser::Static::GetTargets(const std::string &file_name) {
	std::vector<std::unique_ptr<Target>> targets;

	// Collect all disks, tapes and ROMs as can be extrapolated from this file, forming the
	// union of all platforms this file might be a target for.
	TargetPlatform::IntType potential_platforms = 0;
	Media media = GetMediaAndPlatforms(file_name, potential_platforms);

	// Hand off to platform-specific determination of whether these things are actually compatible and,
	// if so, how to load them.
	if(potential_platforms & TargetPlatform::Acorn)			Acorn::AddTargets(media, targets);
	if(potential_platforms & TargetPlatform::AmstradCPC)	AmstradCPC::AddTargets(media, targets);
	if(potential_platforms & TargetPlatform::Atari2600)		Atari::AddTargets(media, targets);
	if(potential_platforms & TargetPlatform::ColecoVision)	Coleco::AddTargets(media, targets);
	if(potential_platforms & TargetPlatform::Commodore)		Commodore::AddTargets(media, targets, file_name);
	if(potential_platforms & TargetPlatform::MSX)			MSX::AddTargets(media, targets);
	if(potential_platforms & TargetPlatform::Oric)			Oric::AddTargets(media, targets);
	if(potential_platforms & TargetPlatform::ZX8081)		ZX8081::AddTargets(media, targets, potential_platforms);

	// Reset any tapes to their initial position
	for(auto &target : targets) {
		for(auto &tape : target->media.tapes) {
			tape->reset();
		}
	}

	// Sort by initial confidence. Use a stable sort in case any of the machine-specific analysers
	// picked their insertion order carefully.
	std::stable_sort(targets.begin(), targets.end(),
        [] (const std::unique_ptr<Target> &a, const std::unique_ptr<Target> &b) {
		    return a->confidence > b->confidence;
	    });

	return targets;
}
