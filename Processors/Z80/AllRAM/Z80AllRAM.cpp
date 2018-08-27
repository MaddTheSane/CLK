//
//  Z80AllRAM.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 16/05/2017.
//  Copyright 2017 Thomas Harte. All rights reserved.
//

#include "Z80AllRAM.hpp"
#include <algorithm>

using namespace CPU::Z80;
namespace {

class ConcreteAllRAMProcessor: public AllRAMProcessor, public BusHandler {
	public:
		ConcreteAllRAMProcessor() : AllRAMProcessor(), z80_(*this) {}

		inline HalfCycles perform_machine_cycle(const PartialMachineCycle &cycle) {
			timestamp_ += cycle.length;
			if(!cycle.is_terminal()) {
				return HalfCycles(0);
			}

			uint16_t address = cycle.address ? *cycle.address : 0x0000;
			switch(cycle.operation) {
				case PartialMachineCycle::ReadOpcode:
					check_address_for_trap(address);
				case PartialMachineCycle::Read:
					*cycle.value = memory_[address];
				break;
				case PartialMachineCycle::Write:
					memory_[address] = *cycle.value;
				break;

				case PartialMachineCycle::Output:
				break;
				case PartialMachineCycle::Input:
					// This logic is selected specifically because it seems to match
					// the FUSE unit tests. It might need factoring out.
					*cycle.value = address >> 8;
				break;

				case PartialMachineCycle::Internal:
				case PartialMachineCycle::Refresh:
				break;

				case PartialMachineCycle::Interrupt:
					// A pick that means LD HL, (nn) if interpreted as an instruction but is otherwise
					// arbitrary.
					*cycle.value = 0x21;
				break;

				default:
					printf("???\n");
				break;
			}

			if(delegate_ != nullptr) {
				delegate_->z80_all_ram_processor_did_perform_bus_operation(*this, cycle.operation, address, cycle.value ? *cycle.value : 0x00, timestamp_);
			}

			return HalfCycles(0);
		}

		void run_for(const Cycles cycles) {
			z80_.run_for(cycles);
		}

		uint16_t get_value_of_register(Register r) {
			return z80_.get_value_of_register(r);
		}

		void set_value_of_register(Register r, uint16_t value) {
			z80_.set_value_of_register(r, value);
		}

		bool get_halt_line() {
			return z80_.get_halt_line();
		}

		void reset_power_on() {
			return z80_.reset_power_on();
		}

		void set_interrupt_line(bool value) {
			z80_.set_interrupt_line(value);
		}

		void set_non_maskable_interrupt_line(bool value) {
			z80_.set_non_maskable_interrupt_line(value);
		}

		void set_wait_line(bool value) {
			z80_.set_wait_line(value);
		}

	private:
		CPU::Z80::Processor<ConcreteAllRAMProcessor, false, true> z80_;
};

}

AllRAMProcessor *AllRAMProcessor::Processor() {
	return new ConcreteAllRAMProcessor;
}
