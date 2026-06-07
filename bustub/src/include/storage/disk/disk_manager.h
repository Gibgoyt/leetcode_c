//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// disk_manager.h
//
// Identification: src/include/storage/disk/disk_manager.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>  // NOLINT
#include <mutex>   // NOLINT
#include <string>
#include <unordered_map>
#include <vector>

#include "common/config.h"
#include "common/logger.h"

namespace bustub {
	/*
	 *	DiskManager
	 *	the bottom of the storage stack
	 *
	 *	this is the only class that actually issues read/write syscalls
	 *	the DiskScheduler talks to it through 3 virtual hooks below
	 *		WritePage
	 *		ReadPage
	 *		DeletePage
	 *	the BufferPoolManager never see this class directly (for project 1, tasks 2,3)
	 *
	 *	two "flavours" live in this codebase:
	 *		- DiskManager		- current class
	 *					  fstream-backed
	 *					  the production path
	 *		- DiskManagerMemory	- [Unlimited]
	 *					  memory backed mocks for tests & leaderboard
	 *					  @see disk_manager_memory.cpp
	 *		
	 *		both honour the same 3 virtual entry points
	 *		DiskManagerMemory derives from DiskManager and overrides those 3
	 *		that's why the pointer that DiskScheduler holds is the base type
	 *	
	 *	allocation model:
	 *		LAZY
	 *		- a page_id has no on disk presence until it is first written or first read
	 *		  the pages_ map tracks the offset of every page that has been touched
	 *		- deleted pages leave their on disk slot in `free_slots_` for re-use:
	 *			the file is never shrunk, only grown
	 *			only grown when no free slots are available	@See AllocatePage at disk_manager.cpp
	 *
	 *	concurrency:
	 *		- db_io_latch_		- serializes all access to db_io_, pages_map, and free_slots_ vector
	 *					  held for the entire duration of each public method that touches the DB file
	 *					  db_io_ is the fstream
	 *		- log_io_		- log methods are not covered by db_io_latch_
	 *					  written/read sequentially from a single thread
	 *		- counter members are not atomic
	 *		  only updated under the latch
	*/
	
	/*
	 *	DiskManager takes care of the allocation and deallocation of pages within a database. It performs the reading and
	 *	writing of pages to and from disk, providing a logical file layer within the context of a database management system.
	 *
	 *	DiskManager uses lazy allocation, meaning that it only allocates space on disk when it is first accessed. It
	 *	maintains a mapping of page ids to their corresponding offsets in the database file. When a page is deleted, it is
	 *	marked as free and can be reused by future allocations.
	*/
	class DiskManager {
		public:
			/*
			 *	@brief		- open/create the db file and sibling *.log files
			 *
			 *	side effects:
			 *		- creates/truncates the *.log file if it does not exist
			 *		- resized db file to (page_capacity_ + 1) *  BUSTUB_PAGE_SIZE
			 *		  so that there is room for initial page_capacity_ no. of pages
			 *		- throws bustub::Exception if either file can not be opened
			*/
			explicit DiskManager(
				const std::filesystem::path &db_file
			);
	
			/*
			 *	FOR TEST / LEADERBOARD ONLY, used by DiskManagerMemory 
			 * 	
			 * 	leaves all fstreams uninitialized
			 * 	derived class is responsible to *not* call base methods that touch them
			*/
			DiskManager() = default;
	
			/*
			 *	virtual so DiskManagerMemory destructor runs
			 *	base impl has no resources to release beyond fstream own destructors and what ShutDown() already closed
			*/
			virtual ~DiskManager() = default;
	
			/*
			 *	cleanly close db_io_ and log_io_
			 *	idempotent practice (fstream ignores double close)
			 *		but call once for clarity
			*/
			void ShutDown();
	
