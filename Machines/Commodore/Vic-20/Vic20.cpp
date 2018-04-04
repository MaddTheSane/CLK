//
//  Vic20.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 04/06/2016.
//  Copyright © 2016 Thomas Harte. All rights reserved.
//

#include "Vic20.hpp"

#include "Keyboard.hpp"

#include "../../ConfigurationTarget.hpp"
#include "../../CRTMachine.hpp"
#include "../../KeyboardMachine.hpp"
#include "../../JoystickMachine.hpp"

#include "../../../Processors/6502/6502.hpp"
#include "../../../Components/6560/6560.hpp"
#include "../../../Components/6522/6522.hpp"

#include "../../../ClockReceiver/ForceInline.hpp"

#include "../../../Storage/Tape/Parsers/Commodore.hpp"

#include "../SerialBus.hpp"
#include "../1540/C1540.hpp"

#include "../../../Storage/Tape/Tape.hpp"
#include "../../../Storage/Disk/Disk.hpp"

#include "../../../Configurable/StandardOptions.hpp"

#include "../../../Analyser/Static/Commodore/Target.hpp"

#include <algorithm>
#include <cstdint>

namespace Commodore {
namespace Vic20 {

enum ROMSlot {
	Kernel = 0,
	BASIC,
	Characters,
	Drive
};

std::vector<std::unique_ptr<Configurable::Option>> get_options() {
	return Configurable::standard_options(
		static_cast<Configurable::StandardOptions>(Configurable::DisplaySVideo | Configurable::DisplayComposite | Configurable::QuickLoadTape)
	);
}

enum JoystickInput {
	Up = 0x04,
	Down = 0x08,
	Left = 0x10,
	Right = 0x80,
	Fire = 0x20
};

enum ROM {
	CharactersDanish = 0,
	CharactersEnglish,
	CharactersJapanese,
	CharactersSwedish,
	KernelDanish,
	KernelJapanese,
	KernelNTSC,
	KernelPAL,
	KernelSwedish
};

/*!
	Models the user-port VIA, which is the Vic's connection point for controlling its tape recorder —
	sensing the presence or absence of a tape and controlling the tape motor — and reading the current
	state from its serial port. Most of the joystick input is also exposed here.
*/
class UserPortVIA: public MOS::MOS6522::IRQDelegatePortHandler {
	public:
		UserPortVIA() : port_a_(0xbf) {}

		/// Reports the current input to the 6522 port @c port.
		uint8_t get_port_input(MOS::MOS6522::Port port) {
			// Port A provides information about the presence or absence of a tape, and parts of
			// the joystick and serial port state, both of which have been statefully collected
			// into port_a_.
			if(!port) {
				return port_a_ | (tape_->has_tape() ? 0x00 : 0x40);
			}
			return 0xff;
		}

		/// Receives announcements of control line output change from the 6522.
		void set_control_line_output(MOS::MOS6522::Port port, MOS::MOS6522::Line line, bool value) {
			// The CA2 output is used to control the tape motor.
			if(port == MOS::MOS6522::Port::A && line == MOS::MOS6522::Line::Two) {
				tape_->set_motor_control(!value);
			}
		}

		/// Receives announcements of changes in the serial bus connected to the serial port and propagates them into Port A.
		void set_serial_line_state(::Commodore::Serial::Line line, bool value) {
			switch(line) {
				default: break;
				case ::Commodore::Serial::Line::Data: port_a_ = (port_a_ & ~0x02) | (value ? 0x02 : 0x00);	break;
				case ::Commodore::Serial::Line::Clock: port_a_ = (port_a_ & ~0x01) | (value ? 0x01 : 0x00);	break;
			}
		}

		/// Allows the current joystick input to be set.
		void set_joystick_state(JoystickInput input, bool value) {
			if(input != JoystickInput::Right) {
				port_a_ = (port_a_ & ~input) | (value ? 0 : input);
			}
		}

