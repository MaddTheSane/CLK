//
//  MasterSystemVDPTests.m
//  Clock SignalTests
//
//  Created by Thomas Harte on 09/10/2018.
//  Copyright © 2018 Thomas Harte. All rights reserved.
//

#import <XCTest/XCTest.h>
#import <OpenGL/OpenGL.h>

#include "9918.hpp"

@interface MasterSystemVDPTests : XCTestCase
@end

@implementation MasterSystemVDPTests {
	NSOpenGLContext *_openGLContext;
}

- (void)setUp {
	[super setUp];

	// Create a valid OpenGL context, so that a VDP can be constructed.
	NSOpenGLPixelFormatAttribute attributes[] =
	{
		NSOpenGLPFAOpenGLProfile,	NSOpenGLProfileVersion3_2Core,
		0
	};

	NSOpenGLPixelFormat *pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
	_openGLContext = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:nil];
	[_openGLContext makeCurrentContext];
}

- (void)tearDown {
	// Put teardown code here. This method is called after the invocation of each test method in the class.
	_openGLContext = nil;

	[super tearDown];
}

- (void)testLineInterrupt {
	TI::TMS::TMS9918 vdp(TI::TMS::Personality::SMSVDP);

	// Disable end-of-frame interrupts, enable line interrupts.
	vdp.set_register(1, 0x00);
	vdp.set_register(1, 0x81);

	vdp.set_register(1, 0x10);
	vdp.set_register(1, 0x80);

	// Set a line interrupt to occur in five lines.
	vdp.set_register(1, 5);
	vdp.set_register(1, 0x8a);

	// Get time until interrupt.
	int time_until_interrupt = vdp.get_time_until_interrupt().as_int() - 1;

	// Check that an interrupt is now scheduled.
	NSAssert(time_until_interrupt != -2, @"No interrupt scheduled");
	NSAssert(time_until_interrupt > 0, @"Interrupt is scheduled in the past");

	// Check interrupt flag isn't set prior to the reported time.
	vdp.run_for(HalfCycles(time_until_interrupt));
	NSAssert(!vdp.get_interrupt_line(), @"Interrupt line went active early [1]");

	// Check interrupt flag is set at the reported time.
	vdp.run_for(HalfCycles(1));
	NSAssert(vdp.get_interrupt_line(), @"Interrupt line wasn't set when promised [1]");

	// Read the status register to clear interrupt status.
	vdp.get_register(1);
	NSAssert(!vdp.get_interrupt_line(), @"Interrupt wasn't reset by status read");

	// Check interrupt flag isn't set prior to the reported time.
	time_until_interrupt = vdp.get_time_until_interrupt().as_int() - 1;
	vdp.run_for(HalfCycles(time_until_interrupt));
	NSAssert(!vdp.get_interrupt_line(), @"Interrupt line went active early [2]");

	// Check interrupt flag is set at the reported time.
	vdp.run_for(HalfCycles(1));
	NSAssert(vdp.get_interrupt_line(), @"Interrupt line wasn't set when promised [2]");
}

- (void)testFirstLineInterrupt {
	TI::TMS::TMS9918 vdp(TI::TMS::Personality::SMSVDP);

	// Disable end-of-frame interrupts, enable line interrupts, set an interrupt to occur every line.
	vdp.set_register(1, 0x00);
	vdp.set_register(1, 0x81);

	vdp.set_register(1, 0x10);
	vdp.set_register(1, 0x80);

	vdp.set_register(1, 0);
	vdp.set_register(1, 0x8a);

	// Advance to outside of the counted area.
	while(vdp.get_current_line() < 200) vdp.run_for(Cycles(228));

	// Clear the pending interrupt and ask about the next one (i.e. the first one).
	vdp.get_register(1);
	int time_until_interrupt = vdp.get_time_until_interrupt().as_int() - 1;

	// Check that an interrupt is now scheduled.
	NSAssert(time_until_interrupt != -2, @"No interrupt scheduled");
	NSAssert(time_until_interrupt > 0, @"Interrupt is scheduled in the past");

	// Check interrupt flag isn't set prior to the reported time.
	vdp.run_for(HalfCycles(time_until_interrupt));
	NSAssert(!vdp.get_interrupt_line(), @"Interrupt line went active early");

	// Check interrupt flag is set at the reported time.
	vdp.run_for(HalfCycles(1));
	NSAssert(vdp.get_interrupt_line(), @"Interrupt line wasn't set when promised");
}

- (void)testInterruptPrediction {
	TI::TMS::TMS9918 vdp(TI::TMS::Personality::SMSVDP);

	for(int c = 0; c < 256; ++c) {
		for(int with_eof = (c < 192) ? 0 : 1; with_eof < 2; ++with_eof) {
			// Enable or disable end-of-frame interrupts as required.
			vdp.set_register(1, with_eof ? 0x20 : 0x00);
			vdp.set_register(1, 0x81);

			// Enable line interrupts.
			vdp.set_register(1, 0x10);
			vdp.set_register(1, 0x80);

			// Set the line interrupt timing as desired.
			vdp.set_register(1, c);
			vdp.set_register(1, 0x8a);

			// Now run through an entire frame...
			int half_cycles = 262*228*2;
			int last_time_until_interrupt = vdp.get_time_until_interrupt().as_int();
			while(half_cycles--) {
				// Validate that an interrupt happened if one was expected, and clear anything that's present.
				NSAssert(vdp.get_interrupt_line() == (last_time_until_interrupt == 0), @"Unexpected interrupt state change; expected %d but got %d; position %d %d @ %d", (last_time_until_interrupt == 0), vdp.get_interrupt_line(), c, with_eof, half_cycles);
				vdp.get_register(1);

				vdp.run_for(HalfCycles(1));

				// Get the time until interrupt.
				int time_until_interrupt = vdp.get_time_until_interrupt().as_int();
				NSAssert(time_until_interrupt != -1, @"No interrupt scheduled; position %d %d @ %d", c, with_eof, half_cycles);
				NSAssert(time_until_interrupt >= 0, @"Interrupt is scheduled in the past; position %d %d @ %d", c, with_eof, half_cycles);

				if(last_time_until_interrupt) {
					NSAssert(time_until_interrupt == (last_time_until_interrupt - 1), @"Discontinuity found in interrupt prediction; from %d to %d; position %d %d @ %d", last_time_until_interrupt, time_until_interrupt, c, with_eof, half_cycles);
				}
				last_time_until_interrupt = time_until_interrupt;
			}
		}
	}
}

- (void)testTimeUntilLine {
	TI::TMS::TMS9918 vdp(TI::TMS::Personality::SMSVDP);

	int time_until_line = vdp.get_time_until_line(-1).as_int();
	for(int c = 0; c < 262*228*5; ++c) {
		vdp.run_for(HalfCycles(1));

		const int time_remaining_until_line = vdp.get_time_until_line(-1).as_int();
		--time_until_line;
		if(time_until_line) {
			NSAssert(time_remaining_until_line == time_until_line, @"Discontinuity found in distance-to-line prediction; expected %d but got %d", time_until_line, time_remaining_until_line);
		}
		time_until_line = time_remaining_until_line;
	}
}

@end
