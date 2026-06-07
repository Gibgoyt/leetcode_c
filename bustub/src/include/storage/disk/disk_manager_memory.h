//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_manager_memory.h
//
// Identification: src/include/storage/disk/disk_manager_memory.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/disk/disk_manager.h"

#include <array>
#include <cassert>
#include <chrono>  // NOLINT
#include <cstring>
#include <fstream>
#include <future>  // NOLINT
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>  // NOLINT
#include <utility>
#include <vector>

#include "common/config.h"
#include "common/exception.h"
#include "common/logger.h"
#include "fmt/core.h"

namespace bustub {
	/*
	 *	two memory-backed DiskManager sub classes
	 *	used to keep tests fast and hermetic (i.e. no actual disk involvement) and as the leaderboard backend
	 *
	 *		DiskManagerMemory	- fixed size flat buffer
	 *					  WritePage/ReadPage are memcpy()
	 *					  asserts on page_id >= capacity
	 *		DiskManagerUnlimitedMemory	- grows on page array demand
	 *						  has per-page shared mutex
	 *						  includes latency simulator simulating delays so leaderboard can stress test
	 *						  	concurrent I/O
	*/
	
	/**
	 *	DiskManagerMemory replicates the utility of DiskManager on memory. It is primarily used for
	 *	data structure performance testing.
	 *
	 *	fixed capacity
	 *	no concurrency control (callers serialize externally)
	`*/
	class DiskManagerMemory : public DiskManager {
		public:
			// allocates (capacity * BUSTUB_PAGE_SIZE) bytes upfront
			explicit DiskManagerMemory(
				size_t capacity
			);
	
			~DiskManagerMemory() override { 
				delete[] memory_; 
			}
	
			void WritePage(
				page_id_t page_id, 
				const char *page_data
			) override;
	
			void ReadPage(
				page_id_t page_id, 
				char *page_data
			) override;
	
		private:
			// owning raw buffer. sized at ctor. never resized.
			char *memory_;
	};
	
	/**
	 * DiskManagerMemory replicates the utility of DiskManager on memory. It is primarily used for
	 * data structure performance testing.
	 *
	 * grow-on-write capacity
	 * per-page shared mutex
	 * optional latency simulation
	*/
	class DiskManagerUnlimitedMemory : public DiskManager {
		public:
			// pre-allocates default page_capacity_ + 1 pages
			DiskManagerUnlimitedMemory();
	
			// writes hold an exclusive lock on per-page shared mutex
			void WritePage(
				page_id_t page_id, 
				const char *page_data
			) override;
	
			/*
			 *	reads hold shared lock on per-page shared mutex
			 *	concurrent reads of the same page can run in parallel
			*/
			void ReadPage(
				page_id_t page_id, 
				char *page_data
			) override;
	
			// no-op (i.e. no file to shrink), counter is not bumped here either
			void DeletePage(
				page_id_t page_id
			) override;
	
			/*
			 *	latency simulator hook
			 *	process latency runs *before* I/O
			 *	it classifies access (e.g. sequential, same block, random) by consulting small ring buffer
			 *		of recent page ids, then sleeps for the appropriate duration
			 *
			 *	PostProcessLatency running after and updates the ring buffer
			 *	both no-op unless EnableLatencySimulator(true)
			*/
			void ProcessLatency(
				page_id_t page_id
			);
	
			void PostProcessLatency(
				page_id_t page_id
			);
	
			/*
			 *	toggles the latency simulator
			 *	used by leaderboard benchmark to add realistic disk costs without involving a real disk
			*/
			void EnableLatencySimulator(
				bool enabled
			) { 
				latency_simulator_enabled_ = enabled; 
			}
	
			/*
			 *	test hook
			 *	returns thread ID of the most recent thread that performed an I/O + clears the field
			 *	tests use this to assert that I/O actually happened on the scheduler's work thread (not on the caller)
			*/
			auto GetLastReadThreadAndClear() -> std::optional<std::thread::id>;
	
		private:
			bool latency_simulator_enabled_{false};
	
			// guards recent_access_ ring buffer for the latency sim only
			std::mutex latency_processor_mutex_;

			// ring buffer of the last 4 page Ids is accessed; used to classify locality
			std::array<page_id_t, 4> recent_access_;

			// insertion into recent_access_ (mod 4)
			uint64_t access_ptr_{0};
	
			using Page = std::array<char, BUSTUB_PAGE_SIZE>;
			
			/*
			 *	page + its own latch
			 *	shared_mutex lets concurrent reads proceed and seralize writes
			*/
			using ProtectedPage = std::pair<Page, std::shared_mutex>;
	
			/*
			 *	guards data_ vector itself (resize/element-pointer assignment) + thread_id_ field
			 *	not held while touching page contents (that is the per page share mutex job)
			*/
			std::mutex mutex_;

			/*
			 *	tracks the most recent thread that performed I/O
			*/
			std::optional<std::thread::id> thread_id_;

			/*
			 *	page storage.
			 *	shared_ptr so that we can read out an element pointer under mutex_, release mutex_, 
			 *		then operate on the page under the shared mutex without holding the outer lock
			*/
			std::vector<std::shared_ptr<ProtectedPage>> data_;
	};
	
}  // namespace bustub
