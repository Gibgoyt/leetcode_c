//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_scheduler.h
//
// Identification: src/include/storage/disk/disk_scheduler.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <future>  // NOLINT
#include <optional>
#include <thread>  // NOLINT
#include <vector>

#include "common/channel.h"
#include "storage/disk/disk_manager.h"

namespace bustub {
	/*
	 *	DiskScheduler
	 *
	 *	DiskScheduler is a single-producer-frienldly + multi-producer-safe thread-pool-of-one in front of DiskManager
	 *	Producers (e.g. BufferPoolManager) hands DiskManager I/O requests
	 *	dedicated background woker dequeues them in FIFO order and dispatches each
	 *		either to DiskManager::ReadPage/DiskManager::WritePage
	 *	completion signalled back to the producer via per-req std::promise<bool>
	 *
	 *		why do we even have a scheduler???
	 *			as opposed to calling DiskManager directly
	 *			- hides the cost of disk I/O behind a future so caller can overlap other work while I/O is in flight
	 *			- serializes access to DiskManager, only the worker thread calls DiskManager, so DiskManager's own locking
	 *			  never has to scale to N worker threads
	 *			- natural extension point for leaderboard optimization
	 *			  (batching, coalescing, prefetching, parallel I/O)
	 *
	 *		lifecycle:
	 *			ctor	- starts a background thread
	 *				  runs StartWorkerThread() in a loop, until told to stop
	 *			dtor	- sends std::nulopt sentinel through the channel
	 *				  worker observes this and returns StartWorkerThread()
	 *				  dtor joins the thread
	 *				  after join, no reqs can ever complete
	 *
	 *		wire format on the channel
	 *			each item is std::optional<DiskRequest>
	 *			- Some(req)	- processes this I/O req
	 *			- None		- shutdown signal
	 *					  worker must break its loop
	 *			  Only the destructor pushes Null
	 *
	 *		concurrency contract:
	 *			- channel is the only sync primitive in the public API
	 *			  producers, and the worker never share other mutable state directly
	 *			- DiskRequest is move-only (i.e. because std::promise is move only)
	 *			  always std::move into the queue, *never* copy
	 *			- once a req is pushed, its promise lives in the queue until the worker calls set_value()
	 *			  do not touch req afterward
	*/

	/**
	 *	@brief Represents a Write or Read request for the DiskManager to execute.
	 *
	 *	DiskRequest is the "work item" of the scheduler
	 *	bundles together everything that the worker needs to perform 1 I/O + signal completion
	 *
	 *		is_write	- true, then WritePage
	 *				  false, then ReadPage
	 *		data_		- pointer to BUSTUB_PAGE_SIZE size buffe
	 *					- write:
	 *					  source of the bytes that go to disk
	 *					  buffer must remain valid until future is satisfied
	 *					- read:
	 *					  destination buffer the bytes are read into
	 *					  worker writes exactly BUSTUB_PAGE_SIZE bytes here
	 *					scheduler does *NOT* own its own memory, typically it is BufferPoolManager's frame
	 *					caller is responsible to keep alive until corresponding future is returned
	 *		page_id_	- which page on disk to read/write to
	 *		callback_	- a std::promise that the worker sets to true when I/O is complete
	 *				  producer typically obtains associated future BEFORE moving promise into the req
	 *				  	(i.e. std::promise is move only, once moved, can not call get_future())
	 *				  then waits for future.get() to observe completion
	 *
	 *	move-only, DiskRequest contains a std::promise, not copyable
	 *	must std::move into containers/channels
	 */
	struct DiskRequest {
		/*
		 *	Flag indicating whether the request is a write or a read. 
		*/
		bool is_write_;
	
		/**
		 *	 Pointer to the start of the memory location where a page is either:
		 *	  1. being read into from disk (on a read).
		 *	  2. being written out to disk (on a write).
		 */
		char *data_;
	
		/** ID of the page being read from / written to disk. */
		page_id_t page_id_;
	
		/*
		 *	Callback used to signal to the request issuer when the request has been completed. 
		 * 	worker should call callback_.set_value(true) *exactly once* per req, after underlying DiskManager call returns
		 * 	calling set_value 2wice/never is a programming error
		*/
		std::promise<bool> callback_;
	};
	
