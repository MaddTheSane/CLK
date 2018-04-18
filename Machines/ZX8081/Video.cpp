//
//  Video.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 06/06/2017.
//  Copyright © 2017 Thomas Harte. All rights reserved.
//

#include "Video.hpp"

using namespace ZX8081;

namespace {

/*!
	The number of bytes of PCM data to allocate at once; if/when more are required,
	the class will simply allocate another batch.
*/
const std::size_t StandardAllocationSize = 40;

/// The amount of time a byte takes to output.
const std::size_t HalfCyclesPerByte = 8;

}

Video::Video() :
	crt_(new Outputs::CRT::CRT(207 * 2, 1, Outputs::CRT::DisplayType::PAL50, 1)) {

	// Set a composite sampling function that assumes 1bpp input.
	crt_->set_composite_sampling_function(
		"float composite_sample(usampler2D sampler, vec2 coordinate, vec2 icoordinate, float phase, float amplitude)"
		"{"
			"uint texValue = texture(sampler, coordinate).r;"
			"texValue <<= int(icoordinate.x * 8) & 7;"
			"return float(texValue & 128u);"
		"}");

	// Show only the centre 80% of the TV frame.
	crt_->set_video_signal(Outputs::CRT::VideoSignal::Composite);
	crt_->set_visible_area(Outputs::CRT::Rect(0.1f, 0.1f, 0.8f, 0.8f));
}

void Video::run_for(const HalfCycles half_cycles) {
	// Just keep a running total of the amount of time that remains owed to the CRT.
	cycles_since_update_ += static_cast<unsigned int>(half_cycles.as_int());
}

void Video::flush() {
	flush(sync_);
}

void Video::flush(bool next_sync) {
	if(sync_) {
		// If in sync, that takes priority. Output the proper amount of sync.
		crt_->output_sync(cycles_since_update_);
	} else {
		// If not presently in sync, then...

		if(line_data_) {
			// If there is output data queued, output it either if it's being interrupted by
			// sync, or if we're past its end anyway. Otherwise let it be.
			unsigned int data_length = static_cast<unsigned int>(line_data_pointer_ - line_data_) * HalfCyclesPerByte;
			if(data_length < cycles_since_update_ || next_sync) {
				unsigned int output_length = std::min(data_length, cycles_since_update_);
				crt_->output_data(output_length, HalfCyclesPerByte);
				line_data_pointer_ = line_data_ = nullptr;
				cycles_since_update_ -= output_length;
			} else return;
		}

		// Any pending pixels being dealt with, pad with the white level.
		uint8_t *colour_pointer = static_cast<uint8_t *>(crt_->allocate_write_area(1));
		if(colour_pointer) *colour_pointer = 0xff;
		crt_->output_level(cycles_since_update_);
	}

	cycles_since_update_ = 0;
}

void Video::set_sync(bool sync) {
	// Do nothing if sync hasn't changed.
	if(sync_ == sync) return;

	// Complete whatever was being drawn, and update sync.
	flush(sync);
	sync_ = sync;
}

void Video::output_byte(uint8_t byte) {
	// Complete whatever was going on.
	if(sync_) return;
	flush();

	// Grab a buffer if one isn't already available.
	if(!line_data_) {
		line_data_pointer_ = line_data_ = crt_->allocate_write_area(StandardAllocationSize);
	}

	// If a buffer was obtained, serialise the new pixels.
	if(line_data_) {
		// If the buffer is full, output it now and obtain a new one
		if(line_data_pointer_ - line_data_ == StandardAllocationSize) {
			crt_->output_data(StandardAllocationSize, HalfCyclesPerByte);
			cycles_since_update_ -= StandardAllocationSize * HalfCyclesPerByte;
			line_data_pointer_ = line_data_ = crt_->allocate_write_area(StandardAllocationSize);
			if(!line_data_) return;
		}

		line_data_pointer_[0] = byte;
		line_data_pointer_ ++;
	}
}

Outputs::CRT::CRT *Video::get_crt() {
	return crt_.get();
}
