//
//  AppleDSK.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 27/04/2018.
//  Copyright 2018 Thomas Harte. All rights reserved.
//

#include "AppleDSK.hpp"

#include "../../Track/PCMTrack.hpp"
#include "../../Track/TrackSerialiser.hpp"
#include "../../Encodings/AppleGCR/Encoder.hpp"
#include "../../Encodings/AppleGCR/SegmentParser.hpp"

#include <cstring>

using namespace Storage::Disk;

namespace {
	const int number_of_tracks = 35;
	const int bytes_per_sector = 256;
}

AppleDSK::AppleDSK(const std::string &file_name) :
	file_(file_name) {
	if(file_.stats().st_size % (number_of_tracks*bytes_per_sector)) throw Error::InvalidFormat;

	sectors_per_track_ = static_cast<int>(file_.stats().st_size / (number_of_tracks*bytes_per_sector));
	if(sectors_per_track_ != 13 && sectors_per_track_ != 16) throw Error::InvalidFormat;

	// Check whether this is a Pro DOS disk by inspecting the filename.
	if(sectors_per_track_ == 16) {
		size_t string_index = file_name.size()-1;
		while(file_name[string_index] != '.') {
			if(file_name[string_index] == 'p') {
				is_prodos_ = true;
				break;
			}
			--string_index;
		}
	}
}

HeadPosition AppleDSK::get_maximum_head_position() {
	return HeadPosition(number_of_tracks);
}

bool AppleDSK::get_is_read_only() {
	return file_.get_is_known_read_only();
}

long AppleDSK::file_offset(Track::Address address) {
	return address.position.as_int() * bytes_per_sector * sectors_per_track_;
}

size_t AppleDSK::logical_sector_for_physical_sector(size_t physical) {
	// DOS and Pro DOS interleave sectors on disk, and they're represented in a disk
	// image in physical order rather than logical.
	if(physical == 15) return 15;
	return (physical * (is_prodos_ ? 8 : 7)) % 15;
}

std::shared_ptr<Track> AppleDSK::get_track_at_position(Track::Address address) {
	std::vector<uint8_t> track_data;
	{
		std::lock_guard<std::mutex> lock_guard(file_.get_file_access_mutex());
		file_.seek(file_offset(address), SEEK_SET);
		track_data = file_.read(static_cast<size_t>(bytes_per_sector * sectors_per_track_));
	}

	Storage::Disk::PCMSegment segment;
	const uint8_t track = static_cast<uint8_t>(address.position.as_int());

	// In either case below, the code aims for exactly 50,000 bits per track.
	if(sectors_per_track_ == 16) {
		// Write gap 1.
		segment += Encodings::AppleGCR::six_and_two_sync(16);

		// Write the sectors.
		for(uint8_t c = 0; c < 16; ++c) {
			segment += Encodings::AppleGCR::header(254, track, c);
			segment += Encodings::AppleGCR::six_and_two_sync(7);	// Gap 2: 7 sync words.
			segment += Encodings::AppleGCR::six_and_two_data(&track_data[logical_sector_for_physical_sector(c) * 256]);
			segment += Encodings::AppleGCR::six_and_two_sync(16);	// Gap 3: 16 sync words.
		}
	} else {
		// TODO: 5 and 3, 13-sector format. If DSK actually supports it?
	}

	return std::make_shared<PCMTrack>(segment);
}

void AppleDSK::set_tracks(const std::map<Track::Address, std::shared_ptr<Track>> &tracks) {
	std::map<Track::Address, std::vector<uint8_t>> tracks_by_address;
	for(const auto &pair: tracks) {
		// Decode the track.
		auto sector_map = Storage::Encodings::AppleGCR::sectors_from_segment(
			Storage::Disk::track_serialisation(*pair.second, Storage::Time(1, 50000)));

		// Rearrange sectors into Apple DOS or Pro-DOS order.
		std::vector<uint8_t> track_contents(static_cast<size_t>(bytes_per_sector * sectors_per_track_));
		for(const auto &sector_pair: sector_map) {
			const size_t target_address = logical_sector_for_physical_sector(sector_pair.second.address.sector);
			memcpy(&track_contents[target_address*256], sector_pair.second.data.data(), bytes_per_sector);
		}

		// Store for later.
		tracks_by_address[pair.first] = std::move(track_contents);
	}

	// Grab the file lock and write out the new tracks.
	std::lock_guard<std::mutex> lock_guard(file_.get_file_access_mutex());
	for(const auto &pair: tracks_by_address) {
		file_.seek(file_offset(pair.first), SEEK_SET);
		file_.write(pair.second);
	}
}
