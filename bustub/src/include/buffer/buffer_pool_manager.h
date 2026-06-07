//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.h
//
// Identification: src/include/buffer/buffer_pool_manager.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <list>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "buffer/arc_replacer.h"
#include "common/config.h"
#include "recovery/log_manager.h"
#include "storage/disk/disk_scheduler.h"
#include "storage/page/page.h"
#include "storage/page/page_guard.h"

namespace bustub {
	/*
	 *	BufferPoolManager
	 *	Project 1, Task 3
	 *
	 *		buffer pool is cache + paging layer that sits between the rest of the DBMS + disk resident data files
	 *		higher layers (e.g. executors, indexes, recoveries) only ever ask for a page by page_id_t
	 *		buffer pool decides whether:
	 *			- page is already resident
	 *			- page must be faulted from disk
	 *			- when is has to evict pages
	 *		
	 *		vocab:
	 *			- page		- BUSTUB_PAGE_SIZE (i.e. 8 KiB) bytes of *logical* data identified by page_id_t
	 *			- frame		- fixed BUSTUB_PAGE_SIZE buffer in memory
	 *					  identified by frame_id_t
	 *					  always stays in memory
	 *					  re-used across many pages over its lifetime
	 *			- FrameHeader	- metadata wrapper around a frame
	 *					  pin count, dirty flag (i.e. tombstoned from BPM), wrlatch, data ptr
	 *					  @see @ref FrameHeader
	 *			- page table	- page_id_t -> frame_id_t mapping for pages currently resident
	 *			- free list	- frames that hold no page
	 *					  initial state for all frames
	 *			- replacer	- ARC replacement policy that decides, which resident, but unpinned to evict
	 *					  (i.e. which ghost resident to evict)
	 *
	 *		architecture collaborators:
	 *			- ArcReplacer	- Project 1, Task 1
	 *					  tracks usage and supplies eviction victims
	 *			- DiskScheduler	- project 1, task 2
	 *					  queues async read/writes to DiskManager
	 *			- FrameHeader	- in-file helper class below
	 *			- ReadPageGuard
	 *			- WritePageGuard	- RAII handles returned to callers
	 *						  only safe way to touch page bytes outside BPM itself
	 *
	 *		concurrency model:
	 *			- bpm_latch_ protects the BPM's own bookkeeping
	 *			  page_table_, free_frames_, the act of consulting/updating the replacer + fault-in/evict critical section
	 *			- each FrameHeader has its own rwlatch_ protecting page bytes inside the frame
	 *			  held shared ReadPageGuard
	 *			  held exclusive by WritePageGuard
	 *			  for the guard's *ENTIRE* lifetime
	 *		- pin_count_ is std::atomic so it can be inspected without holding bpm_latch_
	 *		  *BUT* every transtion from x=0, to x>0, must be coordinated with replacer_->setEvictable() while holding bpm_latch_
	 *		  otherwise the replacer's notion of "evictable" diverges from reality
	 *		- I/O via disk scheduler is async:
	 *		  bpm_latch_ must be dropped before blocking on a future
	 *		  otherwise other threads can not enter the pool + we serialize on the slowest disk req
	 *	
	 *	read per method docs	@see buffer_pool_manager.cpp
	*/
	
	class BufferPoolManager;
	class ReadPageGuard;
	class WritePageGuard;
	
