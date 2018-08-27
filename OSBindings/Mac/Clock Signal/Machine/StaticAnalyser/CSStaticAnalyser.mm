//
//  CSStaticAnalyser.m
//  Clock Signal
//
//  Created by Thomas Harte on 31/08/2016.
//  Copyright 2016 Thomas Harte. All rights reserved.
//

#import "CSStaticAnalyser.h"

#import "CSMachine.h"
#import "CSMachine+Target.h"

#include "StaticAnalyser.hpp"

#include "../../../../../Analyser/Static/Acorn/Target.hpp"
#include "../../../../../Analyser/Static/AmstradCPC/Target.hpp"
#include "../../../../../Analyser/Static/AppleII/Target.hpp"
#include "../../../../../Analyser/Static/Commodore/Target.hpp"
#include "../../../../../Analyser/Static/MSX/Target.hpp"
#include "../../../../../Analyser/Static/Oric/Target.hpp"
#include "../../../../../Analyser/Static/ZX8081/Target.hpp"

#import "Clock_Signal-Swift.h"

@implementation CSStaticAnalyser {
	Analyser::Static::TargetList _targets;
}

- (instancetype)initWithFileAtURL:(NSURL *)url {
	self = [super init];
	if(self) {
		_targets = Analyser::Static::GetTargets([url fileSystemRepresentation]);
		if(!_targets.size()) return nil;

		// TODO: could this better be supplied by the analyser? A hypothetical file format might
		// provide a better name for it contents than the file name?
		_displayName = [[url pathComponents] lastObject];
	}
	return self;
}

- (instancetype)initWithElectronDFS:(BOOL)dfs adfs:(BOOL)adfs {
	self = [super init];
	if(self) {
		using Target = Analyser::Static::Acorn::Target;
		std::unique_ptr<Target> target(new Target);
		target->machine = Analyser::Machine::Electron;
		target->has_dfs = !!dfs;
		target->has_adfs = !!adfs;
		_targets.push_back(std::move(target));
	}
	return self;
}

- (instancetype)initWithAmstradCPCModel:(CSMachineCPCModel)model {
	self = [super init];
	if(self) {
		using Target = Analyser::Static::AmstradCPC::Target;
		std::unique_ptr<Target> target(new Target);
		target->machine = Analyser::Machine::AmstradCPC;
		switch(model) {
			case CSMachineCPCModel464: target->model = Analyser::Static::AmstradCPC::Target::Model::CPC464;		break;
			case CSMachineCPCModel664: target->model = Analyser::Static::AmstradCPC::Target::Model::CPC664;		break;
			case CSMachineCPCModel6128: target->model = Analyser::Static::AmstradCPC::Target::Model::CPC6128;	break;
		}
		_targets.push_back(std::move(target));
	}
	return self;
}

- (instancetype)initWithMSXHasDiskDrive:(BOOL)hasDiskDrive {
	self = [super init];
	if(self) {
		using Target = Analyser::Static::MSX::Target;
		std::unique_ptr<Target> target(new Target);
		target->machine = Analyser::Machine::MSX;
		target->has_disk_drive = !!hasDiskDrive;
		_targets.push_back(std::move(target));
	}
	return self;
}

- (instancetype)initWithOricModel:(CSMachineOricModel)model diskInterface:(CSMachineOricDiskInterface)diskInterface {
	self = [super init];
	if(self) {
		using Target = Analyser::Static::Oric::Target;
		std::unique_ptr<Target> target(new Target);
		target->machine = Analyser::Machine::Oric;
		switch(model) {
			case CSMachineOricModelOric1:		target->rom = Target::ROM::BASIC10;	break;
			case CSMachineOricModelOricAtmos:	target->rom = Target::ROM::BASIC11;	break;
			case CSMachineOricModelPravetz:		target->rom = Target::ROM::Pravetz;	break;
		}
		switch(diskInterface) {
			case CSMachineOricDiskInterfaceNone:		target->disk_interface = Target::DiskInterface::None;		break;
			case CSMachineOricDiskInterfaceMicrodisc:	target->disk_interface = Target::DiskInterface::Microdisc;	break;
			case CSMachineOricDiskInterfacePravetz:		target->disk_interface = Target::DiskInterface::Pravetz;	break;
		}
		_targets.push_back(std::move(target));
	}
	return self;
}

