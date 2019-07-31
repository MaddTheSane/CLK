//
//  68000.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 08/03/2019.
//  Copyright © 2019 Thomas Harte. All rights reserved.
//

#ifndef MC68000_h
#define MC68000_h

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <ostream>
#include <vector>

#include "../../ClockReceiver/ForceInline.hpp"
#include "../../ClockReceiver/ClockReceiver.hpp"
#include "../RegisterSizes.hpp"

namespace CPU {
namespace MC68000 {

/*!
	A microcycle is an atomic unit of 68000 bus activity — it is a single item large enough
	fully to specify a sequence of bus events that occur without any possible interruption.

	Concretely, a standard read cycle breaks down into at least two microcycles:

		1) a 4 half-cycle length microcycle in which the address strobe is signalled; and
		2) a 4 half-cycle length microcycle in which at least one of the data strobes is
		signalled, and the data bus is sampled.

	That is, assuming DTack were signalled when microcycle (1) ended. If not then additional
	wait state microcycles would fall between those two parts.

	The 68000 data sheet defines when the address becomes valid during microcycle (1), and
	when the address strobe is actually asserted. But those timings are fixed. So simply
	telling you that this was a microcycle during which the address trobe was signalled is
	sufficient fully to describe the bus activity.

	(Aside: see the 68000 template's definition for options re: implicit DTack; if your
	68000 owner can always predict exactly how long it will hold DTack following observation
	of an address-strobing microcycle, it can just supply those periods for accounting and
	avoid the runtime cost of actual DTack emulation. But such as the bus allows.)
*/
struct Microcycle {
	/// A NewAddress cycle is one in which the address strobe is initially low but becomes high;
	/// this correlates to states 0 to 5 of a standard read/write cycle.
	static const int NewAddress				= 1 << 0;

	/// A SameAddress cycle is one in which the address strobe is continuously asserted, but neither
	/// of the data strobes are.
	static const int SameAddress			= 1 << 1;

	/// A Reset cycle is one in which the RESET output is asserted.
	static const int Reset					= 1 << 2;

	/// Indicates that the address and both data select strobes are active.
	static const int SelectWord				= 1 << 3;

	/// Indicates that the address strobe and exactly one of the data strobes are active; you can determine
	/// which by inspecting the low bit of the provided address. The RW line indicates a read.
	static const int SelectByte				= 1 << 4;

	/// If set, indicates a read. Otherwise, a write.
	static const int Read 					= 1 << 5;

	/// Contains the value of line FC0 if it is not implicit via InterruptAcknowledge.
	static const int IsData 				= 1 << 6;

	/// Contains the value of line FC1 if it is not implicit via InterruptAcknowledge.
	static const int IsProgram 				= 1 << 7;

	/// The interrupt acknowledge cycle is that during which the 68000 seeks to obtain the vector for
	/// an interrupt it plans to observe. Noted on a real 68000 by all FCs being set to 1.
	static const int InterruptAcknowledge	= 1 << 8;

	/// Represents the state of the 68000's valid memory address line — indicating whether this microcycle
	/// is synchronised with the E clock to satisfy a valid peripheral address request.
	static const int IsPeripheral 			= 1 << 9;

	/// Contains a valid combination of the various static const int flags, describing the operation
	/// performed by this Microcycle.
	int operation = 0;

	/// Describes the duration of this Microcycle.
	HalfCycles length = HalfCycles(4);

	/*!
		For expediency, this provides a full 32-bit byte-resolution address — e.g.
		if reading indirectly via an address register, this will indicate the full
		value of the address register.

		The receiver should ignore bits 0 and 24+. Use word_address() automatically
		to obtain the only the 68000's real address lines, giving a 23-bit address
		at word resolution.
	*/
	const uint32_t *address = nullptr;

	/*!
		If this is a write cycle, dereference value to get the value loaded onto
		the data bus.

		If this is a read cycle, write the value on the data bus to it.

		Otherwise, this value is undefined.

		Byte values are provided via @c value.halves.low. @c value.halves.high is undefined.
		This is true regardless of whether the upper or lower byte of a word is being
		accessed.

		Word values occupy the entirety of @c value.full.
	*/
	RegisterPair16 *value = nullptr;

	/// @returns @c true if two Microcycles are equal; @c false otherwise.
	bool operator ==(const Microcycle &rhs) const {
		if(value != rhs.value) return false;
		if(address != rhs.address) return false;
		if(length != rhs.length) return false;
		if(operation != rhs.operation) return false;
		return true;
	}

	// Various inspectors.

	/*! @returns true if any data select line is active; @c false otherwise. */
	forceinline bool data_select_active() const {
		return bool(operation & (SelectWord | SelectByte | InterruptAcknowledge));
	}

	/*!
		@returns 0 if this byte access wants the low part of a 16-bit word; 8 if it wants the high part.
	*/
	forceinline unsigned int byte_shift() const {
		return (((*address) & 1) << 3) ^ 8;
	}

	/*!
		Obtains the mask to apply to a word that will leave only the byte this microcycle is selecting.

		@returns 0x00ff if this byte access wants the low part of a 16-bit word; 0xff00 if it wants the high part.
	*/
	forceinline uint16_t byte_mask() const {
		return uint16_t(0xff00) >> (((*address) & 1) << 3);
	}

