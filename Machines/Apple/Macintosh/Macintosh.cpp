//
//  Macintosh.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 03/05/2019.
//  Copyright © 2019 Thomas Harte. All rights reserved.
//

#include "Macintosh.hpp"

#include <array>

#include "DeferredAudio.hpp"
#include "DriveSpeedAccumulator.hpp"
#include "Keyboard.hpp"
#include "RealTimeClock.hpp"
#include "Video.hpp"

#include "../../../Activity/Source.hpp"
#include "../../CRTMachine.hpp"
#include "../../KeyboardMachine.hpp"
#include "../../MediaTarget.hpp"
#include "../../MouseMachine.hpp"

#include "../../../Inputs/QuadratureMouse/QuadratureMouse.hpp"
#include "../../../Outputs/Log.hpp"

#include "../../../ClockReceiver/JustInTime.hpp"

//#define LOG_TRACE

#include "../../../Components/6522/6522.hpp"
#include "../../../Components/8530/z8530.hpp"
#include "../../../Components/DiskII/IWM.hpp"
#include "../../../Components/DiskII/MacintoshDoubleDensityDrive.hpp"
#include "../../../Processors/68000/68000.hpp"

#include "../../../Analyser/Static/Macintosh/Target.hpp"

#include "../../Utility/MemoryPacker.hpp"
#include "../../Utility/MemoryFuzzer.hpp"

namespace {

const int CLOCK_RATE = 7833600;

}