- (instancetype)initWithVic20Region:(CSMachineVic20Region)region memorySize:(Kilobytes)memorySize hasC1540:(BOOL)hasC1540 {
	self = [super init];
	if(self) {
		using Target = Analyser::Static::Commodore::Target;
		std::unique_ptr<Target> target(new Target);
		target->machine = Analyser::Machine::Vic20;
		switch(region) {
			case CSMachineVic20RegionDanish:	target->region = Target::Region::Danish;	break;
			case CSMachineVic20RegionSwedish:	target->region = Target::Region::Swedish;	break;
			case CSMachineVic20RegionAmerican:	target->region = Target::Region::American;	break;
			case CSMachineVic20RegionEuropean:	target->region = Target::Region::European;	break;
			case CSMachineVic20RegionJapanese:	target->region = Target::Region::Japanese;	break;
		}
		switch(memorySize) {
			default:	target->memory_model = Target::MemoryModel::Unexpanded;		break;
			case 8:		target->memory_model = Target::MemoryModel::EightKB;		break;
			case 32:	target->memory_model = Target::MemoryModel::ThirtyTwoKB;	break;
		}
		target->has_c1540 = !!hasC1540;
		_targets.push_back(std::move(target));
	}
	return self;
}

static Analyser::Static::ZX8081::Target::MemoryModel ZX8081MemoryModelFromSize(Kilobytes size) {
	using MemoryModel = Analyser::Static::ZX8081::Target::MemoryModel;
	switch(size) {
		default:	return MemoryModel::Unexpanded;
		case 16:	return MemoryModel::SixteenKB;
		case 64:	return MemoryModel::SixtyFourKB;
	}
}

- (instancetype)initWithZX80MemorySize:(Kilobytes)memorySize useZX81ROM:(BOOL)useZX81ROM {
	self = [super init];
	if(self) {
		using Target = Analyser::Static::ZX8081::Target;
		std::unique_ptr<Target> target(new Target);
		target->machine = Analyser::Machine::ZX8081;
		target->is_ZX81 = false;
		target->ZX80_uses_ZX81_ROM = !!useZX81ROM;
		target->memory_model = ZX8081MemoryModelFromSize(memorySize);
		_targets.push_back(std::move(target));
	}
	return self;
}

- (instancetype)initWithZX81MemorySize:(Kilobytes)memorySize {
	self = [super init];
	if(self) {
		using Target = Analyser::Static::ZX8081::Target;
		std::unique_ptr<Target> target(new Target);
		target->machine = Analyser::Machine::ZX8081;
		target->is_ZX81 = true;
		target->memory_model = ZX8081MemoryModelFromSize(memorySize);
		_targets.push_back(std::move(target));
	}
	return self;
}

- (instancetype)initWithAppleIIModel:(CSMachineAppleIIModel)model diskController:(CSMachineAppleIIDiskController)diskController {
	self = [super init];
	if(self) {
		using Target = Analyser::Static::AppleII::Target;
		std::unique_ptr<Target> target(new Target);
		target->machine = Analyser::Machine::AppleII;
		switch(model) {
			default: 									target->model = Target::Model::II; 				break;
			case CSMachineAppleIIModelAppleIIPlus:		target->model = Target::Model::IIplus;			break;
			case CSMachineAppleIIModelAppleIIe:			target->model = Target::Model::IIe;				break;
			case CSMachineAppleIIModelAppleEnhancedIIe:	target->model = Target::Model::EnhancedIIe;		break;
		}
		switch(diskController) {
			default:
			case CSMachineAppleIIDiskControllerNone:			target->disk_controller = Target::DiskController::None;				break;
			case CSMachineAppleIIDiskControllerSixteenSector:	target->disk_controller = Target::DiskController::SixteenSector;	break;
			case CSMachineAppleIIDiskControllerThirteenSector:	target->disk_controller = Target::DiskController::ThirteenSector;	break;
		}
		_targets.push_back(std::move(target));
	}
	return self;

}

- (NSString *)optionsPanelNibName {
	switch(_targets.front()->machine) {
		case Analyser::Machine::AmstradCPC:	return @"CompositeOptions";
//		case Analyser::Machine::AppleII:	return @"AppleIIOptions";
		case Analyser::Machine::Atari2600:	return @"Atari2600Options";
		case Analyser::Machine::Electron:	return @"QuickLoadCompositeOptions";
		case Analyser::Machine::MSX:		return @"QuickLoadCompositeOptions";
		case Analyser::Machine::Oric:		return @"OricOptions";
		case Analyser::Machine::Vic20:		return @"QuickLoadCompositeOptions";
		case Analyser::Machine::ZX8081:		return @"ZX8081Options";
		default: return nil;
	}
}

- (Analyser::Static::TargetList &)targets {
	return _targets;
}

@end

@implementation CSMediaSet {
	Analyser::Static::Media _media;
}

- (instancetype)initWithFileAtURL:(NSURL *)url {
	self = [super init];
	if(self) {
		_media = Analyser::Static::GetMedia([url fileSystemRepresentation]);
	}
	return self;
}

- (void)applyToMachine:(CSMachine *)machine {
	[machine applyMedia:_media];
}

@end