	/**
	 *	@brief A helper class for `BufferPoolManager` that manages a frame of memory and related metadata.
	 *
	 *	This class represents headers for frames of memory that the `BufferPoolManager` stores pages of data into. Note that
	 *	the actual frames of memory are not stored directly inside a `FrameHeader`, rather the `FrameHeader`s store pointer
	 *	to the frames and are stored separately them.
	 *
	 *	---
	 *
	 *	Something that may (or may not) be of interest to you is why the field `data_` is stored as a vector that is
	 *	allocated on the fly instead of as a direct pointer to some pre-allocated chunk of memory.
	 *
	 *	In a traditional production buffer pool manager, all memory that the buffer pool is intended to manage is allocated
	 *	in one large contiguous array (think of a very large `malloc` call that allocates several gigabytes of memory up
	 *	front). This large contiguous block of memory is then divided into contiguous frames. In other words, frames are
	 *	defined by an offset from the base of the array in page-sized (4 KB) intervals.
	 *
	 *	In BusTub, we instead allocate each frame on its own (via a `std::vector<char>`) in order to easily detect buffer
	 *	overflow with address sanitizer. Since C++ has no notion of memory safety, it would be very easy to cast a page's
	 *	data pointer into some large data type and start overwriting other pages of data if they were all contiguous.
	 *
	 *	If you would like to attempt to use more efficient data structures for your buffer pool manager, you are free to do
	 *	so. However, you will likely benefit significantly from detecting buffer overflow in future projects (especially
	 *	project 2).
	 *
	 *	lifecycle + invariants:
	 *		- exactly one FrameHeader per slot
	 *		  BPM owns shared_ptr<FrameHeader> in its frames_ vector + hands the same ptr out to any guard returned to callers
	 *		  guards can outlive a sequence of BPM calls without dangling
	 *		- FrameHeader holds bytes for *at most* one page at a time
	 *		  re-uses the same data_ buffer across many different pages over its lifetime
	 *		- "empty" FrameHeader (i.e. no page resident, all ghost, or none {I THINK})
	 *		  represented by data_ being all zero and frame_id being BufferPoolManager::free_frames_
	 *		  no enum tag
	 *		  state inferred by external bookkeeping
	 *
	 *	BufferPoolManager, ReadPageGuard, WritePageGuard all friends (idk if 'friends' is c++ std)
	 *		FrameHeader exposes its real API (i.e. GetData(), GetDataMut(), Reset(), pin_count_, is_dirty_, rw_latch_) as private
	 *		only these 3 classes needs to read/write to these fields/metadata-fields
	 *		marking them friend keeps everything else from accidentally reaching into a frame
	 * 	
	 * 	concurrency:
	 * 		- rwlatch_ is the *page* latch
	 * 		  shared for readers, exclusive for writers
	 * 		  held for entire lifetime of corresponding guard
	 * 		- pin_count_ is std::atomic
	 * 		  it can be read without rwlatch_
	 * 		  0 to >0 transition *MUST* be done with bpm_latch_ so that replacer's SetEvictable() stays sync
	 * 		- is_dirty_ is plain bool, therefore safe to mutate while frame's rwlatch_ is held in exclusive mode
	 * 		  (i.e. inside WriteGuard(), or with bpm_latch_ && exclusive rwlatch_ during eviction)
	 */
	class FrameHeader {
		friend class BufferPoolManager;
		friend class ReadPageGuard;
		friend class WritePageGuard;
	
	 public:
		/*
		 *	@brief		- construct empty FrameHeader to a specific frame slot
		 * 	
		 * 	frame_id set once and never changed (i.e. const)
		 * 	data_ buffer size to BUSTUB_PAGE_SIZE + zero-filled by Reset() in the body	@see buffer_pool_manager.cpp
		 * 	pin_count_ starts at 0
		 * 	is_dirty_ starts at false
		*/
		explicit FrameHeader(
			frame_id_t frame_id
		);
	
	 private:
		/*
		 *	@brief		- read-only view of page bytes in this frame
		 * 	@return		- const char * into data_ (i.e. length BUSTUB_PAGE_SIZE)
		 *
		 * 	used by:
		 * 		- ReadPageGuard::GetData() to expose bytes to readers
		 * 		- BufferPoolManager flush path (e.g. FlushPage, etc...) when handing buffer to DiskScheduler for write-back
		 *
		 * 	caller contract:
		 * 		- must hold rwlatch_ in at least shared mode for the duration of the read
		 * 		- pointer invalidated by Reset() (i.e. eviction from buffer pool)
		*/
		auto GetData() const -> const char *;

		/*
		 *	@brief		- mutable view of page bytes in this frame
		 *	@return		- char * into data_ (i.e. length  BUSTUB_PAGE_SIZE)
		*/
		auto GetDataMut() -> char *;

