//
//  MFM.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 18/09/2016.
//  Copyright 2016 Thomas Harte. All rights reserved.
//

#include "Encoder.hpp"

#include "Constants.hpp"
#include "../../Track/PCMTrack.hpp"
#include "../../../../NumberTheory/CRC.hpp"

#include <set>

using namespace Storage::Encodings::MFM;

class MFMEncoder: public Encoder {
	public:
		MFMEncoder(std::vector<bool> &target) : Encoder(target) {}
		virtual ~MFMEncoder() {}

		void add_byte(uint8_t input) {
			crc_generator_.add(input);
			uint16_t spread_value =
				static_cast<uint16_t>(
					((input & 0x01) << 0) |
					((input & 0x02) << 1) |
					((input & 0x04) << 2) |
					((input & 0x08) << 3) |
					((input & 0x10) << 4) |
					((input & 0x20) << 5) |
					((input & 0x40) << 6) |
					((input & 0x80) << 7)
				);
			uint16_t or_bits = static_cast<uint16_t>((spread_value << 1) | (spread_value >> 1) | (last_output_ << 15));
			uint16_t output = spread_value | ((~or_bits) & 0xaaaa);
			output_short(output);
		}

		void add_index_address_mark() {
			for(int c = 0; c < 3; c++) output_short(MFMIndexSync);
			add_byte(IndexAddressByte);
		}

		void add_ID_address_mark() {
			output_sync();
			add_byte(IDAddressByte);
		}

		void add_data_address_mark() {
			output_sync();
			add_byte(DataAddressByte);
		}

		void add_deleted_data_address_mark() {
			output_sync();
			add_byte(DeletedDataAddressByte);
		}

	private:
		uint16_t last_output_;
		void output_short(uint16_t value) {
			last_output_ = value;
			Encoder::output_short(value);
		}

		void output_sync() {
			for(int c = 0; c < 3; c++) output_short(MFMSync);
			crc_generator_.set_value(MFMPostSyncCRCValue);
		}
};

class FMEncoder: public Encoder {
	// encodes each 16-bit part as clock, data, clock, data [...]
	public:
		FMEncoder(std::vector<bool> &target) : Encoder(target) {}

		void add_byte(uint8_t input) {
			crc_generator_.add(input);
			output_short(
				static_cast<uint16_t>(
					((input & 0x01) << 0) |
					((input & 0x02) << 1) |
					((input & 0x04) << 2) |
					((input & 0x08) << 3) |
					((input & 0x10) << 4) |
					((input & 0x20) << 5) |
					((input & 0x40) << 6) |
					((input & 0x80) << 7) |
					0xaaaa
				));
		}

		void add_index_address_mark() {
			crc_generator_.reset();
			crc_generator_.add(IndexAddressByte);
			output_short(FMIndexAddressMark);
		}

		void add_ID_address_mark() {
			crc_generator_.reset();
			crc_generator_.add(IDAddressByte);
			output_short(FMIDAddressMark);
		}

		void add_data_address_mark() {
			crc_generator_.reset();
			crc_generator_.add(DataAddressByte);
			output_short(FMDataAddressMark);
		}

		void add_deleted_data_address_mark() {
			crc_generator_.reset();
			crc_generator_.add(DeletedDataAddressByte);
			output_short(FMDeletedDataAddressMark);
		}
};

template<class T> std::shared_ptr<Storage::Disk::Track>
		GetTrackWithSectors(
			const std::vector<const Sector *> &sectors,
			std::size_t post_index_address_mark_bytes, uint8_t post_index_address_mark_value,
			std::size_t pre_address_mark_bytes,
			std::size_t post_address_mark_bytes, uint8_t post_address_mark_value,
			std::size_t pre_data_mark_bytes,
			std::size_t post_data_bytes, uint8_t post_data_value,
			std::size_t expected_track_bytes) {
	Storage::Disk::PCMSegment segment;
	segment.data.reserve(expected_track_bytes * 8);
	T shifter(segment.data);

	// output the index mark
	shifter.add_index_address_mark();

	// add the post-index mark
	for(std::size_t c = 0; c < post_index_address_mark_bytes; c++) shifter.add_byte(post_index_address_mark_value);

	// add sectors
	for(const Sector *sector : sectors) {
		// gap
		for(std::size_t c = 0; c < pre_address_mark_bytes; c++) shifter.add_byte(0x00);

		// sector header
		shifter.add_ID_address_mark();
		shifter.add_byte(sector->address.track);
		shifter.add_byte(sector->address.side);
		shifter.add_byte(sector->address.sector);
		shifter.add_byte(sector->size);
		shifter.add_crc(sector->has_header_crc_error);

		// gap
		for(std::size_t c = 0; c < post_address_mark_bytes; c++) shifter.add_byte(post_address_mark_value);
		for(std::size_t c = 0; c < pre_data_mark_bytes; c++) shifter.add_byte(0x00);

		// data, if attached
		// TODO: allow for weak/fuzzy data.
		if(!sector->samples.empty()) {
			if(sector->is_deleted)
				shifter.add_deleted_data_address_mark();
			else
				shifter.add_data_address_mark();

			std::size_t c = 0;
			std::size_t declared_length = static_cast<std::size_t>(128 << sector->size);
			for(c = 0; c < sector->samples[0].size() && c < declared_length; c++) {
				shifter.add_byte(sector->samples[0][c]);
			}
			for(; c < declared_length; c++) {
				shifter.add_byte(0x00);
			}
			shifter.add_crc(sector->has_data_crc_error);
		}

		// gap
		for(std::size_t c = 0; c < post_data_bytes; c++) shifter.add_byte(post_data_value);
	}

	while(segment.data.size() < expected_track_bytes*8) shifter.add_byte(0x00);

	// Allow the amount of data written to be up to 10% more than the expected size. Which is generous.
	const std::size_t max_size = (expected_track_bytes + (expected_track_bytes / 10)) * 8;
	if(segment.data.size() > max_size) segment.data.resize(max_size);

	return std::shared_ptr<Storage::Disk::Track>(new Storage::Disk::PCMTrack(std::move(segment)));
}