			/**
			 *	Write a page to the database file.
			 *	@param page_id id of the page
			 *	@param page_data raw page data
			 *
			 *	if the page already exists, override at its recorded offset
			 *	else allocate a new offset (preferring free_slots_ over file extensions)
			 *	always flush fsream() before returning
			 */
			virtual void WritePage(
				page_id_t page_id, 
				const char *page_data
			);
	
			/**
			 *	Read a page from the database file.
			 *	@param page_id id of the page
			 *	@param[out] page_data output buffer
			 *
			 *	if a page has no recorded offset AllocatePage() runs
			 *	fresh read after no write returns zeroes from resized file
			 *	short reads at EOF are zero-padded so caller always sees full buffer BUSTUB_PAGE_SIZE bytes long
			 */
			virtual void ReadPage(
				page_id_t page_id, 
				char *page_data
			);
	
			/**
			 *	Delete a page from the database file. Reclaim the disk space.
			 *	@param page_id id of the page
			 *
			 *	"reclaim" here meaning:
			 *		mark file offset as free for re-use
			 *		do not truncate file
			 *		bytes on disk not zeroed
			 */
			virtual void DeletePage(
				page_id_t page_id
			);
	
			/*
			 *	append `size` bytes to the *.log file synchronously
			*/
			void WriteLog(
				char *log_data, 
				int size
			);
	
			/*
			 *	read `size` bytes at byte `offset` from the *.log file
			 *	returns false if `offset` >= EOF
			 *	short reads are zero-padded
			*/
			auto ReadLog(
				char *log_data, 
				int size, 
				int offset
			) -> bool;
	
			auto GetNumFlushes() const -> int;
	
			auto GetFlushState() const -> bool;
	
			auto GetNumWrites() const -> int;
	
			auto GetNumDeletes() const -> int;
	
			/**
			 *	Sets the future which is used to check for non-blocking flushes.
			 *	@param f the non-blocking flush check
			 */
			inline void SetFlushLogFuture(std::future<void> *f) { flush_log_f_ = f; }
	
			/** Checks if the non-blocking flush future was set. */
			inline auto HasFlushLogFuture() -> bool { return flush_log_f_ != nullptr; }
	
			/** @brief returns the log file name */
			inline auto GetLogFileName() const -> std::filesystem::path { return log_file_name_; }
	
			/** @brief returns the size of disk space in use */
			auto GetDbFileSize() -> size_t {
			  auto file_size = GetFileSize(db_file_name_);
			  if (file_size < 0) {
			    LOG_DEBUG("I/O error: Fail to get db file size");
			    return -1;
			  }
			  return static_cast<size_t>(file_size);
			}
	
		protected:
			/*
			 *	counters are protected not private so DiskManagerMemory can increment them too
			 *	keeping test expectations consistent across 2 backend
			*/

			int num_flushes_{0};
			int num_writes_{0};
			int num_deletes_{0};
	
			/*
			 *	@brief The capacity of the file used for storage on disk. 
			 * 	no. of page slots, not bytes
			 * 	doubles when AllocatePage runs out of room
			*/
			size_t page_capacity_{DEFAULT_DB_IO_SIZE};
	
		private:
			// stat2 based size lookup; returning -1 on stat failure
			auto GetFileSize(const std::string &file_name) -> int;
	
			/*
			 *	returning byte offset for new page slot
			 *	pops from free_slots_ if available
			 *	else grows page_capacity_ and the file as needed
			*/
			auto AllocatePage() -> size_t;
	
			// stream to write log file
			std::fstream log_io_;
			std::filesystem::path log_file_name_;

			// stream to write db file
			std::fstream db_io_;
			std::filesystem::path db_file_name_;
	
			// Records the offset of each page in the db file.
			std::unordered_map<page_id_t, size_t> pages_;
			// Records the free slots in the db file if pages are deleted, indicated by offset.
			std::vector<size_t> free_slots_;
	
			bool flush_log_{false};
			std::future<void> *flush_log_f_{nullptr};
			// With multiple buffer pool instances, need to protect file access
			std::mutex db_io_latch_;
	};
	
}  // namespace bustub