		/*
		 *	@brief		- erase per-page state so this frame can be re-used for a different page
		 * 	
		 * 	resets:
		 * 		data_ to all zero bytes
		 * 		is_dirty_ to false
		 * 		pin_count_ to 0
		 * 		does *NOT* touch rwlatch_ or frame_id_ (i.e. that is frame ID not page ID)
		 *
		 * 	caller contract:
		 * 		- pin count must already be 0 (i.e. replacer would NOT have given us this frame otherwise)
		 * 		- if is_dirty_ true, BPM must have already scheduled write-back *BEFORE* calling Reset() (i.e. otherwsie data is lost)
		 * 		- hold bpm_latch *AND* rwlatch_ exclusive while calling
		 * 		  makes "old page goes away, new page arrives" atomic from observer's perspective
		*/
		void Reset();
	
		/** @brief The frame ID / index of the frame this header represents. */
		const frame_id_t frame_id_;
	
		/*
		 *	@brief The readers / writer latch for this frame. 
		 *
		 *	acquired shared by ReadPageGuard(),
		 *	exclusive by WritePageGuard()
		 *	for the *ENTIRE GUARD LIFETIME*
		 *	this is the latch that gives  those guards its thread safety gaurantee
		*/
		std::shared_mutex rwlatch_;
	
		/*
		 *	@brief The number of pins on this frame keeping the page in memory
		 *
		 * 	each outstanding guard adds 1
		 * 	each gaurd's drop/dtro -= 1
		 * 	pin_count_ > 0 means pin is *NOT* evictable, regardless of what the replacer thinks
		 *	when pin_count_ transitions to 0, BPM must call replacer_->SetEvictable(frame_id_, true)
		 *	when pin_count_ transtions away from 0 call SetEvictable(..., false)
		 *
		 *	std::atomic so GetPinCount() can read without holding bpm_latch_, but mutating transitions across 0 still requires bpm_latch_
			 *	for SetEvictable() sync described above (i.e. otherwise an eviction can race a fresh pin)
		*/
		std::atomic<size_t> pin_count_;
	
		/*
		 *	@brief The dirty flag.
		 *
		 *	true if in memory copy has been modified since last read from or written to disk
		 *	set by WritePageGuard() on any mutation (or on Flush() depending on implementation)
		 *	read by BPM at eviction time
		 *	if dirt then schedule a write through disk_scheduler_ before calling Reset() on the frame 
		*/
		bool is_dirty_;
	
		/**
		 *	@brief A pointer to the data of the page that this frame holds.
		 *
		 *	If the frame does not hold any page data, the frame contains all null bytes.
		 */
		std::vector<char> data_;
	
		/**
		 *	TODO(P1): You may add any fields or helper functions under here that you think are necessary.
		 *
		 *	One potential optimization you could make is storing an optional page ID of the page that the `FrameHeader` is
		 *	currently storing. This might allow you to skip searching for the corresponding (page ID, frame ID) pair somewhere
		 *	else in the buffer pool manager...
		 *
		 * 	Implementation hint - reverse-mapping trade-off:
		 * 		page_table_ already lets you go page_id -> frame_id in O(1)
		 * 		but durin eviction the replacer hands you a frame_id and you need the page_id to:
		 * 			- remove the right page_table_ entry
		 * 			- schedule the right write-back if dirty
		 * 			- tell the new page's lookup that its frame is now bound to a differnt page_id
		 * 		storing page_id_t (or std::optional<page_id_t>) here avoids O(n) scans of page_table_ during eviction
		 */
	};
	