namespace Apple {
namespace Macintosh {

template <Analyser::Static::Macintosh::Target::Model model> class ConcreteMachine:
	public Machine,
	public CRTMachine::Machine,
	public MediaTarget::Machine,
	public MouseMachine::Machine,
	public CPU::MC68000::BusHandler,
	public KeyboardMachine::MappedMachine,
	public Zilog::SCC::z8530::Delegate,
	public Activity::Source {
	public:
		using Target = Analyser::Static::Macintosh::Target;

		ConcreteMachine(const Target &target, const ROMMachine::ROMFetcher &rom_fetcher) :
		 	mc68000_(*this),
		 	iwm_(CLOCK_RATE),
		 	video_(ram_, audio_, drive_speed_accumulator_),
		 	via_(via_port_handler_),
		 	via_port_handler_(*this, clock_, keyboard_, video_, audio_, iwm_, mouse_),
		 	drives_{
		 		{CLOCK_RATE, model >= Analyser::Static::Macintosh::Target::Model::Mac512ke},
		 		{CLOCK_RATE, model >= Analyser::Static::Macintosh::Target::Model::Mac512ke}
			},
			mouse_(1) {

			// Select a ROM name and determine the proper ROM and RAM sizes
			// based on the machine model.
			using Model = Analyser::Static::Macintosh::Target::Model;
			const std::string machine_name = "Macintosh";
			uint32_t ram_size, rom_size;
			std::vector<ROMMachine::ROM> rom_descriptions;
			switch(model) {
				default:
				case Model::Mac128k:
					ram_size = 128*1024;
					rom_size = 64*1024;
					rom_descriptions.emplace_back(machine_name, "the Macintosh 128k ROM", "mac128k.rom", 64*1024, 0x6d0c8a28);
				break;
				case Model::Mac512k:
					ram_size = 512*1024;
					rom_size = 64*1024;
					rom_descriptions.emplace_back(machine_name, "the Macintosh 512k ROM", "mac512k.rom", 64*1024, 0xcf759e0d);
				break;
				case Model::Mac512ke:
				case Model::MacPlus: {
					ram_size = 512*1024;
					rom_size = 128*1024;
					const std::initializer_list<uint32_t> crc32s = { 0x4fa5b399, 0x7cacd18f, 0xb2102e8e };
					rom_descriptions.emplace_back(machine_name, "the Macintosh Plus ROM", "macplus.rom", 128*1024, crc32s);
				} break;
			}
			ram_mask_ = (ram_size >> 1) - 1;
			rom_mask_ = (rom_size >> 1) - 1;
			video_.set_ram_mask(ram_mask_);

			// Grab a copy of the ROM and convert it into big-endian data.
			const auto roms = rom_fetcher(rom_descriptions);
			if(!roms[0]) {
				throw ROMMachine::Error::MissingROMs;
			}
			roms[0]->resize(rom_size);
			Memory::PackBigEndian16(*roms[0], rom_);

			// Randomise memory contents.
			Memory::Fuzz(ram_, sizeof(ram_) / sizeof(*ram_));

			// Attach the drives to the IWM.
			iwm_.iwm.set_drive(0, &drives_[0]);
			iwm_.iwm.set_drive(1, &drives_[1]);

			// If they are 400kb drives, also attach them to the drive-speed accumulator.
			if(!drives_[0].is_800k()) drive_speed_accumulator_.add_drive(&drives_[0]);
			if(!drives_[1].is_800k()) drive_speed_accumulator_.add_drive(&drives_[1]);

			// Make sure interrupt changes from the SCC are observed.
			scc_.set_delegate(this);

			// The Mac runs at 7.8336mHz.
			set_clock_rate(double(CLOCK_RATE));
			audio_.speaker.set_input_rate(float(CLOCK_RATE) / 2.0f);

			// Insert any supplied media.
			insert_media(target.media);

			// Set the immutables of the memory map.
			setup_memory_map();
		}

		~ConcreteMachine() {
			audio_.queue.flush();
		}

		void set_scan_target(Outputs::Display::ScanTarget *scan_target) override {
			video_.set_scan_target(scan_target);
		}

		Outputs::Speaker::Speaker *get_speaker() override {
			return &audio_.speaker;
		}

		void run_for(const Cycles cycles) override {
			mc68000_.run_for(cycles);
		}

		using Microcycle = CPU::MC68000::Microcycle;

		forceinline HalfCycles perform_bus_operation(const Microcycle &cycle, int is_supervisor) {
			// Advance time.
			advance_time(cycle.length);

			// A null cycle leaves nothing else to do.
			if(!(cycle.operation & (Microcycle::NewAddress | Microcycle::SameAddress))) return HalfCycles(0);

			// Grab the value on the address bus, at word precision.
			uint32_t word_address = cycle.active_operation_word_address();

			// Everything above E0 0000 is signalled as being on the peripheral bus.
			mc68000_.set_is_peripheral_address(word_address >= 0x700000);

			// All code below deals only with reads and writes — cycles in which a
			// data select is active. So quit now if this is not the active part of
			// a read or write.
			//
			// The 68000 uses 6800-style autovectored interrupts, so the mere act of
			// having set VPA above deals with those given that the generated address
			// for interrupt acknowledge cycles always has all bits set except the
			// lowest explicit address lines.
			if(!cycle.data_select_active() || (cycle.operation & Microcycle::InterruptAcknowledge)) return HalfCycles(0);

			// Grab the word-precision address being accessed.
			uint16_t *memory_base = nullptr;
			HalfCycles delay;
			switch(memory_map_[word_address >> 18]) {
				default: assert(false);

				case BusDevice::Unassigned:
					fill_unmapped(cycle);
				return delay;

				case BusDevice::VIA: {
					if(*cycle.address & 1) {
						fill_unmapped(cycle);
					} else {
						const int register_address = word_address >> 8;

						// VIA accesses are via address 0xefe1fe + register*512,
						// which at word precision is 0x77f0ff + register*256.
						if(cycle.operation & Microcycle::Read) {
							cycle.value->halves.low = via_.get_register(register_address);
						} else {
							via_.set_register(register_address, cycle.value->halves.low);
						}

						if(cycle.operation & Microcycle::SelectWord) cycle.value->halves.high = 0xff;
					}
				} return delay;

				case BusDevice::PhaseRead: {
					if(cycle.operation & Microcycle::Read) {
						cycle.value->halves.low = phase_ & 7;
					}

					if(cycle.operation & Microcycle::SelectWord) cycle.value->halves.high = 0xff;
				} return delay;

				case BusDevice::IWM: {
					if(*cycle.address & 1) {
						const int register_address = word_address >> 8;

						// The IWM; this is a purely polled device, so can be run on demand.
						iwm_.flush();
						if(cycle.operation & Microcycle::Read) {
							cycle.value->halves.low = iwm_.iwm.read(register_address);
						} else {
							iwm_.iwm.write(register_address, cycle.value->halves.low);
						}

						if(cycle.operation & Microcycle::SelectWord) cycle.value->halves.high = 0xff;
					} else {
						fill_unmapped(cycle);
					}
				} return delay;

				case BusDevice::SCCReadResetPhase: {
					// Any word access here adjusts phase.
					if(cycle.operation & Microcycle::SelectWord) {
						adjust_phase();
					} else {
						// A0 = 1 => reset; A0 = 0 => read.
						if(*cycle.address & 1) {
							scc_.reset();

							if(cycle.operation & Microcycle::Read) {
								cycle.value->halves.low = 0xff;
							}
						} else {
							const auto read = scc_.read(int(word_address));
							if(cycle.operation & Microcycle::Read) {
								cycle.value->halves.low = read;
							}
						}
					}
				} return delay;

				case BusDevice::SCCWrite: {
					// Any word access here adjusts phase.
					if(cycle.operation & Microcycle::SelectWord) {
						adjust_phase();
					} else {
						if(*cycle.address & 1) {
							if(cycle.operation & Microcycle::Read) {
								scc_.write(int(word_address), 0xff);
								cycle.value->halves.low = 0xff;
							} else {
								scc_.write(int(word_address), cycle.value->halves.low);
							}
						} else {
							fill_unmapped(cycle);
						}
					}
				} return delay;

				case BusDevice::RAM: {
					// This is coupled with the Macintosh implementation of video; the magic
					// constant should probably be factored into the Video class.
					// It embodies knowledge of the fact that video (and audio) will always
					// be fetched from the final $d900 bytes (i.e. $6c80 words) of memory.
					// (And that ram_mask_ = ram size - 1).
					if(word_address > ram_mask_ - 0x6c80)
						update_video();

					memory_base = ram_;
					word_address &= ram_mask_;

					// Apply a delay due to video contention if applicable; technically this is
					// incorrectly placed — strictly speaking here I'm extending the part of the
					// bus cycle after DTACK rather than delaying DTACK. But it adds up to the
					// same thing.
					if(ram_subcycle_ < 4) {
						delay = HalfCycles(4 - ram_subcycle_);
						advance_time(delay);
					}
				} break;

				case BusDevice::ROM: {
					if(!(cycle.operation & Microcycle::Read)) return delay;
					memory_base = rom_;
					word_address &= rom_mask_;
				} break;
			}

			// If control has fallen through to here, the access is either a read from ROM, or a read or write to RAM.
			switch(cycle.operation & (Microcycle::SelectWord | Microcycle::SelectByte | Microcycle::Read)) {
				default:
				break;

				case Microcycle::SelectWord | Microcycle::Read:
					cycle.value->full = memory_base[word_address];
				break;
				case Microcycle::SelectByte | Microcycle::Read:
					cycle.value->halves.low = uint8_t(memory_base[word_address] >> cycle.byte_shift());
				break;
				case Microcycle::SelectWord:
					memory_base[word_address] = cycle.value->full;
				break;
				case Microcycle::SelectByte:
					memory_base[word_address] = uint16_t(
						(cycle.value->halves.low << cycle.byte_shift()) |
						(memory_base[word_address] & cycle.untouched_byte_mask())
					);
				break;
			}

			return delay;
		}

		void flush() {
			// Flush the video before the audio queue; in a Mac the
			// video is responsible for providing part of the
			// audio signal, so the two aren't as distinct as in
			// most machines.
			update_video();

			// As above: flush audio after video.
			via_.flush();
			audio_.queue.perform();

			// Experimental?
			iwm_.flush();
		}

		void set_rom_is_overlay(bool rom_is_overlay) {
			ROM_is_overlay_ = rom_is_overlay;

			using Model = Analyser::Static::Macintosh::Target::Model;
			switch(model) {
				case Model::Mac128k:
				case Model::Mac512k:
				case Model::Mac512ke:
					populate_memory_map([rom_is_overlay] (std::function<void(int target, BusDevice device)> map_to) {
						// Addresses up to $80 0000 aren't affected by this bit.
						if(rom_is_overlay) {
							// Up to $60 0000 mirrors of the ROM alternate with unassigned areas every $10 0000 byes.
							for(int c = 0; c <= 0x600000; c += 0x100000) {
								map_to(c, ((c >> 20)&1) ? BusDevice::ROM : BusDevice::Unassigned);
							}
							map_to(0x800000, BusDevice::RAM);
						} else {
							map_to(0x400000, BusDevice::RAM);
							map_to(0x500000, BusDevice::ROM);
							map_to(0x800000, BusDevice::Unassigned);
						}
					});
				break;

				case Model::MacPlus:
					populate_memory_map([rom_is_overlay] (std::function<void(int target, BusDevice device)> map_to) {
						// Addresses up to $80 0000 aren't affected by this bit.
						if(rom_is_overlay) {
							map_to(0x100000, BusDevice::ROM);
							map_to(0x400000, BusDevice::Unassigned);
							map_to(0x500000, BusDevice::ROM);
							map_to(0x580000, BusDevice::Unassigned);
							map_to(0x600000, BusDevice::SCSI);
							map_to(0x800000, BusDevice::RAM);
						} else {
							map_to(0x400000, BusDevice::RAM);
							map_to(0x500000, BusDevice::ROM);
							map_to(0x580000, BusDevice::Unassigned);
							map_to(0x600000, BusDevice::SCSI);
							map_to(0x800000, BusDevice::Unassigned);
						}
					});
				break;
			}
		}

		bool video_is_outputting() {
			return video_.is_outputting(time_since_video_update_);
		}

		void set_use_alternate_buffers(bool use_alternate_screen_buffer, bool use_alternate_audio_buffer) {
			update_video();
			video_.set_use_alternate_buffers(use_alternate_screen_buffer, use_alternate_audio_buffer);
		}

		bool insert_media(const Analyser::Static::Media &media) override {
			if(media.disks.empty())
				return false;

			// TODO: shouldn't allow disks to be replaced like this, as the Mac
			// uses software eject. Will need to expand messaging ability of
			// insert_media.
			if(drives_[0].has_disk())
				drives_[1].set_disk(media.disks[0]);
			else
				drives_[0].set_disk(media.disks[0]);

			return true;
		}

		// MARK: Keyboard input.

		KeyboardMapper *get_keyboard_mapper() override {
			return &keyboard_mapper_;
		}

		void set_key_state(uint16_t key, bool is_pressed) override {
			keyboard_.enqueue_key_state(key, is_pressed);
		}

		// TODO: clear all keys.

		// MARK: Interrupt updates.

		void did_change_interrupt_status(Zilog::SCC::z8530 *sender, bool new_status) override {
			update_interrupt_input();
		}

		void update_interrupt_input() {
			// Update interrupt input.
			// TODO: does this really cascade like this?
			if(scc_.get_interrupt_line()) {
				mc68000_.set_interrupt_level(2);
			} else if(via_.get_interrupt_line()) {
				mc68000_.set_interrupt_level(1);
			} else {
				mc68000_.set_interrupt_level(0);
			}
		}

		// MARK: - Activity Source
		void set_activity_observer(Activity::Observer *observer) override {
			iwm_.iwm.set_activity_observer(observer);
		}

	private:
		forceinline void adjust_phase() {
			++phase_;
		}

		forceinline void fill_unmapped(const Microcycle &cycle) {
			if(!(cycle.operation & Microcycle::Read)) return;
			if(cycle.operation & Microcycle::SelectWord) {
				cycle.value->full = 0xffff;
			} else {
				cycle.value->halves.low = 0xff;
			}
		}

		/// Advances all non-CPU components by @c duration half cycles.
		forceinline void advance_time(HalfCycles duration) {
			time_since_video_update_ += duration;
			iwm_.time_since_update += duration;
			ram_subcycle_ = (ram_subcycle_ + duration.as_int()) & 15;

			// The VIA runs at one-tenth of the 68000's clock speed, in sync with the E clock.
			// See: Guide to the Macintosh Hardware Family p149 (PDF p188). Some extra division
			// may occur here in order to provide VSYNC at a proper moment.
			// Possibly route vsync.
			if(time_since_video_update_ < time_until_video_event_) {
				via_clock_ += duration;
				via_.run_for(via_clock_.divide(HalfCycles(10)));
			} else {
				auto via_time_base = time_since_video_update_ - duration;
				auto via_cycles_outstanding = duration;
				while(time_until_video_event_ < time_since_video_update_) {
					const auto via_cycles = time_until_video_event_ - via_time_base;
					via_time_base = HalfCycles(0);
					via_cycles_outstanding -= via_cycles;

					via_clock_ += via_cycles;
					via_.run_for(via_clock_.divide(HalfCycles(10)));

					video_.run_for(time_until_video_event_);
					time_since_video_update_ -= time_until_video_event_;
					time_until_video_event_ = video_.get_next_sequence_point();

					via_.set_control_line_input(MOS::MOS6522::Port::A, MOS::MOS6522::Line::One, !video_.vsync());
				}

				via_clock_ += via_cycles_outstanding;
				via_.run_for(via_clock_.divide(HalfCycles(10)));
			}

			// The keyboard also has a clock, albeit a very slow one — 100,000 cycles/second.
			// Its clock and data lines are connected to the VIA.
			keyboard_clock_ += duration;
			const auto keyboard_ticks = keyboard_clock_.divide(HalfCycles(CLOCK_RATE / 100000));
			if(keyboard_ticks > HalfCycles(0)) {
				keyboard_.run_for(keyboard_ticks);
				via_.set_control_line_input(MOS::MOS6522::Port::B, MOS::MOS6522::Line::Two, keyboard_.get_data());
				via_.set_control_line_input(MOS::MOS6522::Port::B, MOS::MOS6522::Line::One, keyboard_.get_clock());
			}

			// Feed mouse inputs within at most 1250 cycles of each other.
			if(mouse_.has_steps()) {
				time_since_mouse_update_ += duration;
				const auto mouse_ticks = time_since_mouse_update_.divide(HalfCycles(2500));
				if(mouse_ticks > HalfCycles(0)) {
					mouse_.prepare_step();
					scc_.set_dcd(0, mouse_.get_channel(1) & 1);
					scc_.set_dcd(1, mouse_.get_channel(0) & 1);
				}
			}

			// TODO: SCC should be clocked at a divide-by-two, if and when it actually has
			// anything connected.

			// Consider updating the real-time clock.
			real_time_clock_ += duration;
			auto ticks = real_time_clock_.divide_cycles(Cycles(CLOCK_RATE)).as_int();
			while(ticks--) {
				clock_.update();
				// TODO: leave a delay between toggling the input rather than using this coupled hack.
				via_.set_control_line_input(MOS::MOS6522::Port::A, MOS::MOS6522::Line::Two, true);
				via_.set_control_line_input(MOS::MOS6522::Port::A, MOS::MOS6522::Line::Two, false);
			}
		}

		forceinline void update_video() {
			video_.run_for(time_since_video_update_.flush<HalfCycles>());
			time_until_video_event_ = video_.get_next_sequence_point();
		}

		Inputs::Mouse &get_mouse() override {
			return mouse_;
		}

		struct IWM {
			IWM(int clock_rate) : iwm(clock_rate) {}

			HalfCycles time_since_update;
			Apple::IWM iwm;

			void flush() {
				iwm.run_for(time_since_update.flush<Cycles>());
			}
		};

		class VIAPortHandler: public MOS::MOS6522::PortHandler {
			public:
				VIAPortHandler(ConcreteMachine &machine, RealTimeClock &clock, Keyboard &keyboard, Video &video, DeferredAudio &audio, IWM &iwm, Inputs::QuadratureMouse &mouse) :
					machine_(machine), clock_(clock), keyboard_(keyboard), video_(video), audio_(audio), iwm_(iwm), mouse_(mouse) {}

				using Port = MOS::MOS6522::Port;
				using Line = MOS::MOS6522::Line;

				void set_port_output(Port port, uint8_t value, uint8_t direction_mask) {
					/*
						Peripheral lines: keyboard data, interrupt configuration.
						(See p176 [/215])
					*/
					switch(port) {
						case Port::A:
							/*
								Port A:
									b7:	[input] SCC wait/request (/W/REQA and /W/REQB wired together for a logical OR)
									b6:	0 = alternate screen buffer, 1 = main screen buffer
									b5:	floppy disk SEL state control (upper/lower head "among other things")
									b4:	1 = use ROM overlay memory map, 0 = use ordinary memory map
									b3:	0 = use alternate sound buffer, 1 = use ordinary sound buffer
									b2–b0:	audio output volume
							*/
							iwm_.flush();
							iwm_.iwm.set_select(!!(value & 0x20));

							machine_.set_use_alternate_buffers(!(value & 0x40), !(value&0x08));
							machine_.set_rom_is_overlay(!!(value & 0x10));

							audio_.flush();
							audio_.audio.set_volume(value & 7);
						break;

						case Port::B:
							/*
								Port B:
									b7:	0 = sound enabled, 1 = sound disabled
									b6:	[input] 0 = video beam in visible portion of line, 1 = outside
									b5:	[input] mouse y2
									b4:	[input] mouse x2
									b3:	[input] 0 = mouse button down, 1 = up
									b2:	0 = real-time clock enabled, 1 = disabled
									b1:	clock's data-clock line
									b0:	clock's serial data line
							*/
							if(value & 0x4) clock_.abort();
							else clock_.set_input(!!(value & 0x2), !!(value & 0x1));

							audio_.flush();
							audio_.audio.set_enabled(!(value & 0x80));
						break;
					}
				}

				uint8_t get_port_input(Port port) {
					switch(port) {
						case Port::A:
//							printf("6522 r A\n");
						return 0x00;	// TODO: b7 = SCC wait/request

						case Port::B:
						return uint8_t(
							((mouse_.get_button_mask() & 1) ? 0x00 : 0x08) |
							((mouse_.get_channel(0) & 2) << 3) |
							((mouse_.get_channel(1) & 2) << 4) |
							(clock_.get_data() ? 0x02 : 0x00) |
							(machine_.video_is_outputting() ? 0x00 : 0x40)
						);
					}

					// Should be unreachable.
					return 0xff;
				}

				void set_control_line_output(Port port, Line line, bool value) {
					/*
						Keyboard wiring (I believe):
						CB2 = data		(input/output)
						CB1 = clock		(input)

						CA2 is used for receiving RTC interrupts.
						CA1 is used for receiving vsync.
					*/
					if(port == Port::B && line == Line::Two) {
						keyboard_.set_input(value);
					}
					else LOG("Unhandled control line output: " << (port ? 'B' : 'A') << int(line));
				}

				void run_for(HalfCycles duration) {
					// The 6522 enjoys a divide-by-ten, so multiply back up here to make the
					// divided-by-two clock the audio works on.
					audio_.time_since_update += HalfCycles(duration.as_int() * 5);
				}

				void flush() {
					audio_.flush();
				}

				void set_interrupt_status(bool status) {
					machine_.update_interrupt_input();
				}

			private:
				ConcreteMachine &machine_;
				RealTimeClock &clock_;
				Keyboard &keyboard_;
				Video &video_;
				DeferredAudio &audio_;
				IWM &iwm_;
				Inputs::QuadratureMouse &mouse_;
		};

		CPU::MC68000::Processor<ConcreteMachine, true> mc68000_;

		DriveSpeedAccumulator drive_speed_accumulator_;
		IWM iwm_;

		DeferredAudio audio_;
		Video video_;

		RealTimeClock clock_;
		Keyboard keyboard_;

		MOS::MOS6522::MOS6522<VIAPortHandler> via_;
 		VIAPortHandler via_port_handler_;

 		Zilog::SCC::z8530 scc_;

 		HalfCycles via_clock_;
 		HalfCycles real_time_clock_;
 		HalfCycles keyboard_clock_;
 		HalfCycles time_since_video_update_;
 		HalfCycles time_until_video_event_;
 		HalfCycles time_since_mouse_update_;

		bool ROM_is_overlay_ = true;
		int phase_ = 1;
		int ram_subcycle_ = 0;

		DoubleDensityDrive drives_[2];
		Inputs::QuadratureMouse mouse_;

		Apple::Macintosh::KeyboardMapper keyboard_mapper_;

		enum class BusDevice {
			RAM, ROM, VIA, IWM, SCCWrite, SCCReadResetPhase, SCSI, PhaseRead, Unassigned
		};

		/// Divides the 24-bit address space up into $80000 (i.e. 512kb) segments, recording
		/// which device is current mapped in each area. Keeping it in a table is a bit faster
		/// than the multi-level address inspection that is otherwise required, as well as
		/// simplifying slightly the handling of different models.
		///
		/// So: index with the top 5 bits of the 24-bit address.
		BusDevice memory_map_[32];

		void setup_memory_map() {
			// Apply the power-up memory map, i.e. assume that ROM_is_overlay_ = true.

			using Model = Analyser::Static::Macintosh::Target::Model;
			switch(model) {
				default: assert(false);

				case Model::Mac128k:
				case Model::Mac512k:
				case Model::Mac512ke:
					populate_memory_map([] (std::function<void(int target, BusDevice device)> map_to) {
						// Up to $60 0000 mirrors of the ROM alternate with unassigned areas every $10 0000 byes.
						for(int c = 0; c <= 0x600000; c += 0x100000) {
							map_to(c, ((c >> 20)&1) ? BusDevice::ROM : BusDevice::Unassigned);
						}
						map_to(0x800000, BusDevice::RAM);
						map_to(0x900000, BusDevice::Unassigned);
						map_to(0xa00000, BusDevice::SCCReadResetPhase);
						map_to(0xb00000, BusDevice::Unassigned);
						map_to(0xc00000, BusDevice::SCCWrite);
						map_to(0xd00000, BusDevice::Unassigned);
						map_to(0xe00000, BusDevice::IWM);
						map_to(0xe80000, BusDevice::Unassigned);
						map_to(0xf00000, BusDevice::VIA);
						map_to(0xf80000, BusDevice::PhaseRead);
						map_to(0x1000000, BusDevice::Unassigned);
					});
				break;

				case Model::MacPlus:
					populate_memory_map([] (std::function<void(int target, BusDevice device)> map_to) {
						map_to(0x100000, BusDevice::ROM);
						map_to(0x400000, BusDevice::Unassigned);
						map_to(0x500000, BusDevice::ROM);
						map_to(0x580000, BusDevice::Unassigned);
						map_to(0x600000, BusDevice::SCSI);
						map_to(0x800000, BusDevice::RAM);
						map_to(0x900000, BusDevice::Unassigned);
						map_to(0xa00000, BusDevice::SCCReadResetPhase);
						map_to(0xb00000, BusDevice::Unassigned);
						map_to(0xc00000, BusDevice::SCCWrite);
						map_to(0xd00000, BusDevice::Unassigned);
						map_to(0xe00000, BusDevice::IWM);
						map_to(0xe80000, BusDevice::Unassigned);
						map_to(0xf00000, BusDevice::VIA);
						map_to(0xf80000, BusDevice::PhaseRead);
						map_to(0x1000000, BusDevice::Unassigned);
					});
				break;
			}
		}

		void populate_memory_map(std::function<void(std::function<void(int, BusDevice)>)> populator) {
			// Define semantics for below; map_to will write from the current cursor position
			// to the supplied 24-bit address, setting a particular mapped device.
			int segment = 0;
			auto map_to = [&segment, this](int address, BusDevice device) {
				for(; segment < address >> 19; ++segment) {
					this->memory_map_[segment] = device;
				}
			};

			populator(map_to);
		}

		uint32_t ram_mask_ = 0;
		uint32_t rom_mask_ = 0;
		uint16_t rom_[64*1024];
		uint16_t ram_[256*1024];
};

}
}

using namespace Apple::Macintosh;

Machine *Machine::Macintosh(const Analyser::Static::Target *target, const ROMMachine::ROMFetcher &rom_fetcher) {
	auto *const mac_target = dynamic_cast<const Analyser::Static::Macintosh::Target *>(target);

	using Model = Analyser::Static::Macintosh::Target::Model;
	switch(mac_target->model) {
		default:
		case Model::Mac128k:	return new ConcreteMachine<Model::Mac128k>(*mac_target, rom_fetcher);
		case Model::Mac512k:	return new ConcreteMachine<Model::Mac512k>(*mac_target, rom_fetcher);
		case Model::Mac512ke:	return new ConcreteMachine<Model::Mac512ke>(*mac_target, rom_fetcher);
		case Model::MacPlus:	return new ConcreteMachine<Model::MacPlus>(*mac_target, rom_fetcher);
	}
}

Machine::~Machine() {}
