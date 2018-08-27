//
//  CSJoystickManager.m
//  Clock Signal
//
//  Created by Thomas Harte on 19/07/2018.
//  Copyright © 2018 Thomas Harte. All rights reserved.
//

#import "CSJoystickManager.h"

@import IOKit;
#include <IOKit/hid/IOHIDLib.h>

#pragma mark - CSJoystickButton

@implementation CSJoystickButton {
	IOHIDElementRef _element;
}

- (instancetype)initWithElement:(IOHIDElementRef)element index:(NSInteger)index {
	self = [super init];
	if(self) {
		_index = index;
		_element = (IOHIDElementRef)CFRetain(element);
	}
	return self;
}

- (void)dealloc {
	CFRelease(_element);
}

- (NSString *)description {
	return [NSString stringWithFormat:@"<CSJoystickButton: %p>; button %ld, %@", self, (long)self.index, self.isPressed ? @"pressed" : @"released"];
}

- (IOHIDElementRef)element {
	return _element;
}

- (void)setIsPressed:(bool)isPressed {
	_isPressed = isPressed;
}

@end

#pragma mark - CSJoystickAxis

@implementation CSJoystickAxis {
	IOHIDElementRef _element;
}

- (instancetype)initWithElement:(IOHIDElementRef)element type:(CSJoystickAxisType)type {
	self = [super init];
	if(self) {
		_element = (IOHIDElementRef)CFRetain(element);
		_type = type;
		_position = 0.5f;
	}
	return self;
}

- (void)dealloc {
	CFRelease(_element);
}

- (NSString *)description {
	return [NSString stringWithFormat:@"<CSJoystickAxis: %p>; type %d, value %0.2f", self, (int)self.type, self.position];
}

- (IOHIDElementRef)element {
	return _element;
}

- (void)setPosition:(float)position {
	_position = position;
}

@end

#pragma mark - CSJoystickHat

@implementation CSJoystickHat {
	IOHIDElementRef _element;
}

- (instancetype)initWithElement:(IOHIDElementRef)element {
	self = [super init];
	if(self) {
		_element = (IOHIDElementRef)CFRetain(element);
	}
	return self;
}

- (void)dealloc {
	CFRelease(_element);
}

- (NSString *)description {
	return [NSString stringWithFormat:@"<CSJoystickHat: %p>; direction %ld", self, (long)self.direction];
}

- (IOHIDElementRef)element {
	return _element;
}

- (void)setDirection:(CSJoystickHatDirection)direction {
	_direction = direction;
}

@end

#pragma mark - CSJoystick

@implementation CSJoystick {
	IOHIDDeviceRef _device;
}

- (instancetype)initWithButtons:(NSArray<CSJoystickButton *> *)buttons
	axes:(NSArray<CSJoystickAxis *> *)axes
	hats:(NSArray<CSJoystickHat *> *)hats
	device:(IOHIDDeviceRef)device {
	self = [super init];
	if(self) {
		// Sort buttons by index.
		_buttons = [buttons sortedArrayUsingDescriptors:@[[NSSortDescriptor sortDescriptorWithKey:@"index" ascending:YES]]];

		// Sort axes by enum value.
		_axes = [axes sortedArrayUsingDescriptors:@[[NSSortDescriptor sortDescriptorWithKey:@"type" ascending:YES]]];

		// Hats have no guaranteed ordering.
		_hats = hats;

		// Keep hold of the device.
		_device = (IOHIDDeviceRef)CFRetain(device);
	}
	return self;
}

- (void)dealloc {
	CFRelease(_device);
}

- (NSString *)description {
	return [NSString stringWithFormat:@"<CSJoystick: %p>; buttons %@, axes %@, hats %@", self, self.buttons, self.axes, self.hats];
}