	/**
	 *	@brief The declaration of the `BufferPoolManager` class.
	 *
	 *	As stated in the writeup, the buffer pool is responsible for moving physical pages of data back and forth from
	 *	buffers in main memory to persistent storage. It also behaves as a cache, keeping frequently used pages in memory for
	 *	faster access, and evicting unused or cold pages back out to storage.
	 *
	 *	Make sure you read the writeup in its entirety before attempting to implement the buffer pool manager. You also need
	 *	to have completed the implementation of both the `ArcReplacer` and `DiskManager` classes.
	 *
	 *	method taxonomy:
	 *		allocation/lifecycle:
	 *			- NewPage	- allocate fresh page_id_t and reserve a frame for it
	 *			- DeletePage	- evict-if-resident
	 *					  DeallocatePage() on disk scheduler
	 *		access:
	 *			- CheckedReadPage/CheckedWritePage:
	 *				the *only* correct entry points
	 *				returns std::optional<...PageGuard>, std::nullopt iff fault-in frame because every frame is pinned
	 *			- ReadPage/WritePage
	 *				thin wrappers that unwrap optional and BUSTUB_ENSURE on failure
	 *				use only when caller has gauranteed the pool can absorb the req (tests, single-threaded ctx)
	 *		persistenc:
	 *			- FlushPage/FlushAllPages
	 *				thread safe
	 *				acquire bpm_latch_
	 *			- FlushPageUnsafe/FlushAllPagesUnsafe
	 *				assume caller already holds something that excludes concurrent access
	 *				(e.g. tests during teardown, or internal callers that already hold bpm_latch_)
	 *
	 *		diagnostics:
	 *			Size, GetPinCount
	 *
	 *	@see buffer_pool_manager.cpp
	 *	  for per-method expected implementation docs/comments
	 *	  most of the design lives there
	 *
	 *	performance note:
	 *		- holding buffer pool latch from beginning to end should be enough *except* for when it needs to be released early to prevent deadlocks
	 *		  specifically, drop bpm_latch_ before waiting on DiskScheduler future, else no other thread can enter pool from blocking I/O
	 *		- do not over engineer
	 *		  leaderboard reward for finer grained latching is small compared to the bug risk it introduces
	 */
	class BufferPoolManager {
		public:
			/*
			 *	@brief		- construct buffer pool with `num_frames` slots
			 *			  backed by `disk_manager`
			 *
			 * 	allocates num_frames FrameHeaders
			 * 	pushes all its frame_ids onto free_frames_
			 * 	builds ArcReplacer with capacity = num_frames
			 * 	constructs DiskScheduler arround supplied DiskManager
			 *
			 * 	ownership:
			 * 		- disk_manager	- non owning
			 * 				  caller keeps lifetime (i.e. responsible for alloc and de-alloc)
			 * 		- log_manager 	- non owning
			 * 				  optional
			 * 				  not for project 1
			 * 		- replacer_	- owned by this BPM
			 * 		- disk_scheduler	- owned by this BPM
			 * 		- frames_	- owned by this BPM
			*/
			BufferPoolManager(
				size_t num_frames, 
				DiskManager *disk_manager, 
				LogManager *log_manager = nullptr
			);
			
			/*
			 *	@brief		- destroy the buffer pool
			 * 	
			 * 	important:
			 * 		- README for project 1, task 3 does not require to flush dirty pages
			 * 		  tests specifically call FlushAllPages*() before destruction when needing persistence
			 * 		- door MUST however allow DiskScheduler to drain in-flight I/O cleanly
			 * 		  disk_scheduler_ is a shared_ptr, whoever drops last ref will  trigger ~DiskScheduler
			 * 		- DO NOT destroy while any ReadPageGuard/WritePageGuard outlives BPM
			 * 		  guards hold shared ptrs to BPM internals
			 * 		  if BPM goes out first the guards either UB on Drop()/leak
			*/
			~BufferPoolManager();
		
			/*
			 *	@brief		- no. of frames managed (i.e. constructor num_frames)
			*/
			auto Size() const -> size_t;

			/*
			 *	@brief		- allocate fresh page_id_t and reserver frame for it in the pool\
			 * 	
			 * 	returns new page_id (monotonically increasing via next_page_id_
			 * 	the page is considered "pinned-zero/resident/zero-initialized" once this returns
			 * 	a subsquent CheckedReadPage/CheckedWritePage on it must succeed without any disk read
			 *
			 * 	@see buffer_pool_manager.cpp
			 * 	  for exact semantics around what to do when a frame is free (i.e. evict-or-fail)
			 * 	  and whether the new page is dirty
			*/
			auto NewPage() -> page_id_t;

			/*
			 *	@brief		- delete a page from *BOTH* buffer pool and disk
			 *	@return		- true if the page was deleted
			 *			  false if it could not be evicted (i.e. still pinned)
			 *
			 * 	if page is resident and unpinned, then evict it
			 * 	flushing if dirty is optional per semantics as we are about to delete on-disk copy anways
			 * 	call disk_scheduler_->DeallocatePage(page_id) so that the slot becomes re-usable
			*/
			auto DeletePage(
				page_id_t page_id
			) -> bool;

