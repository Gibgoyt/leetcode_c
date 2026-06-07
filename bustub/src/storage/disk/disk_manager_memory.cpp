//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_manager_memory.cpp
//
// Identification: src/storage/disk/disk_manager_memory.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/disk/disk_manager_memory.h"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>  // NOLINT

#include "common/exception.h"
#include "common/logger.h"
#include "common/macros.h"

namespace bustub {
	/*
	 *	Memory backed DiskManageer implementation
	 *
	 *	this file does *NOT* need any modification for anything in project 1
	 *	leaderboard test users DiskManageUnlimitedMemory + latency sim to mimic disk costs without disk
	 *
	 *	locking:
	 *			mutex_		: guards data_ resizes and thread_id_
	 *			per page shared mutex:
	 *					in ProtectedPage
	 *					guards each page's bytes
	 *					shared for reading 
	 *					exclusive for writing
	 *					concurent readers parallize
	 *			latency_processor_mutex_	:
	 *					guards recent_access_ ring buffer for the latency sim
	*/
	
	/**
	 *	Constructor: used for memory based manager
	 *
	 *	sets page_capacity_ for use by GetDbFileSize and friends
	 *	then allocates a single large flat buffer for all pages
	 */
	DiskManagerMemory::DiskManagerMemory(
		size_t capacity
	) {
		page_capacity_ = capacity;
		memory_ = new char[capacity *	BUSTUB_PAGE_SIZE];
	}
	
	/**
	 *	Write a page to the database file.
	 *	@param page_id id of the page
	 *	@param page_data raw page data
	 *
	 *	asserts page_id < capacity
	 *	pure memcpy(), no locking, which means callers must serialize when using this class
	 */
	void DiskManagerMemory::WritePage(
		page_id_t page_id, 
		const char *page_data
	) {
		BUSTUB_ASSERT(static_cast<size_t>(page_id) < page_capacity_,
			"Ran out of disk space for limited memory disk manager implementation");
		size_t offset = static_cast<size_t>(page_id) *	BUSTUB_PAGE_SIZE;
		// set write cursor to offset
		num_writes_ += 1;
		memcpy(memory_ + offset, page_data, BUSTUB_PAGE_SIZE);
	}
	
	/**
	 *	Read a page from the database file.
	 *	@param page_id id of the page
	 *	@param[out] page_data output buffer
	 *
	 *	no bounds check;
	 *	no locking
	 *	reads garbage from un allocated regions if OOB
	 *	tests (i.e. caller ) expected to stay withing the capacity
	 */
	void DiskManagerMemory::ReadPage(
		page_id_t page_id, 
		char *page_data
	) {
		int64_t offset = static_cast<int64_t>(page_id) * BUSTUB_PAGE_SIZE;
		memcpy(page_data, memory_ + offset, BUSTUB_PAGE_SIZE);
	}
	
	/*
	 *	DiskManagerUnlimitedMemory ctor:
	 *		pre-populate data_ with enough empty pages to satisfy default page_capacity_
	 * 		reset latency sim ring buffer to sentinel value (i.e. -1 )
	*/
	DiskManagerUnlimitedMemory::DiskManagerUnlimitedMemory() {
		std::scoped_lock l(mutex_);
		while (data_.size() < page_capacity_ + 1) {
			data_.push_back(std::make_shared<ProtectedPage>());
		}
		std::fill(recent_access_.begin(), recent_access_.end(), -1);
	}
	
	/**
	 *	Write a page to the database file.
	 *	@param page_id id of the page
	 *	@param page_data raw page data
	 *
	 *	locking:
	 *		1. take a mutex to safely grow data_ if needed and to grab a snapshot shared_ptr to target page
	 *		2. acquire per page exclusive lock
	 *		3. *RELEASE* mutex_ (we no longer need it, we have shared_ptr)
	 *		4. memcpy() under per page lock
	 *
	 *		this pattern lets concurrent writes to *DIFFERENT* pages proceed in parallel even 
	 *		though they momentarily contend mutex_
	 */
	void DiskManagerUnlimitedMemory::WritePage(
		page_id_t page_id, 
		const char *page_data
	) {
		if (page_id < 0) {
			fmt::println(stderr, "read invalid page {}", page_id);
			std::terminate();
			return;
		}
	
		ProcessLatency(page_id);
	
		std::unique_lock<std::mutex> l(mutex_);
		if (!thread_id_.has_value()) {
			thread_id_ = std::this_thread::get_id();
		}
		if (page_id >= static_cast<int>(data_.size())) {
			data_.resize(page_id + 1);
		}
		if (data_[page_id] == nullptr) {
			data_[page_id] = std::make_shared<ProtectedPage>();
		}
		std::shared_ptr<ProtectedPage> ptr = data_[page_id];
		std::unique_lock<std::shared_mutex> l_page(ptr->second);
		l.unlock();
	
		memcpy(ptr->first.data(), page_data, BUSTUB_PAGE_SIZE);
		num_writes_ += 1;
	
		PostProcessLatency(page_id);
	}
	