	/**
	 *	@brief The DiskScheduler schedules disk read and write operations.
	 *
	 *	A request is scheduled by calling DiskScheduler::Schedule() with an appropriate DiskRequest object. The scheduler
	 *	maintains a background worker thread that processes the scheduled requests using the disk manager. The background
	 *	thread is created in the DiskScheduler constructor and joined in its destructor.
	 *
	 *	typical usage (@see disk_scheduler_test.cpp)
	 *		auto promise = scheduler->CreatePromise();	// step 1: make promise
	 *		auto future = promise.get_future();	// step 2: grab future *BEFORE* moving promise
	 *		DiskRequest req{is_write, buffer, page_id, std::move(promise)} = std::vector<DiskRequest> v;
	 *		v.push_back(std::move(req));	// step 3: vector of reqs
	 *		scheduler->Schedule(v);		// step 4: hand to scheduler
	 *		bool ok = future.get();		// step 5: block until done
	 *
	 *
	 * 	why does a schedule take a vector??
	 * 		- pairs nicely with BufferPoolManager "evict N pages at once" path
	 * 		- gives leaderboard implementation room to batch/coalesce reqs before dispatching to DiskManager
	 */
	class DiskScheduler {
		public:
			/*
			 *	@brief		- construct DiskScheduler that dispatches to disk_manager
			 *
			 *	spawns background worker thread immediately
			 *	worker runs StartWorkerThread() + joined by ~DiskScheduler
			 *	caller retains ownership of disk_manager
			 *	scheduler only holds a raw pointer
			 *	
			 *	Note:
			 *		disk_scheduler.cpp currenly calls UNIMPLEMENTED() in the constructor
			*/
			explicit DiskScheduler(
				DiskManager *disk_manager
			);

			/*
			 *	@brief		- shuts the scheduler down cleanly
			 *
			 *	pushed std::nullopt sentinel into the queue and joins the worker
			 *	any reqs already in the queue (i.e. queued *before* the sentinel) should still be processed
			 *	sentinel is just FIFO ordered after them
			 *
			 *	res submitted concurrently with ~DiskScheduler() have undefined behaviour
			 *	BufferPoolManager is responsble for not racing dtor
			*/
			~DiskScheduler();
	
			/*
			 *	@brief		- enqueue batch of reqs for the worker to process
			 *
			 *	implementation freedom:
			 *		at min. each DiskRequest must end up in the channel in some order
			 *			- move each req individually with request_queue_.Put()
			 *			- move the whole vector at once
			 *			  would require changing the channel element type
			 *			  worker expects optional<DiskRequest>
			 *
			 *	caller must still own vector after the call
			 *	only move the elements
			 *	vector skeleton (e.g. capacity, .size()), is whatever the caller chose to leave behind
			*/
			void Schedule(
				std::vector<DiskRequest> &requests
			);
	
			/*
			 *	@brief		- the body of the background worker thread
			 *
			 *	loop:
			 *		pull an optional<DiskRequest> from the channel (blocks if empty)
			 *		if nullopt, then return (i.e. shutdown)
			 *		else,
			 *			dispatch to disk_manager->{Read,Write}Page
			 *			based on req.is_write_
			 *			then call req.callback_.set_value(true)
			 *
			 *	must not return until ~DiskManager sends the nullopt sentinel
			*/
			void StartWorkerThread();
	
			using DiskSchedulerPromise = std::promise<bool>;
	
			/**
			 *	@brief Create a Promise object. If you want to implement your own version of promise, you can change this function
			 *	so that our test cases can use your promise implementation.
			 *
			 *	@return std::promise<bool>
			 */
			auto CreatePromise() -> DiskSchedulerPromise { 
				return {}; 
			};
	
			/**
			 *	@brief Deallocates a page on disk.
			 *
			 *	Note: You should look at the documentation for `DeletePage` in `BufferPoolManager` before using this method.
			 *
			 *	@param page_id The page ID of the page to deallocate from disk.
			 *
			 *	Note:
			 *		DeletePage is currently called synchronously on the caller thread (i.e. does not go through queue)
			 *		DiskManager's own latch serializes against concurrent worker read/writes
			 *		if this is changed for leaderboard, please be careful about ordering with in-flight
			 *			reqs on the same page
			 */
			void DeallocatePage(
				page_id_t page_id
			) { 
				disk_manager_->DeletePage(page_id); 
			}
	
		private:
			/*
			 *	Pointer to the disk manager. 
			 *	non-owning
			 *	DiskManager constructed/destroyed by surrounding system (e.g. owned by BufferPoolManager)
			 *	__unused__ attribute only exists because starter file does not reference this member yet
			 *	once StartWorkerThread() is implemented + uses it, attribute becomes harmless decoration
			*/
			DiskManager *disk_manager_ __attribute__((__unused__));

			/*
			 *	A shared queue to concurrently schedule and process requests. 
			 *	When the DiskScheduler's destructor is called, `std::nullopt` is put into 
			 *		the queue to signal to the background thread to stop execution. 
			 *	
			 *	element type is optional<DiskRequest> precisely so we have sentinel value distinct from any real req
			 *	that is the shutdown signal
			*/
			Channel<std::optional<DiskRequest>> request_queue_;

			/*
			 *	The background thread responsible for issuing scheduled requests to the disk manager. 
			 *	stored as optional so the field is default constructible before we decide to spawn the thread
			 *		and so the dtor can check has_value() before joining 
			 *		defensive - ctor always emplaces it
			*/
			std::optional<std::thread> background_thread_;
	};
}  // namespace bustub
