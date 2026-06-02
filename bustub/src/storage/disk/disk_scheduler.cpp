//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_scheduler.cpp
//
// Identification: src/storage/disk/disk_scheduler.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/disk/disk_scheduler.h"
#include <vector>
#include "common/macros.h"
#include "storage/disk/disk_manager.h"

namespace bustub {
	/*
	 *	currently spawns background thread in constructor and joins in destructor
	 *	the following still needs implementation:
	 *		Schedule	- push each DiskRequest into request_queue_
	 *				  wrapped by std::optional so the type matches
	 *		StartWorkerThread	- pop 1 optional<DiskRequest> at a time from request_queue
	 *					  nullopt means "stop the loop"
	 *					  otherwise perform read/write and satisfy req's promise
	 *
	 *	concurrency:
	 *		all sync happens through the Channel
	 *		no extra mutexes are needed
	 *		Channel's internal mutex + cv is sufficient because each DiskRequest is owned by 1 thread at any time
	 *		(the producer until Put(), nobody between Put() and Get(), worker until callback fired)
	 *
	 *	promise/future contract:
	 *		set_value must be called *exactly* once per req
	 *		if early return path (e.g. shutdown mid loop), any unprocessed reqs in queue will leak broken_promise exceptions
	 *			to their future holders when promise destroyed
	 *	
	 *	for project 1, task 2
	 *		the most correct approach:
	 *			never short circuit, only stop on nullopt sentinel
	 *			~DiskScheduler requires producer to have stopped producing before destruction
	*/
	
	/*
	 *	@brief		- construct a scheduler + spawn a worker
	 *
	 *	Important:
	 *	UNIMPLEMENTED() aborts process the moment DisScheduler() is instantiated
	 *	delete it
	 *	background_thread_.emplace() after is the real ctor body, capture this via &, dispatching StartWorkerThread()
	*/
	DiskScheduler::DiskScheduler(
		DiskManager *disk_manager
	) : disk_manager_(disk_manager) {
		UNIMPLEMENTED("TODO(P1): Add implementation.");
		// Spawn the background thread
		background_thread_.emplace([&] { 
			StartWorkerThread(); 
		});
	}
	
	/*
	 *	@brief		- stop the scheduler, send shutdown sentinel, join worker
	 *
	 *	already implemented:
	 *		1. put std::nullopt into channel, worker will eventually see as "no more work" + break its loop
	 *		2. join worker thread so its destructor does not UB on us
	 *
	 *	any reqs already queued before nullopt will still run
	 *	(i.e. Channel is FIFO)
	 *	producers must gaurantee they don't race this dtor
	*/
	DiskScheduler::~DiskScheduler() {
		// Put a `std::nullopt` in the queue to signal to exit the loop
		request_queue_.Put(std::nullopt);
		if (background_thread_.has_value()) {
			background_thread_->join();
		}
	}
	
	/**
	 *	TODO(P1): Add implementation
	 *
	 *	@brief Schedules a request for the DiskManager to execute.
	 *
	 *	@param requests The requests to be scheduled.
	 *
	 *	Expected Implementation:
	 *		for ( auto &req: requests ) {
	 *			request_queue_.Put(std::move(req));	// wraps into option implicitly
	 *		}
	 *
	 *	Notes:
	 *		- DiskRequest is move only
	 *		  do *not* copy
	 *		  std::move is mandatory
	 *		- Channel<std::optional<DiskRequest>>::Put takes its argument by value
	 *		  implicit converstion from DiskRequest -> optional kicks in here for free
	 *		- no need to take any extra lock
	 *		  Channel::Put is internally synchronized1
	 *
	 *	Leaderboard Hook:
	 *		this is where we would batch/coalesce/prefetch if we choose to
	 *		but project 1, task 2 does not currently require that
	 */
	void DiskScheduler::Schedule(
		std::vector<DiskRequest> &requests
	) {
		//
	}
	
	/**
	 *	TODO(P1): Add implementation
	 *
	 *	@brief Background worker thread function that processes scheduled requests.
	 *
	 *	The background thread needs to process requests while the DiskScheduler exists, i.e., this function should not
	 *	return until ~DiskScheduler() is called. At that point you need to make sure that the function does return.
	 *
	 *	expected implementation:
	 *		while (true) {
	 *			auto opt = request_queue_.Get();
	 *			if (!opt.has_value()) {
	 *				return;
	 *			}
	 *			DiskRequest req = std::move(*opt);
	 *			if (req.is_write_) {
	 *				disk_manager_->WritePage(req.page_id_, req.data_);
	 *			} else {
	 *				disk_manager_->ReadPage(req.page_id_, req.data_);
	 *			}
	 *			req.callback_.set_value(true);
	 *		}
	 *
	 *	concurrency:
	 *		this thread only runs on a dedicated worker thread
	 *		DiskManager call may take a long time
	 *		producers not locked on us because channel is bounded only by the queue depth
	 *
	 *	failure modes to be aware of:
	 *		- DiskManager methods are void
	 *		  we only have to report partial I/O failure here
	 *		  the convention "we tried, hence mark complete with true regardless"
	 *		  reporting possible failures would require widening the promise's value type (what about masking???)
	 * 		- if never set_value, corresponding future blocks forever
	 * 		  or throws broken_promise on dtor (the test will hang)
	 */
	void DiskScheduler::StartWorkerThread() {
		//
	}
	
}  // namespace bustub
