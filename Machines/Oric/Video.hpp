//
//  Video.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 12/10/2016.
//  Copyright © 2016 Thomas Harte. All rights reserved.
//

#ifndef Machines_Oric_Video_hpp
#define Machines_Oric_Video_hpp

#include "../../Outputs/CRT/CRT.hpp"
#include "../../ClockReceiver/ClockReceiver.hpp"

namespace Oric {

class VideoOutput {
	public:
		VideoOutput(uint8_t *memory);
		Outputs::CRT::CRT *get_crt();
		void run_for(const Cycles cycles);
		void set_colour_rom(const std::vector<uint8_t> &rom);
		void set_video_signal(Outputs::CRT::VideoSignal output_device);

	private:
		uint8_t *ram_;
		std::unique_ptr<Outputs::CRT::CRT> crt_;

		// Counters and limits
		int counter_ = 0, frame_counter_ = 0;
		int v_sync_start_position_, v_sync_end_position_, counter_period_;

		// Output target and device
		uint16_t *pixel_target_;
		uint16_t colour_forms_[8];
		Outputs::CRT::VideoSignal video_signal_;

		// Registers
		uint8_t ink_, paper_;

		int character_set_base_address_ = 0xb400;
		inline void set_character_set_base_address();

		bool is_graphics_mode_ = false;
		bool next_frame_is_sixty_hertz_ = false;
		bool use_alternative_character_set_;
		bool use_double_height_characters_;
		bool blink_text_;
};

}

#endif /* Video_hpp */
