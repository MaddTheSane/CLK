//
//  AudioToggle.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 17/04/2018.
//  Copyright 2018 Thomas Harte. All rights reserved.
//

#include "AudioToggle.hpp"

using namespace Audio;

Audio::Toggle::Toggle(Concurrency::DeferringAsyncTaskQueue &audio_queue) :
	audio_queue_(audio_queue) {}

void Toggle::get_samples(std::size_t number_of_samples, std::int16_t *target) {
	for(std::size_t sample = 0; sample < number_of_samples; ++sample) {
		target[sample] = level_;
	}
}

void Toggle::set_sample_volume_range(std::int16_t range) {
	volume_ = range;
}

void Toggle::skip_samples(const std::size_t number_of_samples) {}

void Toggle::set_output(bool enabled) {
	if(is_enabled_ == enabled) return;
	is_enabled_ = enabled;
	audio_queue_.defer([=] {
		level_ = enabled ? volume_ : 0;
	});
}

bool Toggle::get_output() {
	return is_enabled_;
}
