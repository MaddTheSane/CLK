//
//  Z80Storage.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 01/09/2017.
//  Copyright 2017 Thomas Harte. All rights reserved.
//

/*!
	A repository for all the internal state of a CPU::Z80::Processor; extracted into a separate base
	class in order to remove it from visibility within the main Z80.hpp.
*/

class ProcessorStorage {
	protected:
		struct MicroOp {
			enum Type {
				BusOperation,
				DecodeOperation,
				DecodeOperationNoRChange,
				MoveToNextProgram,

				Increment8,
				Increment16,
				Decrement8,
				Decrement16,
				Move8,
				Move16,

				IncrementPC,

				AssembleAF,
				DisassembleAF,

				And,
				Or,
				Xor,

				TestNZ,
				TestZ,
				TestNC,
				TestC,
				TestPO,
				TestPE,
				TestP,
				TestM,

				ADD16,	ADC16,	SBC16,
				CP8,	SUB8,	SBC8,	ADD8,	ADC8,
				NEG,

				ExDEHL, ExAFAFDash, EXX,

				EI,		DI,		IM,

				LDI,	LDIR,	LDD,	LDDR,
				CPI,	CPIR,	CPD,	CPDR,
				INI,	INIR,	IND,	INDR,
				OUTI,	OUTD,	OUT_R,

				RLA,	RLCA,	RRA,	RRCA,
				RLC,	RRC,	RL,		RR,
				SLA,	SRA,	SLL,	SRL,
				RLD,	RRD,

				SetInstructionPage,
				CalculateIndexAddress,

				BeginNMI,
				BeginIRQ,
				BeginIRQMode0,
				RETN,
				JumpTo66,
				HALT,

				DJNZ,
				DAA,
				CPL,
				SCF,
				CCF,

				RES,
				BIT,
				SET,

				CalculateRSTDestination,

				SetAFlags,
				SetInFlags,
				SetZero,

				IndexedPlaceHolder,

				SetAddrAMemptr,

				Reset
			};
			Type type;
			void *source;
			void *destination;
			PartialMachineCycle machine_cycle;
		};

		struct InstructionPage {
			std::vector<MicroOp *> instructions;
			std::vector<MicroOp> all_operations;
			std::vector<MicroOp> fetch_decode_execute;
			MicroOp *fetch_decode_execute_data;
			uint8_t r_step;
			bool is_indexed;

			InstructionPage() : r_step(1), is_indexed(false) {}
		};

		typedef MicroOp InstructionTable[256][30];

		ProcessorStorage();
		void install_default_instruction_set();

		uint8_t a_;
		RegisterPair16 bc_, de_, hl_;
		RegisterPair16 afDash_, bcDash_, deDash_, hlDash_;
		RegisterPair16 ix_, iy_, pc_, sp_;
		RegisterPair16 ir_, refresh_addr_;
		bool iff1_ = false, iff2_ = false;
		int interrupt_mode_ = 0;
		uint16_t pc_increment_ = 1;
		uint8_t sign_result_;				// the sign flag is set if the value in sign_result_ is negative
		uint8_t zero_result_;				// the zero flag is set if the value in zero_result_ is zero
		uint8_t half_carry_result_;			// the half-carry flag is set if bit 4 of half_carry_result_ is set
		uint8_t bit53_result_;				// the bit 3 and 5 flags are set if the corresponding bits of bit53_result_ are set
		uint8_t parity_overflow_result_;	// the parity/overflow flag is set if the corresponding bit of parity_overflow_result_ is set
		uint8_t subtract_flag_;				// contains a copy of the subtract flag in isolation
		uint8_t carry_result_;				// the carry flag is set if bit 0 of carry_result_ is set
		uint8_t halt_mask_ = 0xff;

		unsigned int flag_adjustment_history_ = 0;	// a shifting record of whether each opcode set any flags; it turns out
													// that knowledge of what the last opcode did is necessary to get bits 5 & 3
													// correct for SCF and CCF.

		HalfCycles number_of_cycles_;

		enum Interrupt: uint8_t {
			IRQ			= 0x01,
			NMI			= 0x02,
			Reset		= 0x04,
			PowerOn		= 0x08
		};
		uint8_t request_status_ = Interrupt::PowerOn;
		uint8_t last_request_status_ = Interrupt::PowerOn;
		bool irq_line_ = false, nmi_line_ = false;
		bool bus_request_line_ = false;
		bool wait_line_ = false;

		uint8_t operation_;
		RegisterPair16 temp16_, memptr_;
		uint8_t temp8_;

		const MicroOp *scheduled_program_counter_ = nullptr;

		std::vector<MicroOp> conditional_call_untaken_program_;
		std::vector<MicroOp> reset_program_;
		std::vector<MicroOp> irq_program_[3];
		std::vector<MicroOp> nmi_program_;
		InstructionPage *current_instruction_page_;

		InstructionPage base_page_;
		InstructionPage ed_page_;
		InstructionPage fd_page_;
		InstructionPage dd_page_;

		InstructionPage cb_page_;
		InstructionPage fdcb_page_;
		InstructionPage ddcb_page_;

		/*!
			Gets the flags register.

			@see set_flags

			@returns The current value of the flags register.
		*/
		uint8_t get_flags() {
			uint8_t result =
				(sign_result_ & Flag::Sign) |
				(zero_result_ ? 0 : Flag::Zero) |
				(bit53_result_ & (Flag::Bit5 | Flag::Bit3)) |
				(half_carry_result_ & Flag::HalfCarry) |
				(parity_overflow_result_ & Flag::Parity) |
				subtract_flag_ |
				(carry_result_ & Flag::Carry);
			return result;
		}

		/*!
			Sets the flags register.

			@see set_flags

			@param flags The new value of the flags register.
		*/
		void set_flags(uint8_t flags) {
			sign_result_			= flags;
			zero_result_			= (flags & Flag::Zero) ^ Flag::Zero;
			bit53_result_			= flags;
			half_carry_result_		= flags;
			parity_overflow_result_	= flags;
			subtract_flag_			= flags & Flag::Subtract;
			carry_result_			= flags;
		}

		virtual void assemble_page(InstructionPage &target, InstructionTable &table, bool add_offsets) = 0;
		virtual void copy_program(const MicroOp *source, std::vector<MicroOp> &destination) = 0;

		void assemble_fetch_decode_execute(InstructionPage &target, int length);
		void assemble_ed_page(InstructionPage &target);
		void assemble_cb_page(InstructionPage &target, RegisterPair16 &index, bool add_offsets);
		void assemble_base_page(InstructionPage &target, RegisterPair16 &index, bool add_offsets, InstructionPage &cb_page);

};