- (void)update {
	// Update buttons.
	for(CSJoystickButton *button in _buttons) {
		IOHIDValueRef value;
		if(IOHIDDeviceGetValue(_device, button.element, &value) == kIOReturnSuccess) {
			// Some pressure-sensitive buttons return values greater than 1 for hard presses,
			// but this class rationalised everything to Boolean.
			button.isPressed = !!IOHIDValueGetIntegerValue(value);
		}
	}

	// Update hats.
	for(CSJoystickHat *hat in _hats) {
		IOHIDValueRef value;
		if(IOHIDDeviceGetValue(_device, hat.element, &value) == kIOReturnSuccess) {
			// Hats report a direction, which is either one of eight or one of four.
			CFIndex integerValue = IOHIDValueGetIntegerValue(value) - IOHIDElementGetLogicalMin(hat.element);
			const CFIndex range = 1 + IOHIDElementGetLogicalMax(hat.element) - IOHIDElementGetLogicalMin(hat.element);
			integerValue *= 8 / range;

			// Map from the HID direction to the bit field.
			switch(integerValue) {
				default:	hat.direction = 0;															break;
				case 0:		hat.direction = CSJoystickHatDirectionUp;									break;
				case 1:		hat.direction = CSJoystickHatDirectionUp | CSJoystickHatDirectionRight;		break;
				case 2:		hat.direction = CSJoystickHatDirectionRight;								break;
				case 3:		hat.direction = CSJoystickHatDirectionRight | CSJoystickHatDirectionDown;	break;
				case 4:		hat.direction = CSJoystickHatDirectionDown;									break;
				case 5:		hat.direction = CSJoystickHatDirectionDown | CSJoystickHatDirectionLeft;	break;
				case 6:		hat.direction = CSJoystickHatDirectionLeft;									break;
				case 7:		hat.direction = CSJoystickHatDirectionLeft | CSJoystickHatDirectionUp;		break;
			}
		}
	}

	// Update axes.
	for(CSJoystickAxis *axis in _axes) {
		IOHIDValueRef value;
		if(IOHIDDeviceGetValue(_device, axis.element, &value) == kIOReturnSuccess) {
			const CFIndex integerValue = IOHIDValueGetIntegerValue(value) - IOHIDElementGetLogicalMin(axis.element);
			const CFIndex range = 1 + IOHIDElementGetLogicalMax(axis.element) - IOHIDElementGetLogicalMin(axis.element);
			axis.position = (float)integerValue / (float)range;
		}
	}
}

- (IOHIDDeviceRef)device {
	return _device;
}

@end

#pragma mark - CSJoystickManager

@interface CSJoystickManager ()
- (void)deviceMatched:(IOHIDDeviceRef)device result:(IOReturn)result sender:(void *)sender;
- (void)deviceRemoved:(IOHIDDeviceRef)device result:(IOReturn)result sender:(void *)sender;
@end

static void DeviceMatched(void *context, IOReturn result, void *sender, IOHIDDeviceRef device) {
	[(__bridge  CSJoystickManager *)context deviceMatched:device result:result sender:sender];
}

static void DeviceRemoved(void *context, IOReturn result, void *sender, IOHIDDeviceRef device) {
	[(__bridge  CSJoystickManager *)context deviceRemoved:device result:result sender:sender];
}

@implementation CSJoystickManager {
	IOHIDManagerRef _hidManager;
	NSMutableArray<CSJoystick *> *_joysticks;
}