		/// Receives announcements from the 6522 of user-port output, which might affect what's currently being presented onto the serial bus.
		void set_port_output(MOS::MOS6522::Port port, uint8_t value, uint8_t mask) {
			// Line 7 of port A is inverted and output as serial ATN.
			if(!port) {
				std::shared_ptr<::Commodore::Serial::Port> serialPort = serial_port_.lock();
				if(serialPort) serialPort->set_output(::Commodore::Serial::Line::Attention, (::Commodore::Serial::LineLevel)!(value&0x80));
			}
		}

		/// Sets @serial_port as this VIA's connection to the serial bus.
		void set_serial_port(std::shared_ptr<::Commodore::Serial::Port> serial_port) {
			serial_port_ = serial_port;
		}

		/// Sets @tape as the tape player connected to this VIA.
		void set_tape(std::shared_ptr<Storage::Tape::BinaryTapePlayer> tape) {
			tape_ = tape;
		}

	private:
		uint8_t port_a_;
		std::weak_ptr<::Commodore::Serial::Port> serial_port_;
		std::shared_ptr<Storage::Tape::BinaryTapePlayer> tape_;
};

/*!
	Models the keyboard VIA, which is used by the Vic for reading its keyboard, to output to its serial port,
	and for the small portion of joystick input not connected to the user-port VIA.
*/
class KeyboardVIA: public MOS::MOS6522::IRQDelegatePortHandler {
	public:
		KeyboardVIA() : port_b_(0xff) {
			clear_all_keys();
		}

		/// Sets whether @c key @c is_pressed.
		void set_key_state(uint16_t key, bool is_pressed) {
			if(is_pressed)
				columns_[key & 7] &= ~(key >> 3);
			else
				columns_[key & 7] |= (key >> 3);
		}

		/// Sets all keys as unpressed.
		void clear_all_keys() {
			memset(columns_, 0xff, sizeof(columns_));
		}

		/// Called by the 6522 to get input. Reads the keyboard on Port A, returns a small amount of joystick state on Port B.
		uint8_t get_port_input(MOS::MOS6522::Port port) {
			if(!port) {
				uint8_t result = 0xff;
				for(int c = 0; c < 8; c++) {
					if(!(activation_mask_&(1 << c)))
						result &= columns_[c];
				}
				return result;
			}

			return port_b_;
		}

		/// Called by the 6522 to set output. The value of Port B selects which part of the keyboard to read.
		void set_port_output(MOS::MOS6522::Port port, uint8_t value, uint8_t mask) {
			if(port) activation_mask_ = (value & mask) | (~mask);
		}

		/// Called by the 6522 to set control line output. Which affects the serial port.
		void set_control_line_output(MOS::MOS6522::Port port, MOS::MOS6522::Line line, bool value) {
			if(line == MOS::MOS6522::Line::Two) {
				std::shared_ptr<::Commodore::Serial::Port> serialPort = serial_port_.lock();
				if(serialPort) {
					// CB2 is inverted to become serial data; CA2 is inverted to become serial clock
					if(port == MOS::MOS6522::Port::A)
						serialPort->set_output(::Commodore::Serial::Line::Clock, (::Commodore::Serial::LineLevel)!value);
					else
						serialPort->set_output(::Commodore::Serial::Line::Data, (::Commodore::Serial::LineLevel)!value);
				}
			}
		}

		/// Sets whether the joystick input @c input is pressed.
		void set_joystick_state(JoystickInput input, bool value) {
			if(input == JoystickInput::Right) {
				port_b_ = (port_b_ & ~input) | (value ? 0 : input);
			}
		}

		/// Sets the serial port to which this VIA is connected.
		void set_serial_port(std::shared_ptr<::Commodore::Serial::Port> serialPort) {
			serial_port_ = serialPort;
		}