	/*!
		Obtains the mask to apply to a word that will leave only the byte this microcycle **isn't** selecting.
		i.e. this is the part of a word that should be untouched by this microcycle.

		@returns 0xff00 if this byte access wants the low part of a 16-bit word; 0x00ff if it wants the high part.
	*/
	forceinline uint16_t untouched_byte_mask() const {
		return uint16_t(uint16_t(0xff) << (((*address) & 1) << 3));
	}

	/*!
		Assuming this cycle is a byte write, mutates @c destination by writing the byte to the proper upper or
		lower part, retaining the other half.
	*/
	forceinline uint16_t write_byte(uint16_t destination) const {
		return uint16_t((destination & untouched_byte_mask()) | (value->halves.low << byte_shift()));
	}

	/*!
		@returns non-zero if this is a byte read and 68000 LDS is asserted.
	*/
	forceinline int lower_data_select() const {
		return (operation & SelectByte) & ((*address & 1) << 3);
	}

	/*!
		@returns non-zero if this is a byte read and 68000 UDS is asserted.
	*/
	forceinline int upper_data_select() const {
		return (operation & SelectByte) & ~((*address & 1) << 3);
	}

	/*!
		@returns the address being accessed at the precision a 68000 supplies it —
		only 24 address bit precision, with the low bit shifted out. So it's the
		68000 address at word precision: address 0 is the first word in the address
		space, address 1 is the second word (i.e. the third and fourth bytes) in
		the address space, etc.
	*/
	forceinline uint32_t word_address() const {
		return (address ? (*address) & 0x00fffffe : 0) >> 1;
	}

	/*!
		@returns the same value as word_address() for any Microcycle with the NewAddress or
		SameAddress flags set; undefined behaviour otherwise.
	*/
	forceinline uint32_t active_operation_word_address() const {
		return ((*address) & 0x00fffffe) >> 1;
	}

#ifndef NDEBUG
	bool is_resizeable = false;
#endif
};

/*!
	This is the prototype for a 68000 bus handler; real bus handlers can descend from this
	in order to get default implementations of any changes that may occur in the expected interface.
*/
class BusHandler {
	public:
		/*!
			Provides the bus handler with a single Microcycle to 'perform'.

			FC0 and FC1 are provided inside the microcycle as the IsData and IsProgram
			flags; FC2 is provided here as is_supervisor — it'll be either 0 or 1.
		*/
		HalfCycles perform_bus_operation(const Microcycle &cycle, int is_supervisor) {
			return HalfCycles(0);
		}

		void flush() {}

		/*!
			Provides information about the path of execution if enabled via the template.
		*/
		void will_perform(uint32_t address, uint16_t opcode) {}
};

#include "Implementation/68000Storage.hpp"

class ProcessorBase: public ProcessorStorage {
};

enum Flag: uint16_t {
	Trace		= 0x8000,
	Supervisor	= 0x2000,

	ConditionCodes	= 0x1f,

	Extend		= 0x0010,
	Negative	= 0x0008,
	Zero		= 0x0004,
	Overflow	= 0x0002,
	Carry		= 0x0001
};

struct ProcessorState {
	uint32_t data[8];
	uint32_t address[7];
	uint32_t user_stack_pointer, supervisor_stack_pointer;
	uint32_t program_counter;
	uint16_t status;

	/*!
		@returns the supervisor stack pointer if @c status indicates that
			the processor is in supervisor mode; the user stack pointer otherwise.
	*/
	uint32_t stack_pointer() const {
		return (status & Flag::Supervisor) ? supervisor_stack_pointer : user_stack_pointer;
	}

	// TODO: More state needed to indicate current instruction, the processor's
	// progress through it, and anything it has fetched so far.
//			uint16_t current_instruction;
};

template <class T, bool dtack_is_implicit, bool signal_will_perform = false> class Processor: public ProcessorBase {
	public:
		Processor(T &bus_handler) : ProcessorBase(), bus_handler_(bus_handler) {}

		void run_for(HalfCycles duration);

		using State = ProcessorState;
		/// @returns The current processor state.
		State get_state();

		/// Sets the processor to the supplied state.
		void set_state(const State &);

		/// Sets the DTack line — @c true for active, @c false for inactive.
		inline void set_dtack(bool dtack) {
			dtack_ = dtack;
		}

		/// Sets the VPA (valid peripheral address) line — @c true for active, @c false for inactive.
		inline void set_is_peripheral_address(bool is_peripheral_address) {
			is_peripheral_address_ = is_peripheral_address;
		}

		/// Sets the bus error line — @c true for active, @c false for inactive.
		inline void set_bus_error(bool bus_error) {
			bus_error_ = bus_error;
		}

		/// Sets the interrupt lines, IPL0, IPL1 and IPL2.
		inline void set_interrupt_level(int interrupt_level) {
			bus_interrupt_level_ = interrupt_level;
		}

		/// Sets the bus request line.
		/// This are of functionality is TODO.
		inline void set_bus_request(bool bus_request) {
			bus_request_ = bus_request;
		}

		/// Sets the bus acknowledge line.
		/// This are of functionality is TODO.
		inline void set_bus_acknowledge(bool bus_acknowledge) {
			bus_acknowledge_ = bus_acknowledge;
		}

		/// Sets the halt line.
		inline void set_halt(bool halt) {
			halt_ = halt;
		}

	private:
		T &bus_handler_;
};

#include "Implementation/68000Implementation.hpp"

}
}

#endif /* MC68000_h */