Encoder::Encoder(std::vector<bool> &target) :
	target_(target) {}

void Encoder::output_short(uint16_t value) {
	uint16_t mask = 0x8000;
	while(mask) {
		target_.push_back(!!(value & mask));
		mask >>= 1;
	}
}

void Encoder::add_crc(bool incorrectly) {
	uint16_t crc_value = crc_generator_.get_value();
	add_byte(crc_value >> 8);
	add_byte((crc_value & 0xff) ^ (incorrectly ? 1 : 0));
}

const std::size_t Storage::Encodings::MFM::DefaultSectorGapLength = std::numeric_limits<std::size_t>::max();

static std::vector<const Sector *> sector_pointers(const std::vector<Sector> &sectors) {
	std::vector<const Sector *> pointers;
	for(const Sector &sector: sectors) {
		pointers.push_back(&sector);
	}
	return pointers;
}

std::shared_ptr<Storage::Disk::Track> Storage::Encodings::MFM::GetFMTrackWithSectors(const std::vector<Sector> &sectors, std::size_t sector_gap_length, uint8_t sector_gap_filler_byte) {
	return GetTrackWithSectors<FMEncoder>(
		sector_pointers(sectors),
		26, 0xff,
		6,
		11, 0xff,
		6,
		(sector_gap_length != DefaultSectorGapLength) ? sector_gap_length : 27, 0xff,
		6250);	// i.e. 250kbps (including clocks) * 60 = 15000kpm, at 300 rpm => 50 kbits/rotation => 6250 bytes/rotation
}

std::shared_ptr<Storage::Disk::Track> Storage::Encodings::MFM::GetFMTrackWithSectors(const std::vector<const Sector *> &sectors, std::size_t sector_gap_length, uint8_t sector_gap_filler_byte) {
	return GetTrackWithSectors<FMEncoder>(
		sectors,
		26, 0xff,
		6,
		11, 0xff,
		6,
		(sector_gap_length != DefaultSectorGapLength) ? sector_gap_length : 27, 0xff,
		6250);	// i.e. 250kbps (including clocks) * 60 = 15000kpm, at 300 rpm => 50 kbits/rotation => 6250 bytes/rotation
}

std::shared_ptr<Storage::Disk::Track> Storage::Encodings::MFM::GetMFMTrackWithSectors(const std::vector<Sector> &sectors, std::size_t sector_gap_length, uint8_t sector_gap_filler_byte) {
	return GetTrackWithSectors<MFMEncoder>(
		sector_pointers(sectors),
		50, 0x4e,
		12,
		22, 0x4e,
		12,
		(sector_gap_length != DefaultSectorGapLength) ? sector_gap_length : 54, 0xff,
		12500);	// unintelligently: double the single-density bytes/rotation (or: 500kbps @ 300 rpm)
}

std::shared_ptr<Storage::Disk::Track> Storage::Encodings::MFM::GetMFMTrackWithSectors(const std::vector<const Sector *> &sectors, std::size_t sector_gap_length, uint8_t sector_gap_filler_byte) {
	return GetTrackWithSectors<MFMEncoder>(
		sectors,
		50, 0x4e,
		12,
		22, 0x4e,
		12,
		(sector_gap_length != DefaultSectorGapLength) ? sector_gap_length : 54, 0xff,
		12500);	// unintelligently: double the single-density bytes/rotation (or: 500kbps @ 300 rpm)
}

std::unique_ptr<Encoder> Storage::Encodings::MFM::GetMFMEncoder(std::vector<bool> &target) {
	return std::unique_ptr<Encoder>(new MFMEncoder(target));
}

std::unique_ptr<Encoder> Storage::Encodings::MFM::GetFMEncoder(std::vector<bool> &target) {
	return std::unique_ptr<Encoder>(new FMEncoder(target));
}