			/*
			 *	@brief		- acquire exclusive writer guard for page_id
			 *	@return		- WritePageGuard on success
			 *			  std::nullopt if every frame in the pool is pinned (i.e. we can not fault page_id into)
			 *
			 *	std::optional return is what makes this "Checked"
			 *		callers handle "pool exhausted" case
			 *		tests verify that this matters
			 *
			 *
			 *	@param access_type	- hint to replacer for leaderboard pre-fetching policies
			*/
			auto CheckedWritePage(
				page_id_t page_id, 
				AccessType access_type = AccessType::Unknown
			) -> std::optional<WritePageGuard>;

			/*
			 *	@brief		- acquire a shared reader gaurd for page_id
			 *	@return		- ReadPageGuard on success
			 *			  std::nullopt if every frame is pinned
			 *
			 *	@see CheckedWritePage
			 *	  identical lifecycle latching
			*/
			auto CheckedReadPage(
				page_id_t page_id, 
				AccessType access_type = AccessType::Unknown
			) -> std::optional<ReadPageGuard>;

			/*
			 *	@brief		- unchecked WritePage
			 *			  BusTub ensures checked path succeeded
			 *
			 *	Sugar for callers assuming this cannot fail
			 *	if Checked variant returns std::nullopt (i.e. CheckedWritePage()), then this will abort (i.e. BUSTUB_ENSURE)
			 *	rather than returning a default constructed guard (because a default WritePageGuard looks eerily valid)
			*/
			auto WritePage(
				page_id_t page_id, 
				AccessType access_type = AccessType::Unknown
			) -> WritePageGuard;

			/*
			 *	@copydoc WritePage
			*/
			auto ReadPage(page_id_t page_id, AccessType access_type = AccessType::Unknown) -> ReadPageGuard;

			/*
			 *	@brief		- write page_id frame to disk *WITHOUT* taking bpm_latch_
			 *	@return		- true if flush was scheduled
			 *			  false if page not resident
			 *
			 *	assumes the caller has external synchronization
			 *	used by FlushAllPagesUnsafe(), test teardown, and WritePageGuard::Flush() path 
			 *		which holds frame's exclusive rwlatch_,
			 *		enough to serialize against other writers of *THAT* page,
			 *		but not against other BPM operations (e.g. eviction) so use with care please
			 *
			 *	clears dirty page iff I/O happened
			*/
			auto FlushPageUnsafe(
				page_id_t page_id
			) -> bool;

			/*
			 *	@brief		- thread safe flush of a single page
			 *	
			 *	acquires bpm_latch_
			 *	dispatches to an unsafe style body, releasees
			 *	unsafe variant exists so that internal code that already holds the latch does not deadlock on self-recursion
			*/
			auto FlushPage(
				page_id_t page_id
			) -> bool;

			/*
			 *	@brief		- FlushPageUnsafe() for every resident page
			*/
			void FlushAllPagesUnsafe();
			
			/*
			 *	@brief		- thread safe variant of FlushAllPagesUnsafe()
			*/
			void FlushAllPages();

			/*
			 *	@brief		- diagnostic accessor
			 *	@return		- pin count if page resident
			 *			  std::nullopt otherwise
			 *
			 *	primarily for tests
			 *	safe to call concurrently with other BPM ops because pin_count_ is std::atomic
			 *	however, "resident or not" check still requires bpm_latch_, so this function takes it
			*/
			auto GetPinCount(
				page_id_t page_id
			) -> std::optional<size_t>;
		
		private:
			/*
			 *	@brief The number of frames in the buffer pool. 
			*/
			const size_t num_frames_;
		
			/*
			 *	@brief The next page ID to be allocated.
			 * 	
			 * 	atomic so that NewPage() can fetch without bpm_latch_
			 * 	starts at 0
			 * 	INVALID_PAGE_ID(-1) is reserved as a sentinel and will never be returned`
			*/
			std::atomic<page_id_t> next_page_id_;
		
