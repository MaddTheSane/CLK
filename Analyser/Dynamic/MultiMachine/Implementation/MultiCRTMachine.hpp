//
//  MultiCRTMachine.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 29/01/2018.
//  Copyright 2018 Thomas Harte. All rights reserved.
//

#ifndef MultiCRTMachine_hpp
#define MultiCRTMachine_hpp

#include "../../../../Concurrency/AsyncTaskQueue.hpp"
#include "../../../../Machines/CRTMachine.hpp"
#include "../../../../Machines/DynamicMachine.hpp"

#include "MultiSpeaker.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace Analyser {
namespace Dynamic {

/*!
	Provides a class that multiplexes the CRT machine interface to multiple machines.

	Keeps a reference to the original vector of machines; will access it only after
	acquiring a supplied mutex. The owner should also call did_change_machine_order()
	if the order of machines changes.
*/
class MultiCRTMachine: public CRTMachine::Machine {
	public:
		MultiCRTMachine(const std::vector<std::unique_ptr<::Machine::DynamicMachine>> &machines, std::mutex &machines_mutex);

		/*!
			Informs the MultiCRTMachine that the order of machines has changed; the MultiCRTMachine
			uses this as an opportunity to synthesis any CRTMachine::Machine::Delegate messages that
			are necessary to bridge the gap between one machine and the next.
		*/
		void did_change_machine_order();

		/*!
			Provides a mechanism by which a delegate can be informed each time a call to run_for has
			been received.
		*/
		struct Delegate {
			virtual void multi_crt_did_run_machines() = 0;
		};
		/// Sets @c delegate as the receiver of delegate messages.
		void set_delegate(Delegate *delegate) {
			delegate_ = delegate;
		}

		// Below is the standard CRTMachine::Machine interface; see there for documentation.
		void setup_output(float aspect_ratio) override;
		void close_output() override;
		Outputs::CRT::CRT *get_crt() override;
		Outputs::Speaker::Speaker *get_speaker() override;
		void run_for(Time::Seconds duration) override;

	private:
		void run_for(const Cycles cycles) override {}
		const std::vector<std::unique_ptr<::Machine::DynamicMachine>> &machines_;
		std::mutex &machines_mutex_;
		std::vector<Concurrency::AsyncTaskQueue> queues_;
		MultiSpeaker *speaker_ = nullptr;
		Delegate *delegate_ = nullptr;

		/*!
			Performs a parallel for operation across all machines, performing the supplied
			function on each and returning only once all applications have completed.

			No guarantees are extended as to which thread operations will occur on.
		*/
		void perform_parallel(const std::function<void(::CRTMachine::Machine *)> &);

		/*!
			Performs a serial for operation across all machines, performing the supplied
			function on each on the calling thread.
		*/
		void perform_serial(const std::function<void(::CRTMachine::Machine *)> &);
};

}
}


#endif /* MultiCRTMachine_hpp */