	/**
	 *	Read a page from the database file.
	 *	@param page_id id of the page
	 *	@param[out] page_data output buffer
	 *
	 *	same locking as WritePage() but takes per page lock shared, where WritePage() takes exclusive
	 *	so that concurrent reads of the same page can run in parallel
	 */
	void DiskManagerUnlimitedMemory::ReadPage(
		page_id_t page_id, 
		char *page_data
	) {
		if (page_id < 0) {
			fmt::println(stderr, "read invalid page {}", page_id);
			std::terminate();
			return;
		}
	
		ProcessLatency(page_id);
	
		std::unique_lock<std::mutex> l(mutex_);
		if (!thread_id_.has_value()) {
			thread_id_ = std::this_thread::get_id();
		}
	
		if (page_id >= static_cast<int>(data_.size())) {
			data_.resize(page_id + 1);
		}
		if (data_[page_id] == nullptr) {
			data_[page_id] = std::make_shared<ProtectedPage>();
		}
	
		std::shared_ptr<ProtectedPage> ptr = data_[page_id];
		std::shared_lock<std::shared_mutex> l_page(ptr->second);
		l.unlock();
	
		memcpy(page_data, ptr->first.data(), BUSTUB_PAGE_SIZE);
	
		PostProcessLatency(page_id);
	}
	
	/**
	 *	Delete a page from the database file. Reclaim the disk space.
	 *	Since we are using memory, this is a no-op.
	 *	@param page_id id of the page
	 *
	 *	no counter bump either - DeletePage() on this memory backed backend is invisible to tombstoning
	 */
	void DiskManagerUnlimitedMemory::DeletePage(page_id_t page_id) {
		// no-op since we are using memory
	}
	
	/*
	 *	latency classifier for leaderboard benchmark
	 *
	 *	walks ring buffer of last 4 accessed page_id
	 *	if current access in same block (i.e. lower bits cleared comparison)
	 *	or within 4-page sequential run of recent access, treat as cheap (i.e. 100 µs)
	 *	else treat as random access (i.e. 1,000µs = 1ms)
	 *
	 *	sleep itslef is std::this_thread::sleep_for running *outside* latency_processor_mutex_
	 *	(i.e. multiple threads can sleep in parallel), important for benchmarks to actually measure concurrency benefits
	*/
	void DiskManagerUnlimitedMemory::ProcessLatency(
		page_id_t page_id
	) {
		uint64_t sleep_micro_sec = 1000;  // for random access, 1ms latency
		if (latency_simulator_enabled_) {
			std::unique_lock<std::mutex> lck(latency_processor_mutex_);
			for (auto &recent_page_id : recent_access_) {
				if ((recent_page_id & (~0x3)) == (page_id & (~0x3))) {
					sleep_micro_sec = 100;  // for access in the same "block", 0.1ms latency
					break;
				}
				if (page_id >= recent_page_id && page_id <= recent_page_id + 3) {
					sleep_micro_sec = 100;  // for sequential access, 0.1ms latency
					break;
				}
			}
			lck.unlock();
			std::this_thread::sleep_for(std::chrono::microseconds(sleep_micro_sec));
		}
	}
	
	/*
	 *	after I/O complete, remember this page_id + ring buffer
	 *	so next access can classify itself relative to recent traffic
	*/
	void DiskManagerUnlimitedMemory::PostProcessLatency(
		page_id_t page_id
	) {
		if (latency_simulator_enabled_) {
			std::scoped_lock<std::mutex> lck(latency_processor_mutex_);
			recent_access_[access_ptr_] = page_id;
			access_ptr_ = (access_ptr_ + 1) % recent_access_.size();
		}
	}
	
	/*
	 *	test hook
	 *
	 * 	was the last I/O performed by a specific thread?
	 * 	returns ID then clears so next check starts fresh
	*/
	auto DiskManagerUnlimitedMemory::GetLastReadThreadAndClear() -> std::optional<std::thread::id> {
		std::unique_lock<std::mutex> lck(mutex_);
		auto t = thread_id_;
		thread_id_ = std::nullopt;
		return t;
	}
	
}  // namespace bustub
