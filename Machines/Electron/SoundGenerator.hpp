//
//  SoundGenerator.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 03/12/2016.
//  Copyright © 2016 Thomas Harte. All rights reserved.
//

#ifndef Electron_SoundGenerator_hpp
#define Electron_SoundGenerator_hpp

#include "../../Outputs/Speaker/Implementation/FilteringSpeaker.hpp"
#include "../../Concurrency/AsyncTaskQueue.hpp"

namespace Electron {

class SoundGenerator: public ::Outputs::Speaker::SampleSource {
	public:
		SoundGenerator(Concurrency::DeferringAsyncTaskQueue &audio_queue);

		void set_divider(uint8_t divider);

		void set_is_enabled(bool is_enabled);

		void get_samples(std::size_t number_of_samples, int16_t *target);
		void skip_samples(std::size_t number_of_samples);

		static const unsigned int clock_rate_divider = 8;

	private:
		Concurrency::DeferringAsyncTaskQueue &audio_queue_;
		unsigned int counter_ = 0;
		unsigned int divider_ = 0;
		bool is_enabled_ = false;
};

}

#endif /* Speaker_hpp */
