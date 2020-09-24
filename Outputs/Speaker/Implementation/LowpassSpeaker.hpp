//
//  FilteringSpeaker.h
//  Clock Signal
//
//  Created by Thomas Harte on 15/12/2017.
//  Copyright 2017 Thomas Harte. All rights reserved.
//

#ifndef FilteringSpeaker_h
#define FilteringSpeaker_h

#include "../Speaker.hpp"
#include "../../../SignalProcessing/FIRFilter.hpp"
#include "../../../ClockReceiver/ClockReceiver.hpp"
#include "../../../Concurrency/AsyncTaskQueue.hpp"

#include <mutex>
#include <cstring>
#include <cmath>

namespace Outputs {
namespace Speaker {

/*!
	The low-pass speaker expects an Outputs::Speaker::SampleSource-derived
	template class, and uses the instance supplied to its constructor as the
	source of a high-frequency stream of audio which it filters down to a
	lower-frequency output.
*/
template <typename SampleSource> class LowpassSpeaker: public Speaker {
	public:
		LowpassSpeaker(SampleSource &sample_source) : sample_source_(sample_source) {
			// Propagate an initial volume level.
			sample_source.set_sample_volume_range(32767);
		}

		void set_output_volume(float volume) final {
			// Clamp to the acceptable range, and set.
			volume = std::min(std::max(0.0f, volume), 1.0f);
			sample_source_.set_sample_volume_range(int16_t(32767.0f * volume));
		}

		// Implemented as per Speaker.
		float get_ideal_clock_rate_in_range(float minimum, float maximum) final {
			std::lock_guard lock_guard(filter_parameters_mutex_);

			// return twice the cut off, if applicable
			if(	filter_parameters_.high_frequency_cutoff > 0.0f &&
				filter_parameters_.input_cycles_per_second >= filter_parameters_.high_frequency_cutoff * 3.0f &&
				filter_parameters_.input_cycles_per_second <= filter_parameters_.high_frequency_cutoff * 3.0f)
					return filter_parameters_.high_frequency_cutoff * 3.0f;

			// return exactly the input rate if possible
			if(	filter_parameters_.input_cycles_per_second >= minimum &&
				filter_parameters_.input_cycles_per_second <= maximum)
					return filter_parameters_.input_cycles_per_second;

			// if the input rate is lower, return the minimum
			if(filter_parameters_.input_cycles_per_second < minimum)
				return minimum;

			// otherwise, return the maximum
			return maximum;
		}

		// Implemented as per Speaker.
		void set_computed_output_rate(float cycles_per_second, int buffer_size, bool) final {
			std::lock_guard lock_guard(filter_parameters_mutex_);
			if(filter_parameters_.output_cycles_per_second == cycles_per_second && size_t(buffer_size) == output_buffer_.size()) {
				return;
			}

			filter_parameters_.output_cycles_per_second = cycles_per_second;
			filter_parameters_.parameters_are_dirty = true;
			output_buffer_.resize(std::size_t(buffer_size) * (SampleSource::get_is_stereo() ? 2 : 1));
		}

		bool get_is_stereo() final {
			return SampleSource::get_is_stereo();
		}

		/*!
			Sets the clock rate of the input audio.
		*/
		void set_input_rate(float cycles_per_second) {
			std::lock_guard lock_guard(filter_parameters_mutex_);
			if(filter_parameters_.input_cycles_per_second == cycles_per_second) {
				return;
			}
			filter_parameters_.input_cycles_per_second = cycles_per_second;
			filter_parameters_.parameters_are_dirty = true;
			filter_parameters_.input_rate_changed = true;
		}

		/*!
			Allows a cut-off frequency to be specified for audio. Ordinarily this low-pass speaker
			will determine a cut-off based on the output audio rate. A caller can manually select
			an alternative cut-off. This allows machines with a low-pass filter on their audio output
			path to be explicit about its effect, and get that simulation for free.
		*/
		void set_high_frequency_cutoff(float high_frequency) {
			std::lock_guard lock_guard(filter_parameters_mutex_);
			if(filter_parameters_.high_frequency_cutoff == high_frequency) {
				return;
			}
			filter_parameters_.high_frequency_cutoff = high_frequency;
			filter_parameters_.parameters_are_dirty = true;
		}

		/*!
			Schedules an advancement by the number of cycles specified on the provided queue.
			The speaker will advance by obtaining data from the sample source supplied
			at construction, filtering it and passing it on to the speaker's delegate if there is one.
		*/
		void run_for(Concurrency::DeferringAsyncTaskQueue &queue, const Cycles cycles) {
			queue.defer([this, cycles] {
				run_for(cycles);
			});
		}

	private:
		enum class Conversion {
			ResampleSmaller,
			Copy,
			ResampleLarger
		} conversion_ = Conversion::Copy;

		/*!
			Advances by the number of cycles specified, obtaining data from the sample source supplied
			at construction, filtering it and passing it on to the speaker's delegate if there is one.
		*/
		void run_for(const Cycles cycles) {
			const auto delegate = delegate_.load(std::memory_order::memory_order_relaxed);
			if(!delegate) return;

			const int scale = get_scale();

			std::size_t cycles_remaining = size_t(cycles.as_integral());
			if(!cycles_remaining) return;

			FilterParameters filter_parameters;
			{
				std::lock_guard lock_guard(filter_parameters_mutex_);
				filter_parameters = filter_parameters_;
				filter_parameters_.parameters_are_dirty = false;
				filter_parameters_.input_rate_changed = false;
			}
			if(filter_parameters.parameters_are_dirty) update_filter_coefficients(filter_parameters);
			if(filter_parameters.input_rate_changed) {
				delegate->speaker_did_change_input_clock(this);
			}

			switch(conversion_) {
				case Conversion::Copy:
					while(cycles_remaining) {
						const auto cycles_to_read = std::min((output_buffer_.size() - output_buffer_pointer_) / (SampleSource::get_is_stereo() ? 2 : 1), cycles_remaining);
						sample_source_.get_samples(cycles_to_read, &output_buffer_[output_buffer_pointer_ ]);
						output_buffer_pointer_ += cycles_to_read * (SampleSource::get_is_stereo() ? 2 : 1);

						// TODO: apply scale.

						// Announce to delegate if full.
						if(output_buffer_pointer_ == output_buffer_.size()) {
							output_buffer_pointer_ = 0;
							did_complete_samples(this, output_buffer_, SampleSource::get_is_stereo());
						}

						cycles_remaining -= cycles_to_read;
					}
				break;

				case Conversion::ResampleSmaller:
					while(cycles_remaining) {
						const auto cycles_to_read = std::min((input_buffer_.size() - input_buffer_depth_) / (SampleSource::get_is_stereo() ? 2 : 1), cycles_remaining);

						sample_source_.get_samples(cycles_to_read, &input_buffer_[input_buffer_depth_]);
						input_buffer_depth_ += cycles_to_read * (SampleSource::get_is_stereo() ? 2 : 1);

						if(input_buffer_depth_ == input_buffer_.size()) {
							resample_input_buffer(scale);
						}

						cycles_remaining -= cycles_to_read;
					}
				break;

				case Conversion::ResampleLarger:
					// TODO: input rate is less than output rate.
				break;
			}
		}

		SampleSource &sample_source_;

		std::size_t output_buffer_pointer_ = 0;
		std::size_t input_buffer_depth_ = 0;
		std::vector<int16_t> input_buffer_;
		std::vector<int16_t> output_buffer_;

		float step_rate_ = 0.0f;
		float position_error_ = 0.0f;
		std::unique_ptr<SignalProcessing::FIRFilter> filter_;

		std::mutex filter_parameters_mutex_;
		struct FilterParameters {
			float input_cycles_per_second = 0.0f;
			float output_cycles_per_second = 0.0f;
			float high_frequency_cutoff = -1.0;

			bool parameters_are_dirty = true;
			bool input_rate_changed = false;
		} filter_parameters_;

		void update_filter_coefficients(const FilterParameters &filter_parameters) {
			float high_pass_frequency = filter_parameters.output_cycles_per_second / 2.0f;
			if(filter_parameters.high_frequency_cutoff > 0.0) {
				high_pass_frequency = std::min(filter_parameters.high_frequency_cutoff, high_pass_frequency);
			}

			// Make a guess at a good number of taps.
			std::size_t number_of_taps = std::size_t(
				ceilf((filter_parameters.input_cycles_per_second + high_pass_frequency) / high_pass_frequency)
			);
			number_of_taps = (number_of_taps * 2) | 1;

			step_rate_ = filter_parameters.input_cycles_per_second / filter_parameters.output_cycles_per_second;
			position_error_ = 0.0f;

			filter_ = std::make_unique<SignalProcessing::FIRFilter>(
				unsigned(number_of_taps),
				filter_parameters.input_cycles_per_second,
				0.0,
				high_pass_frequency,
				SignalProcessing::FIRFilter::DefaultAttenuation);


			// Pick the new conversion function.
			if(	filter_parameters.input_cycles_per_second == filter_parameters.output_cycles_per_second &&
				filter_parameters.high_frequency_cutoff < 0.0) {
				// If input and output rates exactly match, and no additional cut-off has been specified,
				// just accumulate results and pass on.
				conversion_ = Conversion::Copy;
			} else if(	filter_parameters.input_cycles_per_second > filter_parameters.output_cycles_per_second ||
				(filter_parameters.input_cycles_per_second == filter_parameters.output_cycles_per_second && filter_parameters.high_frequency_cutoff >= 0.0)) {
				// If the output rate is less than the input rate, or an additional cut-off has been specified, use the filter.
				conversion_ = Conversion::ResampleSmaller;
			} else {
				conversion_ = Conversion::ResampleLarger;
			}

			// Do something sensible with any dangling input, if necessary.
			const int scale = get_scale();
			switch(conversion_) {
				// Neither direct copying nor resampling larger currently use any temporary input.
				// Although in the latter case that's just because it's unimplemented. But, regardless,
				// that means nothing to do.
				default: break;

				case Conversion::ResampleSmaller: {
					// Reize the input buffer only if absolutely necessary; if sizing downward
					// such that a sample would otherwise be lost then output it now. Keep anything
					// currently in the input buffer that hasn't yet been processed.
					const size_t required_buffer_size = size_t(number_of_taps) * (SampleSource::get_is_stereo() ? 2 : 1);
					if(input_buffer_.size() != required_buffer_size) {
						if(input_buffer_depth_ >= required_buffer_size) {
							resample_input_buffer(scale);
							input_buffer_depth_ %= required_buffer_size;
						}
						input_buffer_.resize(required_buffer_size);
					}
				} break;
			}
		}

		inline void resample_input_buffer(int scale) {
			if constexpr (SampleSource::get_is_stereo()) {
				output_buffer_[output_buffer_pointer_ + 0] = filter_->apply(input_buffer_.data(), 2);
				output_buffer_[output_buffer_pointer_ + 1] = filter_->apply(input_buffer_.data() + 1, 2);
				output_buffer_pointer_+= 2;
			} else {
				output_buffer_[output_buffer_pointer_] = filter_->apply(input_buffer_.data());
				output_buffer_pointer_++;
			}

			// Apply scale, if supplied, clamping appropriately.
			if(scale != 65536) {
				#define SCALE(x) x = int16_t(std::max(std::min((int(x) * scale) >> 16, 32767), -32768))
				if constexpr (SampleSource::get_is_stereo()) {
					SCALE(output_buffer_[output_buffer_pointer_ - 2]);
					SCALE(output_buffer_[output_buffer_pointer_ - 1]);
				} else {
					SCALE(output_buffer_[output_buffer_pointer_ - 1]);
				}
				#undef SCALE
			}

			// Announce to delegate if full.
			if(output_buffer_pointer_ == output_buffer_.size()) {
				output_buffer_pointer_ = 0;
				did_complete_samples(this, output_buffer_, SampleSource::get_is_stereo());
			}

			// If the next loop around is going to reuse some of the samples just collected, use a memmove to
			// preserve them in the correct locations (TODO: use a longer buffer to fix that?) and don't skip
			// anything. Otherwise skip as required to get to the next sample batch and don't expect to reuse.
			const size_t steps = size_t(step_rate_ + position_error_) * (SampleSource::get_is_stereo() ? 2 : 1);
			position_error_ = fmodf(step_rate_ + position_error_, 1.0f);
			if(steps < input_buffer_.size()) {
				auto *const input_buffer = input_buffer_.data();
				std::memmove(	input_buffer,
								&input_buffer[steps],
								sizeof(int16_t) * (input_buffer_.size() - steps));
				input_buffer_depth_ -= steps;
			} else {
				if(steps > input_buffer_.size()) {
					sample_source_.skip_samples((steps - input_buffer_.size()) / (SampleSource::get_is_stereo() ? 2 : 1));
				}
				input_buffer_depth_ = 0;
			}
		}

		int get_scale() {
			return int(65536.0 / sample_source_.get_average_output_peak());
		};
};

}
}

#endif /* FilteringSpeaker_h */
