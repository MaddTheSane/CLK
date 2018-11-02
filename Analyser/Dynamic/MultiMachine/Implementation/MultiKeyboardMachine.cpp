//
//  MultiKeyboardMachine.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 09/02/2018.
//  Copyright 2018 Thomas Harte. All rights reserved.
//

#include "MultiKeyboardMachine.hpp"

using namespace Analyser::Dynamic;

MultiKeyboardMachine::MultiKeyboardMachine(const std::vector<std::unique_ptr<::Machine::DynamicMachine>> &machines) :
 	keyboard_(machines_) {
	for(const auto &machine: machines) {
		KeyboardMachine::Machine *keyboard_machine = machine->keyboard_machine();
		if(keyboard_machine) machines_.push_back(keyboard_machine);
	}
}

void MultiKeyboardMachine::clear_all_keys() {
	for(const auto &machine: machines_) {
		machine->clear_all_keys();
	}
}

void MultiKeyboardMachine::set_key_state(uint16_t key, bool is_pressed) {
	for(const auto &machine: machines_) {
		machine->set_key_state(key, is_pressed);
	}
}

void MultiKeyboardMachine::type_string(const std::string &string) {
	for(const auto &machine: machines_) {
		machine->type_string(string);
	}
}

Inputs::Keyboard &MultiKeyboardMachine::get_keyboard() {
	return keyboard_;
}

MultiKeyboardMachine::MultiKeyboard::MultiKeyboard(const std::vector<::KeyboardMachine::Machine *> &machines)
	: machines_(machines) {
	for(const auto &machine: machines_) {
		observed_keys_.insert(machine->get_keyboard().observed_keys().begin(), machine->get_keyboard().observed_keys().end());
		is_exclusive_ |= machine->get_keyboard().is_exclusive();
	}
}

void MultiKeyboardMachine::MultiKeyboard::set_key_pressed(Key key, char value, bool is_pressed) {
	for(const auto &machine: machines_) {
		machine->get_keyboard().set_key_pressed(key, value, is_pressed);
	}
}

void MultiKeyboardMachine::MultiKeyboard::reset_all_keys() {
	for(const auto &machine: machines_) {
		machine->get_keyboard().reset_all_keys();
	}
}

const std::set<Inputs::Keyboard::Key> &MultiKeyboardMachine::MultiKeyboard::observed_keys() {
	return observed_keys_;
}

bool MultiKeyboardMachine::MultiKeyboard::is_exclusive() {
	return is_exclusive_;
}
