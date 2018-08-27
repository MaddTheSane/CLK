//
//  ColecoVision.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 23/02/2018.
//  Copyright 2018 Thomas Harte. All rights reserved.
//

#include "ColecoVision.hpp"

#include "../../Processors/Z80/Z80.hpp"

#include "../../Components/9918/9918.hpp"
#include "../../Components/AY38910/AY38910.hpp"	// For the Super Game Module.
#include "../../Components/SN76489/SN76489.hpp"

#include "../CRTMachine.hpp"
#include "../JoystickMachine.hpp"

#include "../../ClockReceiver/ForceInline.hpp"

#include "../../Outputs/Speaker/Implementation/CompoundSource.hpp"
#include "../../Outputs/Speaker/Implementation/LowpassSpeaker.hpp"

#include "../../Analyser/Dynamic/ConfidenceCounter.hpp"

namespace {
const int sn76489_divider = 2;
}

namespace Coleco {
namespace Vision {

class Joystick: public Inputs::ConcreteJoystick {
	public:
		Joystick() :
			ConcreteJoystick({
				Input(Input::Up),
				Input(Input::Down),
				Input(Input::Left),
				Input(Input::Right),

				Input(Input::Fire, 0),
				Input(Input::Fire, 1),

				Input('0'),	Input('1'),	Input('2'),
				Input('3'),	Input('4'),	Input('5'),
				Input('6'),	Input('7'),	Input('8'),
				Input('9'),	Input('*'),	Input('#'),
			}) {}

		void did_set_input(const Input &digital_input, bool is_active) override {
			switch(digital_input.type) {
				default: return;

				case Input::Key:
					if(!is_active) keypad_ |= 0xf;
					else {
						uint8_t mask = 0xf;
						switch(digital_input.info.key.symbol) {
							case '8':	mask = 0x1;		break;
							case '4':	mask = 0x2;		break;
							case '5':	mask = 0x3;		break;
							case '7':	mask = 0x5;		break;
							case '#':	mask = 0x6;		break;
							case '2':	mask = 0x7;		break;
							case '*':	mask = 0x9;		break;
							case '0':	mask = 0xa;		break;
							case '9':	mask = 0xb;		break;
							case '3':	mask = 0xc;		break;
							case '1':	mask = 0xd;		break;
							case '6':	mask = 0xe;		break;
							default: break;
						}
						keypad_ = (keypad_ & 0xf0) | mask;
					}
				break;

				case Input::Up: 	if(is_active) direction_ &= ~0x01; else direction_ |= 0x01;	break;
				case Input::Right:	if(is_active) direction_ &= ~0x02; else direction_ |= 0x02;	break;
				case Input::Down:	if(is_active) direction_ &= ~0x04; else direction_ |= 0x04;	break;
				case Input::Left:	if(is_active) direction_ &= ~0x08; else direction_ |= 0x08;	break;
				case Input::Fire:
					switch(digital_input.info.control.index) {
						default: break;
						case 0:	if(is_active) direction_ &= ~0x40; else direction_ |= 0x40;	break;
						case 1:	if(is_active) keypad_ &= ~0x40; else keypad_ |= 0x40;		break;
					}
				break;
			}
		}

		uint8_t get_direction_input() {
			return direction_;
		}

		uint8_t get_keypad_input() {
			return keypad_;
		}

