//
//  CSROMFetcher.m
//  Clock Signal
//
//  Created by Thomas Harte on 01/01/2018.
//  Copyright 2018 Thomas Harte. All rights reserved.
//

#import <Foundation/Foundation.h>
#include "CSROMFetcher.hpp"

#import "NSBundle+DataResource.h"
#import "NSData+StdVector.h"

#include <string>

ROMMachine::ROMFetcher CSROMFetcher(std::vector<ROMMachine::ROM> *missing_roms) {
	return [missing_roms] (const std::vector<ROMMachine::ROM> &roms) -> std::vector<std::unique_ptr<std::vector<std::uint8_t>>> {
		NSArray<NSURL *> *const supportURLs = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask];

		std::vector<std::unique_ptr<std::vector<std::uint8_t>>> results;
		for(const auto &rom: roms) {
			NSData *fileData;
			NSString *const subdirectory = [@"ROMImages/" stringByAppendingString:[NSString stringWithUTF8String:rom.machine_name.c_str()]];

			// Check for this file first within the application support directories.
			for(NSURL *supportURL in supportURLs) {
				NSURL *const fullURL = [[supportURL URLByAppendingPathComponent:subdirectory]
							URLByAppendingPathComponent:[NSString stringWithUTF8String:rom.file_name.c_str()]];
				fileData = [NSData dataWithContentsOfURL:fullURL];
				if(fileData) break;
			}

			// Failing that, check inside the application bundle.
			if(!fileData) {
				fileData = [[NSBundle mainBundle]
					dataForResource:[NSString stringWithUTF8String:rom.file_name.c_str()]
					withExtension:nil
					subdirectory:subdirectory];
			}

			// Store an appropriate result, accumulating a list of the missing if requested.
			if(!fileData) {
				results.emplace_back(nullptr);

				if(missing_roms) {
					missing_roms->push_back(rom);
				}
			}
			else {
				std::unique_ptr<std::vector<std::uint8_t>> data(new std::vector<std::uint8_t>);
				*data = fileData.stdVector8;
				results.emplace_back(std::move(data));
			}
		}

		return results;
	};
}