			/**
			 *	@brief The latch protecting the buffer pool's inner data structures.
			 *
			 *	TODO(P1) We recommend replacing this comment with details about what this latch actually protects.
			 *
			 *	specifically gaurds:
			 *		- page_table_		- insertions, removals, lookups during fault-in/evict
			 *		- free_frames_		- pop on fault-in
			 *					  push on DeletePage() evict-and-free
			 *		- frames_		- only shared_ptr slot
			 *					  not FrameHeader contents
			 *		- replacer_		- every RecordAccess/SetEvictable/Evict/Remove must hold this
			 *					  otherwise frame metadata can desync from BPM's perspective
			 *
			 *	does *NOT* gaurd:
			 *		- FrameHeader::rwlatch_	- this is the page latch
			 *					  acquired on top of bpm_latch_ by callers of CheckedRead()/CheckedWrite()
			 *		- next_page_id_		- it is atomic
			 *		- pin_count_		- atomic
			 *					  x=0, to x>0 transition needs bpm_latch_ for the ArcReplacer sync
			 *
			 *	std::shared_ptr<std::mutex> (i.e. not a flat mutex) so that it can be shared with page guards
			 *	a guard's Drop() may need to re-acquire it after BPM call that created it returns
			 *	shared_ptr also lets a lifetime extend safely
			 *
			 *	IMPORTANT
			 *		lock ordering
			 *			drop bpm_latch_ *BEFORE* blocking on DiskScheduler future, 
			 *			otherwise worker thread can not make progress
			 *			(i.e. it does not take bpm_latch_ itself, but every other thread that wants to enter BPM does)
			*/
			std::shared_ptr<std::mutex> bpm_latch_;
		
			/*
			 *	@brief The frame headers of the frames that this buffer pool manages.
			*/
			std::vector<std::shared_ptr<FrameHeader>> frames_;
		
			/*
			 *	@brief The page table that keeps track of the mapping between pages and buffer pool frames.
			*/
			std::unordered_map<page_id_t, frame_id_t> page_table_;
		
			/*
			 *	@brief A list of free frames that do not hold any page's data.
			*/
			std::list<frame_id_t> free_frames_;
		
			/*
			 *	@brief The replacer to find unpinned / candidate pages for eviction.
			*/
			std::shared_ptr<ArcReplacer> replacer_;
		
			/*
			 *	@brief A pointer to the disk scheduler. Shared with the page guards for flushing.
			*/
			std::shared_ptr<DiskScheduler> disk_scheduler_;
		
			/*
			 *	@brief A pointer to the log manager.
			 *
			 *	Note: Please ignore this for P1.
			*/
			LogManager *log_manager_ __attribute__((__unused__));
		
			/**
			 *	TODO(P1): You may add additional private members and helper functions if you find them necessary.
			 *
			 *	There will likely be a lot of code duplication between the different modes of accessing a page.
			 *
			 *	We would recommend implementing a helper function that returns the ID of a frame that is free and has nothing
			 *	stored inside of it. Additionally, you may also want to implement a helper function that returns either a shared
			 *	pointer to a `FrameHeader` that already has a page's data stored inside of it, or an index to said `FrameHeader`.
			 *
			 *	suggested private helpers:
			 *		- auto AllocateFrame() -> std::optional<frame_id_t>;
			 *		  returns frame_id that is currently *NOT* holding any page
			 *			1. if free_frames_ not empty, pop one, return it
			 *			2. else replacer_->Evict() returns a victim:
			 *				- look up its current page via reverse mapping
			 *				- if dirty, then schedule flush (i.e. drop bpm_latch_ around future.get()
			 *				- Reset() the FrameHeader()
			 *				  remove page_table_
			 *				  return
			 * 			3. else (i.e. no free, nothing evictable)
			 * 			 	- remove from page_table_
			 * 			 	  return
			 * 		  caller must hold bpm_latch_ on entry
			 * 		  note:
			 * 		  	step 2 may temporarily drop latch for I/O
			 * 		  	please remember to document this in the helper
			 * 		- auto LookupOrFault(page_id_t, AccessType) -> std::optional<frame_id_t>;
			 * 		  returns frame_id holding page_id
			 * 		  faulting it if necessary
			 * 		  common path shared by CheckedReadPage()/CheckedWritePage()
			 * 		  returning std::nullopt if AllocateFrame() did
			 * 		- void BumpPin(frame_id_t);
			 * 		- void DropPin(frame_id_t);
			 * 		  encapsulate the "pin_count_ fetch_add/sub + SetEvictable() sync"
			 * 		  dance so it is written exactly once
			*/
	};
}  // namespace bustub