- (instancetype)init {
	self = [super init];
	if(self) {
		_joysticks = [[NSMutableArray alloc] init];

		_hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
		if(!_hidManager) return nil;

		NSArray<NSDictionary<NSString *, NSNumber *> *> *const multiple = @[
			@{ @kIOHIDDeviceUsagePageKey: @(kHIDPage_GenericDesktop), @kIOHIDDeviceUsageKey: @(kHIDUsage_GD_Joystick) },
			@{ @kIOHIDDeviceUsagePageKey: @(kHIDPage_GenericDesktop), @kIOHIDDeviceUsageKey: @(kHIDUsage_GD_GamePad) },
			@{ @kIOHIDDeviceUsagePageKey: @(kHIDPage_GenericDesktop), @kIOHIDDeviceUsageKey: @(kHIDUsage_GD_MultiAxisController) },
		];

		IOHIDManagerSetDeviceMatchingMultiple(_hidManager, (__bridge CFArrayRef)multiple);
		IOHIDManagerRegisterDeviceMatchingCallback(_hidManager, DeviceMatched, (__bridge void *)self);
		IOHIDManagerRegisterDeviceRemovalCallback(_hidManager, DeviceRemoved, (__bridge void *)self);
		IOHIDManagerScheduleWithRunLoop(_hidManager, CFRunLoopGetMain(), kCFRunLoopDefaultMode);

		if(IOHIDManagerOpen(_hidManager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
			NSLog(@"Failed to open HID manager");
			// something
			return nil;
		}
	}

	return self;
}

- (void)dealloc {
	IOHIDManagerUnscheduleFromRunLoop(_hidManager, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
	IOHIDManagerClose(_hidManager, kIOHIDOptionsTypeNone);
	CFRelease(_hidManager);
}

- (void)deviceMatched:(IOHIDDeviceRef)device result:(IOReturn)result sender:(void *)sender {
	// Double check this joystick isn't already known.
	for(CSJoystick *joystick in _joysticks) {
		if(joystick.device == device) return;
	}

	// Prepare to collate a list of buttons, axes and hats for the new device.
	NSMutableArray<CSJoystickButton *> *buttons = [[NSMutableArray alloc] init];
	NSMutableArray<CSJoystickAxis *> *axes = [[NSMutableArray alloc] init];
	NSMutableArray<CSJoystickHat *> *hats = [[NSMutableArray alloc] init];

	// Inspect all elements for those that are comprehensible to this code.
	const CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device, NULL, kIOHIDOptionsTypeNone);
	for(CFIndex index = 0; index < CFArrayGetCount(elements); ++index) {
		const IOHIDElementRef element = (IOHIDElementRef)CFArrayGetValueAtIndex(elements, index);

		// Check that this element is either on the generic desktop page or else is a button.
		const uint32_t usagePage = IOHIDElementGetUsagePage(element);
		if(usagePage != kHIDPage_GenericDesktop && usagePage != kHIDPage_Button) continue;

		// Then inspect the type.
		switch(IOHIDElementGetType(element)) {
			default: break;

			case kIOHIDElementTypeInput_Button: {
				// Add a button; pretty easy stuff. 'Usage' provides a button index.
				const uint32_t usage = IOHIDElementGetUsage(element);
				[buttons addObject:[[CSJoystickButton alloc] initWithElement:element index:usage]];
			} break;

			case kIOHIDElementTypeInput_Misc:
			case kIOHIDElementTypeInput_Axis: {
				CSJoystickAxisType axisType;
				switch(IOHIDElementGetUsage(element)) {
					default: continue;

					// Three analogue axes are implemented here; there are another three sets
					// of these that could be parsed in the future if interesting.
					case kHIDUsage_GD_X:	axisType = CSJoystickAxisTypeX;		break;
					case kHIDUsage_GD_Y:	axisType = CSJoystickAxisTypeY;		break;
					case kHIDUsage_GD_Z:	axisType = CSJoystickAxisTypeZ;		break;

					// A hatswitch is a multi-directional control all of its own.
					case kHIDUsage_GD_Hatswitch:
						[hats addObject:[[CSJoystickHat alloc] initWithElement:element]];
					continue;
				}

				// Add the axis; if it was a hat switch or unrecognised then the code doesn't
				// reach here.
				[axes addObject:[[CSJoystickAxis alloc] initWithElement:element type:axisType]];
			} break;
		}
	}
	CFRelease(elements);

	// Add this joystick to the list.
	[_joysticks addObject:[[CSJoystick alloc] initWithButtons:buttons axes:axes hats:hats device:device]];
}

- (void)deviceRemoved:(IOHIDDeviceRef)device result:(IOReturn)result sender:(void *)sender {
	// If this joystick was recorded, remove it.
	for(CSJoystick *joystick in [_joysticks copy]) {
		if(joystick.device == device) {
			[_joysticks removeObject:joystick];
			return;
		}
	}
}

- (void)update {
	[self.joysticks makeObjectsPerformSelector:@selector(update)];
}

- (NSArray<CSJoystick *> *)joysticks {
	return [_joysticks copy];
}

@end