	private:
		uint8_t port_b_;
		uint8_t columns_[8];
		uint8_t activation_mask_;
		std::weak_ptr<::Commodore::Serial::Port> serial_port_;
};

/*!
	Models the Vic's serial port, providing the receipticle for input.
*/
class SerialPort : public ::Commodore::Serial::Port {
	public:
		/// Receives an input change from the base serial port class, and communicates it to the user-port VIA.
		void set_input(::Commodore::Serial::Line line, ::Commodore::Serial::LineLevel level) {
			std::shared_ptr<UserPortVIA> userPortVIA = user_port_via_.lock();
			if(userPortVIA) userPortVIA->set_serial_line_state(line, (bool)level);
		}

		/// Sets the user-port VIA with which this serial port communicates.
		void set_user_port_via(std::shared_ptr<UserPortVIA> userPortVIA) {
			user_port_via_ = userPortVIA;
		}

	private:
		std::weak_ptr<UserPortVIA> user_port_via_;
};

/*!
	Provides the bus over which the Vic 6560 fetches memory in a Vic-20.
*/
class Vic6560: public MOS::MOS6560<Vic6560> {
	public:
		/// Performs a read on behalf of the 6560; in practice uses @c video_memory_map and @c colour_memory to find data.
		inline void perform_read(uint16_t address, uint8_t *pixel_data, uint8_t *colour_data) {
			*pixel_data = video_memory_map[address >> 10] ? video_memory_map[address >> 10][address & 0x3ff] : 0xff; // TODO
			*colour_data = colour_memory[address & 0x03ff];
		}

		// It is assumed that these pointers have been filled in by the machine.
		uint8_t *video_memory_map[16];	// Segments video memory into 1kb portions.
		uint8_t *colour_memory;			// Colour memory must be contiguous.
};

/*!
	Interfaces a joystick to the two VIAs.
*/
class Joystick: public Inputs::Joystick {
	public:
		Joystick(UserPortVIA &user_port_via_port_handler, KeyboardVIA &keyboard_via_port_handler) :
			user_port_via_port_handler_(user_port_via_port_handler),
			keyboard_via_port_handler_(keyboard_via_port_handler) {}

		std::vector<DigitalInput> get_inputs() override {
			return {
				DigitalInput(DigitalInput::Up),
				DigitalInput(DigitalInput::Down),
				DigitalInput(DigitalInput::Left),
				DigitalInput(DigitalInput::Right),
				DigitalInput(DigitalInput::Fire)
			};
		}

		void set_digital_input(const DigitalInput &digital_input, bool is_active) override {
			JoystickInput mapped_input;
			switch(digital_input.type) {
				default: return;
				case DigitalInput::Up: mapped_input = Up;		break;
				case DigitalInput::Down: mapped_input = Down;	break;
				case DigitalInput::Left: mapped_input = Left;	break;
				case DigitalInput::Right: mapped_input = Right;	break;
				case DigitalInput::Fire: mapped_input = Fire;	break;
			}

			user_port_via_port_handler_.set_joystick_state(mapped_input, is_active);
			keyboard_via_port_handler_.set_joystick_state(mapped_input, is_active);
		}