	private:
		uint8_t direction_ = 0xff;
		uint8_t keypad_ = 0xff;
};

class ConcreteMachine:
	public Machine,
	public CPU::Z80::BusHandler,
	public CRTMachine::Machine,
	public JoystickMachine::Machine {

	public:
		ConcreteMachine(const Analyser::Static::Target &target, const ROMMachine::ROMFetcher &rom_fetcher) :
			z80_(*this),
			sn76489_(TI::SN76489::Personality::SN76489, audio_queue_, sn76489_divider),
			ay_(audio_queue_),
			mixer_(sn76489_, ay_),
			speaker_(mixer_) {
			speaker_.set_input_rate(3579545.0f / static_cast<float>(sn76489_divider));
			set_clock_rate(3579545);
			joysticks_.emplace_back(new Joystick);
			joysticks_.emplace_back(new Joystick);

			const auto roms = rom_fetcher(
				"ColecoVision",
				{ "coleco.rom" });

			if(!roms[0]) {
				throw ROMMachine::Error::MissingROMs;
			}

			bios_ = *roms[0];
			bios_.resize(8192);

			if(!target.media.cartridges.empty()) {
				const auto &segment = target.media.cartridges.front()->get_segments().front();
				cartridge_ = segment.data;
				if(cartridge_.size() >= 32768)
					cartridge_address_limit_ = 0xffff;
				else
					cartridge_address_limit_ = static_cast<uint16_t>(0x8000 + cartridge_.size() - 1);

				if(cartridge_.size() > 32768) {
					cartridge_pages_[0] = &cartridge_[cartridge_.size() - 16384];
					cartridge_pages_[1] = cartridge_.data();
					is_megacart_ = true;
				} else {
					cartridge_pages_[0] = cartridge_.data();
					cartridge_pages_[1] = cartridge_.data() + 16384;
					is_megacart_ = false;
				}
			}
		}

		~ConcreteMachine() {
			audio_queue_.flush();
		}

		std::vector<std::unique_ptr<Inputs::Joystick>> &get_joysticks() override {
			return joysticks_;
		}

		void setup_output(float aspect_ratio) override {
			vdp_.reset(new TI::TMS9918(TI::TMS9918::TMS9918A));
			get_crt()->set_video_signal(Outputs::CRT::VideoSignal::Composite);
		}

		void close_output() override {
			vdp_.reset();
		}

		Outputs::CRT::CRT *get_crt() override {
			return vdp_->get_crt();
		}

		Outputs::Speaker::Speaker *get_speaker() override {
			return &speaker_;
		}

		void run_for(const Cycles cycles) override {
			z80_.run_for(cycles);
		}

		// MARK: Z80::BusHandler
		forceinline HalfCycles perform_machine_cycle(const CPU::Z80::PartialMachineCycle &cycle) {
			// The SN76489 will use its ready line to trigger the Z80's wait for three
			// cycles when accessed. Everything else runs at full speed. Short-circuit
			// that whole piece of communications by just accruing the time here if applicable.
			const HalfCycles penalty(
				(
					cycle.operation == CPU::Z80::PartialMachineCycle::Output &&
					((*cycle.address >> 5) & 7) == 7
				) ? 6 : 0
			);
			const HalfCycles length = cycle.length + penalty;

			time_since_vdp_update_ += length;
			time_since_sn76489_update_ += length;

			uint16_t address = cycle.address ? *cycle.address : 0x0000;
			switch(cycle.operation) {
				case CPU::Z80::PartialMachineCycle::ReadOpcode:
					if(!address) pc_zero_accesses_++;
				case CPU::Z80::PartialMachineCycle::Read:
					if(address < 0x2000) {
						if(super_game_module_.replace_bios) {
							*cycle.value = super_game_module_.ram[address];
						} else {
							*cycle.value = bios_[address];
						}
					} else if(super_game_module_.replace_ram && address < 0x8000) {
						*cycle.value = super_game_module_.ram[address];
					} else if(address >= 0x6000 && address < 0x8000) {
						*cycle.value = ram_[address & 1023];
					} else if(address >= 0x8000 && address <= cartridge_address_limit_) {
						if(is_megacart_ && address >= 0xffc0) {
							page_megacart(address);
						}
						*cycle.value = cartridge_pages_[(address >> 14)&1][address&0x3fff];
					} else {
						*cycle.value = 0xff;
					}
				break;

				case CPU::Z80::PartialMachineCycle::Write:
					if(super_game_module_.replace_bios && address < 0x2000) {
						super_game_module_.ram[address] = *cycle.value;
					} else if(super_game_module_.replace_ram && address >= 0x2000 && address < 0x8000) {
						super_game_module_.ram[address] = *cycle.value;
					} else if(address >= 0x6000 && address < 0x8000) {
						ram_[address & 1023] = *cycle.value;
					} else if(is_megacart_ && address >= 0xffc0) {
						page_megacart(address);
					}
				break;

				case CPU::Z80::PartialMachineCycle::Input:
					switch((address >> 5) & 7) {
						case 5:
							update_video();
							*cycle.value = vdp_->get_register(address);
							z80_.set_non_maskable_interrupt_line(vdp_->get_interrupt_line());
							time_until_interrupt_ = vdp_->get_time_until_interrupt();
						break;

						case 7: {
							const std::size_t joystick_id = (address&2) >> 1;
							Joystick *joystick = static_cast<Joystick *>(joysticks_[joystick_id].get());
							if(joysticks_in_keypad_mode_) {
								*cycle.value = joystick->get_keypad_input();
							} else {
								*cycle.value = joystick->get_direction_input();
							}

							// Hitting exactly the recommended joypad input port is an indicator that
							// this really is a ColecoVision game. The BIOS won't do this when just waiting
							// to start a game (unlike accessing the VDP and SN).
							if((address&0xfc) == 0xfc) confidence_counter_.add_hit();
						} break;

						default:
							switch(address&0xff) {
								default: *cycle.value = 0xff; break;
								case 0x52:
									// Read AY data.
									update_audio();
									ay_.set_control_lines(GI::AY38910::ControlLines(GI::AY38910::BC2 | GI::AY38910::BC1));
									*cycle.value = ay_.get_data_output();
									ay_.set_control_lines(GI::AY38910::ControlLines(0));
								break;
							}
						break;
					}
				break;

				case CPU::Z80::PartialMachineCycle::Output: {
					const int eighth = (address >> 5) & 7;
					switch(eighth) {
						case 4: case 6:
							joysticks_in_keypad_mode_ = eighth == 4;
						break;

						case 5:
							update_video();
							vdp_->set_register(address, *cycle.value);
							z80_.set_non_maskable_interrupt_line(vdp_->get_interrupt_line());
							time_until_interrupt_ = vdp_->get_time_until_interrupt();
						break;

						case 7:
							update_audio();
							sn76489_.set_register(*cycle.value);
						break;

						default:
							// Catch Super Game Module accesses; it decodes more thoroughly.
							switch(address&0xff) {
								default: break;
								case 0x7f:
									super_game_module_.replace_bios = !((*cycle.value)&0x2);
								break;
								case 0x50:
									// Set AY address.
									update_audio();
									ay_.set_control_lines(GI::AY38910::BC1);
									ay_.set_data_input(*cycle.value);
									ay_.set_control_lines(GI::AY38910::ControlLines(0));
								break;
								case 0x51:
									// Set AY data.
									update_audio();
									ay_.set_control_lines(GI::AY38910::ControlLines(GI::AY38910::BC2 | GI::AY38910::BDIR));
									ay_.set_data_input(*cycle.value);
									ay_.set_control_lines(GI::AY38910::ControlLines(0));
								break;
								case 0x53:
									super_game_module_.replace_ram = !!((*cycle.value)&0x1);
								break;
							}
						break;
					}
				} break;

				default: break;
			}

			if(time_until_interrupt_ > 0) {
				time_until_interrupt_ -= length;
				if(time_until_interrupt_ <= HalfCycles(0)) {
					z80_.set_non_maskable_interrupt_line(true, time_until_interrupt_);
				}
			}

			return penalty;
		}

		void flush() {
			update_video();
			update_audio();
			audio_queue_.perform();
		}

		float get_confidence() override {
			if(pc_zero_accesses_ > 1) return 0.0f;
			return confidence_counter_.get_confidence();
		}

	private:
		inline void page_megacart(uint16_t address) {
			const std::size_t selected_start = (static_cast<std::size_t>(address&63) << 14) % cartridge_.size();
			cartridge_pages_[1] = &cartridge_[selected_start];
		}
		inline void update_audio() {
			speaker_.run_for(audio_queue_, time_since_sn76489_update_.divide_cycles(Cycles(sn76489_divider)));
		}
		inline void update_video() {
			vdp_->run_for(time_since_vdp_update_.flush());
		}

		CPU::Z80::Processor<ConcreteMachine, false, false> z80_;
		std::unique_ptr<TI::TMS9918> vdp_;

		Concurrency::DeferringAsyncTaskQueue audio_queue_;
		TI::SN76489 sn76489_;
		GI::AY38910::AY38910 ay_;
		Outputs::Speaker::CompoundSource<TI::SN76489, GI::AY38910::AY38910> mixer_;
		Outputs::Speaker::LowpassSpeaker<Outputs::Speaker::CompoundSource<TI::SN76489, GI::AY38910::AY38910>> speaker_;

		std::vector<uint8_t> bios_;
		std::vector<uint8_t> cartridge_;
		uint8_t *cartridge_pages_[2];
		uint8_t ram_[1024];
		bool is_megacart_ = false;
		uint16_t cartridge_address_limit_ = 0;
		struct {
			bool replace_bios = false;
			bool replace_ram = false;
			uint8_t ram[32768];
		} super_game_module_;

		std::vector<std::unique_ptr<Inputs::Joystick>> joysticks_;
		bool joysticks_in_keypad_mode_ = false;

		HalfCycles time_since_vdp_update_;
		HalfCycles time_since_sn76489_update_;
		HalfCycles time_until_interrupt_;

		Analyser::Dynamic::ConfidenceCounter confidence_counter_;
		int pc_zero_accesses_ = 0;
};

}
}

using namespace Coleco::Vision;

Machine *Machine::ColecoVision(const Analyser::Static::Target *target, const ROMMachine::ROMFetcher &rom_fetcher) {
	return new ConcreteMachine(*target, rom_fetcher);
}

Machine::~Machine() {}