	private:
		UserPortVIA &user_port_via_port_handler_;
		KeyboardVIA &keyboard_via_port_handler_;
};

class ConcreteMachine:
	public CRTMachine::Machine,
	public ConfigurationTarget::Machine,
	public KeyboardMachine::Machine,
	public JoystickMachine::Machine,
	public Configurable::Device,
	public CPU::MOS6502::BusHandler,
	public MOS::MOS6522::IRQDelegatePortHandler::Delegate,
	public Utility::TypeRecipient,
	public Storage::Tape::BinaryTapePlayer::Delegate,
	public Machine,
	public Sleeper::SleepObserver {
	public:
		ConcreteMachine() :
				m6502_(*this),
				user_port_via_port_handler_(new UserPortVIA),
				keyboard_via_port_handler_(new KeyboardVIA),
				serial_port_(new SerialPort),
				serial_bus_(new ::Commodore::Serial::Bus),
				user_port_via_(*user_port_via_port_handler_),
				keyboard_via_(*keyboard_via_port_handler_),
				tape_(new Storage::Tape::BinaryTapePlayer(1022727)) {
			// communicate the tape to the user-port VIA
			user_port_via_port_handler_->set_tape(tape_);

			// wire up the serial bus and serial port
			Commodore::Serial::AttachPortAndBus(serial_port_, serial_bus_);

			// wire up 6522s and serial port
			user_port_via_port_handler_->set_serial_port(serial_port_);
			keyboard_via_port_handler_->set_serial_port(serial_port_);
			serial_port_->set_user_port_via(user_port_via_port_handler_);

			// wire up the 6522s, tape and machine
			user_port_via_port_handler_->set_interrupt_delegate(this);
			keyboard_via_port_handler_->set_interrupt_delegate(this);
			tape_->set_delegate(this);
			tape_->set_sleep_observer(this);

			// install a joystick
			joysticks_.emplace_back(new Joystick(*user_port_via_port_handler_, *keyboard_via_port_handler_));
		}

		// Obtains the system ROMs.
		bool set_rom_fetcher(const std::function<std::vector<std::unique_ptr<std::vector<uint8_t>>>(const std::string &machine, const std::vector<std::string> &names)> &roms_with_names) override {
			rom_fetcher_ = roms_with_names;

			auto roms = roms_with_names(
				"Vic20",
				{
					"characters-danish.bin",
					"characters-english.bin",
					"characters-japanese.bin",
					"characters-swedish.bin",
					"kernel-danish.bin",
					"kernel-japanese.bin",
					"kernel-ntsc.bin",
					"kernel-pal.bin",
					"kernel-swedish.bin",
					"basic.bin"
				});

			for(std::size_t index = 0; index < roms.size(); ++index) {
				auto &data = roms[index];
				if(!data) return false;
				if(index < 9) roms_[index] = std::move(*data); else basic_rom_ = std::move(*data);
			}

			// Characters ROMs should be 4kb.
			for(std::size_t index = 0; index < 4; ++index) roms_[index].resize(4096);
			// Kernel ROMs and the BASIC ROM should be 8kb.
			for(std::size_t index = 4; index < roms.size(); ++index) roms_[index].resize(8192);

			return true;
		}

		void configure_as_target(const Analyser::Static::Target *target) override final {
			commodore_target_ = *dynamic_cast<const Analyser::Static::Commodore::Target *>(target);

			if(target->loading_command.length()) {
				type_string(target->loading_command);
			}

			if(target->media.disks.size()) {
				// construct the 1540
				c1540_.reset(new ::Commodore::C1540::Machine(Commodore::C1540::Machine::C1540));

				// attach it to the serial bus
				c1540_->set_serial_bus(serial_bus_);

				// give it a means to obtain its ROM
				c1540_->set_rom_fetcher(rom_fetcher_);

				// give it a little warm up
				c1540_->run_for(Cycles(2000000));
			}

			insert_media(target->media);
		}

		bool insert_media(const Analyser::Static::Media &media) override final {
			if(!media.tapes.empty()) {
				tape_->set_tape(media.tapes.front());
			}

			if(!media.disks.empty() && c1540_) {
				c1540_->set_disk(media.disks.front());
			}

			if(!media.cartridges.empty()) {
				rom_address_ = 0xa000;
				std::vector<uint8_t> rom_image = media.cartridges.front()->get_segments().front().data;
				rom_length_ = static_cast<uint16_t>(rom_image.size());

				rom_ = rom_image;
				rom_.resize(0x2000);
			}

			set_use_fast_tape();

			return !media.tapes.empty() || (!media.disks.empty() && c1540_ != nullptr) || !media.cartridges.empty();
		}

		void set_key_state(uint16_t key, bool is_pressed) override final {
			if(key != KeyRestore)
				keyboard_via_port_handler_->set_key_state(key, is_pressed);
			else
				user_port_via_.set_control_line_input(MOS::MOS6522::Port::A, MOS::MOS6522::Line::One, !is_pressed);
		}

		void clear_all_keys() override final {
			keyboard_via_port_handler_->clear_all_keys();
		}

		std::vector<std::unique_ptr<Inputs::Joystick>> &get_joysticks() override {
			return joysticks_;
		}

		void set_ntsc_6560() {
			set_clock_rate(1022727);
			if(mos6560_) {
				mos6560_->set_output_mode(MOS::MOS6560<Commodore::Vic20::Vic6560>::OutputMode::NTSC);
				mos6560_->set_clock_rate(1022727);
			}
		}

		void set_pal_6560() {
			set_clock_rate(1108404);
			if(mos6560_) {
				mos6560_->set_output_mode(MOS::MOS6560<Commodore::Vic20::Vic6560>::OutputMode::PAL);
				mos6560_->set_clock_rate(1108404);
			}
		}

		void set_memory_map(Analyser::Static::Commodore::Target::MemoryModel memory_model, Analyser::Static::Commodore::Target::Region region) {
			// Determine PAL/NTSC
			if(region == Analyser::Static::Commodore::Target::Region::American || region == Analyser::Static::Commodore::Target::Region::Japanese) {
				// NTSC
				set_ntsc_6560();
			} else {
				// PAL
				set_pal_6560();
			}

			// Initialise the memory maps as all pointing to nothing
			memset(processor_read_memory_map_, 0, sizeof(processor_read_memory_map_));
			memset(processor_write_memory_map_, 0, sizeof(processor_write_memory_map_));
			memset(mos6560_->video_memory_map, 0, sizeof(mos6560_->video_memory_map));

#define set_ram(baseaddr, length)	\
	write_to_map(processor_read_memory_map_, &ram_[baseaddr], baseaddr, length);	\
	write_to_map(processor_write_memory_map_, &ram_[baseaddr], baseaddr, length);

			// Add 6502-visible RAM as requested
			switch(memory_model) {
				case Analyser::Static::Commodore::Target::MemoryModel::Unexpanded:
					// The default Vic-20 memory map has 1kb at address 0 and another 4kb at address 0x1000.
					set_ram(0x0000, 0x0400);
					set_ram(0x1000, 0x1000);
				break;
				case Analyser::Static::Commodore::Target::MemoryModel::EightKB:
					// An 8kb Vic-20 fills in the gap between the two blocks of RAM on an unexpanded machine.
					set_ram(0x0000, 0x2000);
				break;
				case Analyser::Static::Commodore::Target::MemoryModel::ThirtyTwoKB:
					// A 32kb Vic-20 fills the entire lower 32kb with RAM.
					set_ram(0x0000, 0x8000);
				break;
			}

#undef set_ram

			// all expansions also have colour RAM visible at 0x9400.
			write_to_map(processor_read_memory_map_, colour_ram_, 0x9400, sizeof(colour_ram_));
			write_to_map(processor_write_memory_map_, colour_ram_, 0x9400, sizeof(colour_ram_));

			// also push memory resources into the 6560 video memory map; the 6560 has only a
			// 14-bit address bus and the top bit is invested and used as bit 15 for the main
			// memory bus.
			for(int addr = 0; addr < 0x4000; addr += 0x400) {
				int source_address = (addr & 0x1fff) | (((addr & 0x2000) << 2) ^ 0x8000);
				if(processor_read_memory_map_[source_address >> 10]) {
					write_to_map(mos6560_->video_memory_map, &ram_[source_address], static_cast<uint16_t>(addr), 0x400);
				}
			}
			mos6560_->colour_memory = colour_ram_;

			// install the BASIC ROM
			write_to_map(processor_read_memory_map_, basic_rom_.data(), 0xc000, static_cast<uint16_t>(basic_rom_.size()));

			// install the system ROM
			ROM character_rom;
			ROM kernel_rom;
			switch(region) {
				default:
					character_rom = CharactersEnglish;
					kernel_rom = KernelPAL;
				break;
				case Analyser::Static::Commodore::Target::Region::American:
					character_rom = CharactersEnglish;
					kernel_rom = KernelNTSC;
				break;
				case Analyser::Static::Commodore::Target::Region::Danish:
					character_rom = CharactersDanish;
					kernel_rom = KernelDanish;
				break;
				case Analyser::Static::Commodore::Target::Region::Japanese:
					character_rom = CharactersJapanese;
					kernel_rom = KernelJapanese;
				break;
				case Analyser::Static::Commodore::Target::Region::Swedish:
					character_rom = CharactersSwedish;
					kernel_rom = KernelSwedish;
				break;
			}

			write_to_map(processor_read_memory_map_, roms_[character_rom].data(), 0x8000, static_cast<uint16_t>(roms_[character_rom].size()));
			write_to_map(mos6560_->video_memory_map, roms_[character_rom].data(), 0x0000, static_cast<uint16_t>(roms_[character_rom].size()));
			write_to_map(processor_read_memory_map_, roms_[kernel_rom].data(), 0xe000, static_cast<uint16_t>(roms_[kernel_rom].size()));

			// install the inserted ROM if there is one
			if(!rom_.empty()) {
				write_to_map(processor_read_memory_map_, rom_.data(), rom_address_, rom_length_);
			}
		}

		// to satisfy CPU::MOS6502::Processor
		forceinline Cycles perform_bus_operation(CPU::MOS6502::BusOperation operation, uint16_t address, uint8_t *value) {
			// run the phase-1 part of this cycle, in which the VIC accesses memory
			cycles_since_mos6560_update_++;

			// run the phase-2 part of the cycle, which is whatever the 6502 said it should be
			if(isReadOperation(operation)) {
				uint8_t result = processor_read_memory_map_[address >> 10] ? processor_read_memory_map_[address >> 10][address & 0x3ff] : 0xff;
				if((address&0xfc00) == 0x9000) {
					if((address&0xff00) == 0x9000) {
						update_video();
						result &= mos6560_->get_register(address);
					}
					if((address&0xfc10) == 0x9010)	result &= user_port_via_.get_register(address);
					if((address&0xfc20) == 0x9020)	result &= keyboard_via_.get_register(address);
				}
				*value = result;

				// Consider applying the fast tape hack.
				if(use_fast_tape_hack_ && operation == CPU::MOS6502::BusOperation::ReadOpcode) {
					if(address == 0xf7b2) {
						// Address 0xf7b2 contains a JSR to 0xf8c0 that will fill the tape buffer with the next header.
						// So cancel that via a double NOP and fill in the next header programmatically.
						Storage::Tape::Commodore::Parser parser;
						std::unique_ptr<Storage::Tape::Commodore::Header> header = parser.get_next_header(tape_->get_tape());

						const uint64_t tape_position = tape_->get_tape()->get_offset();
						if(header) {
							// serialise to wherever b2:b3 points
							const uint16_t tape_buffer_pointer = static_cast<uint16_t>(ram_[0xb2]) | static_cast<uint16_t>(ram_[0xb3] << 8);
							header->serialise(&ram_[tape_buffer_pointer], 0x8000 - tape_buffer_pointer);
							hold_tape_ = true;
							printf("Found header\n");
						} else {
							// no header found, so pretend this hack never interceded
							tape_->get_tape()->set_offset(tape_position);
							hold_tape_ = false;
							printf("Didn't find header\n");
						}

						// clear status and the verify flag
						ram_[0x90] = 0;
						ram_[0x93] = 0;

						*value = 0x0c;	// i.e. NOP abs, to swallow the entire JSR
					} else if(address == 0xf90b) {
						uint8_t x = static_cast<uint8_t>(m6502_.get_value_of_register(CPU::MOS6502::Register::X));
						if(x == 0xe) {
							Storage::Tape::Commodore::Parser parser;
							const uint64_t tape_position = tape_->get_tape()->get_offset();
							const std::unique_ptr<Storage::Tape::Commodore::Data> data = parser.get_next_data(tape_->get_tape());
							if(data) {
								uint16_t start_address, end_address;
								start_address = static_cast<uint16_t>(ram_[0xc1] | (ram_[0xc2] << 8));
								end_address = static_cast<uint16_t>(ram_[0xae] | (ram_[0xaf] << 8));

								// perform a via-processor_write_memory_map_ memcpy
								uint8_t *data_ptr = data->data.data();
								std::size_t data_left = data->data.size();
								while(data_left && start_address != end_address) {
									uint8_t *page = processor_write_memory_map_[start_address >> 10];
									if(page) page[start_address & 0x3ff] = *data_ptr;
									data_ptr++;
									start_address++;
									data_left--;
								}

								// set tape status, carry and flag
								ram_[0x90] |= 0x40;
								uint8_t	flags = static_cast<uint8_t>(m6502_.get_value_of_register(CPU::MOS6502::Register::Flags));
								flags &= ~static_cast<uint8_t>((CPU::MOS6502::Flag::Carry | CPU::MOS6502::Flag::Interrupt));
								m6502_.set_value_of_register(CPU::MOS6502::Register::Flags, flags);

								// to ensure that execution proceeds to 0xfccf, pretend a NOP was here and
								// ensure that the PC leaps to 0xfccf
								m6502_.set_value_of_register(CPU::MOS6502::Register::ProgramCounter, 0xfccf);
								*value = 0xea;	// i.e. NOP implied
								hold_tape_ = true;
								printf("Found data\n");
							} else {
								tape_->get_tape()->set_offset(tape_position);
								hold_tape_ = false;
								printf("Didn't find data\n");
							}
						}
					}
				}
			} else {
				uint8_t *ram = processor_write_memory_map_[address >> 10];
				if(ram) {
					update_video();
					ram[address & 0x3ff] = *value;
				}
				if((address&0xfc00) == 0x9000) {
					if((address&0xff00) == 0x9000) {
						update_video();
						mos6560_->set_register(address, *value);
					}
					if((address&0xfc10) == 0x9010)	user_port_via_.set_register(address, *value);
					if((address&0xfc20) == 0x9020)	keyboard_via_.set_register(address, *value);
				}
			}

			user_port_via_.run_for(Cycles(1));
			keyboard_via_.run_for(Cycles(1));
			if(typer_ && operation == CPU::MOS6502::BusOperation::ReadOpcode && address == 0xEB1E) {
				if(!typer_->type_next_character()) {
					clear_all_keys();
					typer_.reset();
				}
			}
			if(!tape_is_sleeping_ && !hold_tape_) tape_->run_for(Cycles(1));
			if(c1540_) c1540_->run_for(Cycles(1));

			return Cycles(1);
		}

		void flush() {
			update_video();
			mos6560_->flush();
		}

		void run_for(const Cycles cycles) override final {
			m6502_.run_for(cycles);
		}

		void setup_output(float aspect_ratio) override final {
			mos6560_.reset(new Vic6560());
			mos6560_->set_high_frequency_cutoff(1600);	// There is a 1.6Khz low-pass filter in the Vic-20.
			// Make a guess: PAL. Without setting a clock rate the 6560 isn't fully set up so contractually something must be set.
			set_memory_map(commodore_target_.memory_model, commodore_target_.region);
		}

		void close_output() override final {
			mos6560_ = nullptr;
		}

		Outputs::CRT::CRT *get_crt() override final {
			return mos6560_->get_crt();
		}

		Outputs::Speaker::Speaker *get_speaker() override final {
			return mos6560_->get_speaker();
		}

		void mos6522_did_change_interrupt_status(void *mos6522) override final {
			m6502_.set_nmi_line(user_port_via_.get_interrupt_line());
			m6502_.set_irq_line(keyboard_via_.get_interrupt_line());
		}

		void type_string(const std::string &string) override final {
			std::unique_ptr<CharacterMapper> mapper(new CharacterMapper());
			Utility::TypeRecipient::add_typer(string, std::move(mapper));
		}

		void tape_did_change_input(Storage::Tape::BinaryTapePlayer *tape) override final {
			keyboard_via_.set_control_line_input(MOS::MOS6522::Port::A, MOS::MOS6522::Line::One, !tape->get_input());
		}

		KeyboardMapper *get_keyboard_mapper() override {
			return &keyboard_mapper_;
		}

		// MARK: - Configuration options.
		std::vector<std::unique_ptr<Configurable::Option>> get_options() override {
			return Commodore::Vic20::get_options();
		}

		void set_selections(const Configurable::SelectionSet &selections_by_option) override {
			bool quickload;
			if(Configurable::get_quick_load_tape(selections_by_option, quickload)) {
				allow_fast_tape_hack_ = quickload;
				set_use_fast_tape();
			}

			Configurable::Display display;
			if(Configurable::get_display(selections_by_option, display)) {
				set_video_signal_configurable(display);
			}
		}

		Configurable::SelectionSet get_accurate_selections() override {
			Configurable::SelectionSet selection_set;
			Configurable::append_quick_load_tape_selection(selection_set, false);
			Configurable::append_display_selection(selection_set, Configurable::Display::Composite);
			return selection_set;
		}

		Configurable::SelectionSet get_user_friendly_selections() override {
			Configurable::SelectionSet selection_set;
			Configurable::append_quick_load_tape_selection(selection_set, true);
			Configurable::append_display_selection(selection_set, Configurable::Display::SVideo);
			return selection_set;
		}

		void set_component_is_sleeping(void *component, bool is_sleeping) override {
			tape_is_sleeping_ = is_sleeping;
			set_use_fast_tape();
		}

	private:
		void update_video() {
			mos6560_->run_for(cycles_since_mos6560_update_.flush());
		}
		Analyser::Static::Commodore::Target commodore_target_;

		CPU::MOS6502::Processor<ConcreteMachine, false> m6502_;

		std::vector<uint8_t>  roms_[9];

		std::vector<uint8_t>  character_rom_;
		std::vector<uint8_t>  basic_rom_;
		std::vector<uint8_t>  kernel_rom_;

		std::vector<uint8_t> rom_;
		uint16_t rom_address_, rom_length_;
		uint8_t ram_[0x8000];
		uint8_t colour_ram_[0x0400];

		std::function<std::vector<std::unique_ptr<std::vector<uint8_t>>>(const std::string &machine, const std::vector<std::string> &names)> rom_fetcher_;

		uint8_t *processor_read_memory_map_[64];
		uint8_t *processor_write_memory_map_[64];
		void write_to_map(uint8_t **map, uint8_t *area, uint16_t address, uint16_t length) {
			address >>= 10;
			length >>= 10;
			while(length--) {
				map[address] = area;
				area += 0x400;
				address++;
			}
		}

		Commodore::Vic20::KeyboardMapper keyboard_mapper_;
		std::vector<std::unique_ptr<Inputs::Joystick>> joysticks_;

		Cycles cycles_since_mos6560_update_;
		std::unique_ptr<Vic6560> mos6560_;
		std::shared_ptr<UserPortVIA> user_port_via_port_handler_;
		std::shared_ptr<KeyboardVIA> keyboard_via_port_handler_;
		std::shared_ptr<SerialPort> serial_port_;
		std::shared_ptr<::Commodore::Serial::Bus> serial_bus_;

		MOS::MOS6522::MOS6522<UserPortVIA> user_port_via_;
		MOS::MOS6522::MOS6522<KeyboardVIA> keyboard_via_;

		// Tape
		std::shared_ptr<Storage::Tape::BinaryTapePlayer> tape_;
		bool use_fast_tape_hack_ = false;
		bool hold_tape_ = false;
		bool allow_fast_tape_hack_ = false;
		bool tape_is_sleeping_ = true;
		void set_use_fast_tape() {
			use_fast_tape_hack_ = !tape_is_sleeping_ && allow_fast_tape_hack_ && tape_->has_tape();
		}

		// Disk
		std::shared_ptr<::Commodore::C1540::Machine> c1540_;
};

}
}

using namespace Commodore::Vic20;

Machine *Machine::Vic20() {
	return new Vic20::ConcreteMachine;
}

Machine::~Machine() {}
