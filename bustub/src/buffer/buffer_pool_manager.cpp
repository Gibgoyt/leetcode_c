//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager.h"
#include <bits/chrono.h>
#include "buffer/arc_replacer.h"
#include "common/config.h"
#include "common/macros.h"

namespace bustub {
	/*
	 *	this is the bulk of project 1, task 3
	 *	this entire file is almost entirely UNIMPLEMENTED() stubs
	 *
	 *	lock ordering:
	 *		acquire order
	 *		1. bpm_latch_
	 *		2. FrameHeader::rwlatch_
	 *		3. release bpm_latch_
	 *		4. *future.get()
	 *
	 *		why are we dropping bpm_latch_ before future.get()
	 *			disk_scheduler_ worker thread does *NOT* take bpm_latch_ directly
	 *			every other caller of the BPM takes bpm_latc_ (e.g. other threads, CheckedReadPage(), CheckedWritePage(), etc...)
	 *			holding bpm_latch_ during a multi-ms I/O turns the buffer pool into a single threaded queue, tanking throughput
	 *			.../project_1/README.md "§Implementation" warns explicitly about this
	 *
	 *	pin count handshake
	 *		.../project_1/README.md "Concurrency" section is built around this
	 *
	 *		BPM side (i.e. under bpm_latch_)
	 *			1. find/fault-in the frame
	 *			2. frame_->pin_count_.fetch_add(1)
	 *			   invariant 2 @see page_guard.h
	 *			   "pin_count_ > 0, means page is not evictable"
	 *			3. replacer_->SetEvictable(fid, false)
	 *			4. replacer_->RecordAccess(fid, pid)
	 *			5. construct a ReadPageGuard{}/WritePageGuard{} with the frame's shared_ptr
	 *			   i.e. the ...PageGuard{} ctor acquires rwlatch_
	 *			6. release bpm_latch_
	 *			   return the guard
	 *
	 *		Guard side (i.e. on Drop()/dtor)
	 *			1. release rwlatch_
	 *			2. frame_->pin_count.fetch_add(1)
	 *			3. if new_pin == 0
	 *			   lock bpm_latch_ briefly
	 *			   replacer_->SetEvictable(fid, true)
	 *			   unlock the bpm_latch_
	 *
	 *	"One copy of a page in memory"	@see .../project_1/README.md "§Concurrency" for scenarios T1/T2/X1/X2/X3
	 *		enforced by the fact the page_table_ lookup + frame allocation happens atomically under bpm_latch_
	 *		two concurrent CheckedReadPage(X) calls serialize on bpm_latch_
	 *			first wins fault-in race
	 *			second sees page already in page_table_, and just pins the existing frame
	 *
	 *	suggested helpers:
	 *		1. auto AllocateFrame() -> std::optional<frame_id_t>;
	 *			caller holds bpm_latch_
	 *			returns a frame_id that does not hold a page
	 *			1. if non-empty
	 *			   ```code snuppet
	 *			   free_frames_.pop_front()
	 *			   ```
	 *			2. if empty
	 *			   ```code snippet
	 *			   replacer_->Evict() -> frame_id_t
	 *			   ```
	 *			   find the page currently in the frame (use a reverse map)
	 *			   - if dirty
	 *			     schedule Flush()
	 *			     unlock bpm_latch_
	 *			     future.get()
	 *			     re-lock bpm_latch_
	 *			     re check invariants (i.e. someone else may have pinned it while we were gone)
	 *			  - frame_->Reset()
	 *			  - page_table_->Erase(old_page_id)
	 *			  - else nullopt (everything pinned)
	 *		2. auto LookupOrFault(page_id_t pid) -> std::optional<frame_id_t>;
	 *		   caller holds bpm_latch_
	 *		   returns frame_id for pid, faulting in if necessary
	 *		   shared backbone (i.e. CheckedReadPage()/CheckedWritePage())
	*/
	
	/**
	 * @brief The constructor for a `FrameHeader` that initializes all fields to default values.
	 *
	 * See the documentation for `FrameHeader` in "buffer/buffer_pool_manager.h" for more information.
	 *
	 * @param frame_id The frame ID / index of the frame we are creating a header for.
	 *
	 * Reset() is what actually zeroes data_ + clears is_dirty_, sets pin_count_ to 0
	 * data_ allocated to BUSTUB_PAGE_SIZE bytes long in the init list, *before* Reset() runs, so Reset() can fill() into already allocated memory
	*/
	FrameHeader::FrameHeader(
		frame_id_t frame_id
	) : frame_id_(frame_id), data_(BUSTUB_PAGE_SIZE, 0) { 
		Reset(); 
	}
	
	/**
	 * @brief Get a raw const pointer to the frame's data.
	 *
	 * @return const char* A pointer to immutable data that the frame stores.
	 *
	 * caller must hold rwlatch_ shared/exclusive
	 * pointer valid until Reset() (i.e. until eviction)
	 * length of BUSTUB_PAGE_SIZE
	 */
	auto FrameHeader::GetData() const -> const char * { 
		return data_.data(); 
	}
	
	/**
	 * @brief Get a raw mutable pointer to the frame's data.
	 *
	 * @return char* A pointer to mutable data that the frame stores.
	 *
	 * caller *MUST* hold rwlatch_ exclusive
	 * or bpm_latch + exclusive ownership during fault-in/evict
	 * caller is also responsible for setting is_dirty_ = true if they actually mutate (WritePageGuard::GetDataMut() does this for them)
	*/
	auto FrameHeader::GetDataMut() -> char * { 
		return data_.data();	
	}
	
	/**
	 * @brief Resets a `FrameHeader`'s member fields.
	 *
	 * caller contract:
	 * 	- pin_count_ must already be 0
	 * 	  or this is the constructor, where 0 is trivially true
	 *	- if is_dirty_ == true, caller *MUST* have already scheduled a flush
	 *	  Reset() throws away bytes
	 *	  if the bytes were not flushed, they are lost forever
	 *	- caller holds bpm_latch + rwlatch_ exclusive
	 *
	 * does *NOT* touch frame_id or rwlatch_ itself
	 * that is the frame's id-level latch
	 * we are swapping page_id, not frame_id
	 */
	void FrameHeader::Reset() {
		std::fill(data_.begin(), data_.end(), 0);
		pin_count_.store(0);
		is_dirty_ = false;
	}
	
	/**
	 * @brief Creates a new `BufferPoolManager` instance and initializes all fields.
	 *
	 * See the documentation for `BufferPoolManager` in "buffer/buffer_pool_manager.h" for more information.
	 *
	 * ### Implementation
	 *
	 * We have implemented the constructor for you in a way that makes sense with our reference solution. You are free to
	 * change anything you would like here if it doesn't fit with you implementation.
	 *
	 * Be warned, though! If you stray too far away from our guidance, it will be much harder for us to help you. Our
	 * recommendation would be to first implement the buffer pool manager using the stepping stones we have provided.
	 *
	 * Once you have a fully working solution (all Gradescope test cases pass), then you can try more interesting things!
	 *
	 * @param num_frames The size of the buffer pool.
	 * @param disk_manager The disk manager.
	 * @param log_manager The log manager. Please ignore this for P1.
	 *
	 * notes:
	 * 	- bpm_latch_ is created as a shared_ptr<mutex> so that we can hand the same shared_ptr to every PageGuard{} that we mint
	 * 	  @see buffer_pool_manager.h
	 * 	- ArcReplacer capacity = num_frames
	 * 	  replacer does *NOT* track ghost list overflow as additional capacity, that is internal to ARC
	 * 	- DiskScheduler{} constructed with supplied DiskManager{} raw ptr
	 * 	  ~DiskScheduler() runs when shared_ptrs refcount hits 0
	 * 	  this would be at ~BufferPoolManager since that holds the only ref, ...PageGuard{} only borrow the ref
	 * 	- "not strictly necessary" scoped_lock at the top:
	 * 	  BPM is not reachable from other threads yet (i.e. we are still in the ctor)
	 * 	  lock makes TSAN/helgrind happy, by establishing happens-before for any subsequent reads of the fields inited below
	*/
	BufferPoolManager::BufferPoolManager(
		size_t num_frames, 
		DiskManager *disk_manager, 
		LogManager *log_manager
	) : num_frames_(num_frames), next_page_id_(0), bpm_latch_(std::make_shared<std::mutex>()), replacer_(std::make_shared<ArcReplacer>(num_frames)),
		disk_scheduler_(std::make_shared<DiskScheduler>(disk_manager)), log_manager_(log_manager) {
		// Not strictly necessary...
		std::scoped_lock latch(*bpm_latch_);
	
		// Initialize the monotonically increasing counter at 0.
		next_page_id_.store(0);
	
		// Allocate all of the in-memory frames up front.
		frames_.reserve(num_frames_);
	
		// The page table should have exactly `num_frames_` slots, corresponding to exactly `num_frames_` frames.
		page_table_.reserve(num_frames_);
	
		// Initialize all of the frame headers, and fill the free frame list with all possible frame IDs (since all frames are
		// initially free).
		for (size_t i = 0; i < num_frames_; i++) {
			frames_.push_back(std::make_shared<FrameHeader>(i));
			free_frames_.push_back(static_cast<int>(i));
		}
	}
	
	/**
	 * @brief Destroys the `BufferPoolManager`, freeing up all memory that the buffer pool was using.
	 *
	 * "= default" is sufficient
	 * 	shared_ptrs auto release
	 * 	vectors/maps free their storage
	 * 	does *NOT* flush dirty pages
	 * 		tests call FlushAllPages*() explicitly when care about persistence
	 * 	dirty data left in evictable frames is lost on dtor
	 *
	 * *DO NOT* destroy any while ReadPageGuard{}/WritePageGuard{} outlives this BPM
	 * ...PageGuard{}s hold shared_ptrs into BPM's/our internals
	 * if BPM frees first, PageGuard{}s' Drop() either UB or leaks
	*/
	BufferPoolManager::~BufferPoolManager() = default;
	
	/**
	 * @brief Returns the number of frames that this buffer pool manages.
	 *
	 * no latches needed, since num_frames_ is const after ctor
	 */
	auto BufferPoolManager::Size() const -> size_t { 
		return num_frames_; 
	}
	
	/**
	 * @brief Allocates a new page on disk.
	 *
	 * ### Implementation
	 *
	 * You will maintain a thread-safe, monotonically increasing counter in the form of a `std::atomic<page_id_t>`.
	 * See the documentation on [atomics](https://en.cppreference.com/w/cpp/atomic/atomic) for more information.
	 *
	 * TODO(P1): Add implementation.
	 *
	 * @return The page ID of the newly allocated page.
	 *
	 * expected algorithm
	 * 	1. ```code snuppet
	 * 	   page_id_t new_pid = next_page_id_.fetch_add(1, std::memory_order_relaxed);
	 * 	   ```
	 * 	   this is the only step that does *NOT* need bpm_latch_
	 * 	   fetch_add() is atomic
	 * 	2. decision (i.e. minimal vs eager allocation)
	 * 		minimal:
	 * 			just return new_pid
	 * 			page only becomes "real" when CheckedReadPage()/CheckedWritePage() is called
	 * 			i.e. fault-in path will allocate the frame
	 * 		eager:
	 * 			lock bpm_latch_
	 * 			allocate frame for new_pid now
	 * 			zero fill
	 * 			insert into page_table_
	 * 			return
	 *
	 * 			matches .../project_1/README.md "Allocates a new page on disk.
	 * 	3. ```code snippet
	 * 	   return new_pid;
	 * 	   ```
	 *
	 * concurrency:
	 * 	nothing else
	 * 	next_page_id_ is atomic (i.e. handling the race between multiple NewPage() calls)
	 *
	 * BUSTUB_ENSURE(new_pid != INVALID_PAGE_SIZE) is a reasonable defensive check
	 * would only trigger if we overflowes int32_t, we would have to allocate > 2^31 - 1
	 */
	auto BufferPoolManager::NewPage() -> page_id_t { 
		UNIMPLEMENTED("TODO(P1): Add implementation."); 
	}
	
	/**
	 * @brief Removes a page from the database, both on disk and in memory.
	 *
	 * If the page is pinned in the buffer pool, this function does nothing and returns `false`. Otherwise, this function
	 * removes the page from both disk and memory (if it is still in the buffer pool), returning `true`.
	 *
	 * ### Implementation
	 *
	 * Think about all of the places that a page or a page's metadata could be, and use that to guide you on implementing
	 * this function. You will probably want to implement this function _after_ you have implemented `CheckedReadPage` and
	 * `CheckedWritePage`.
	 *
	 * You should call `DeallocatePage` in the disk scheduler to make the space available for new pages.
	 *
	 * TODO(P1): Add implementation.
	 *
	 * @param page_id The page ID of the page we want to delete.
	 * @return `false` if the page exists but could not be deleted, `true` if the page didn't exist or deletion succeeded.
	 *
	 * expected algorithm:
	 * 	1. ```code snippet
	 * 	   std::scoped_lock l(*bpm_latch_);
	 * 	   ```
	 * 	2. ```code snippet
	 * 	   auto it = page_table_.find(page_id);
	 * 	   if (it == page_table_.end()) {
	 * 	   	// page is not a resident, as per .../project_1/README.md
	 *		// still ask DiskScheduler{} to de-allocate (i.e. page may exist on disk but not cache)
	 *		disk_scheduler_->DeallocatePage(page_id);
	 *		return true;
	 *	   }
	 *	   ```
	 *	3. ``` code snippet
	 *	   frame_id_t fid = it->second;
	 *	   auto frame = frames_[fid];
	 *	   ```
	 *	4. ```code snippet
	 *	   if (frame_->pin_count_.load() > 0) {
	 *		return false;
	 *	   }
	 *	   ```
	 *	   someone is reading/writing it
	 *	   can not delete
	 *	   .load() reads the pin_count_ atomically
	 * 	5. ```code snippet
	 * 	   replacer_->Remove(fid);
	 * 	   page_table_.erase(it);
	 * 	   frame->Reset();
	 * 	   ```
	 * 	   Evict(), no need to flush even if dirty since we are delting the page (i.e. the Remove()```
	 *	   Reset() takes rwlatch_ exclusively briefly inside or hold rwlatch_ exclusive around it ?? TODO!!: proper testing around this 	   
	 *	6. ```code snippet
	 *	   disk_scheduler_->DeallocatePage(page_id);
	 * 	   ```
	 * 	7. ```code snippet
	 * 	   return true;
	 * 	   ```
	 *
	 * subtlety:
	 * 	step 4 reads pin_count_ atomically
	 * 	because we hold bpm_latch_ this means no new ...PageGuard{} can be minted
	 * 	those go through CheckedReadPage/CheckedWritePage which take bpm_latch_
	 * 	pin_count_ won't *increaase* while read, but can still decrease
	 * 		i.e. another ...PageGuard{} Drop() locks bpm_latch_ *AFTER* we take it, then Drop(), then SetEvictable()
	 * 	  	     actually, no. since Drop() takes bpm_latch_ for SetEvictable(), hence, serializing it
	 * 	hence, pin_count_.load() under the bpm_latch_ is authoritative
	*/
	auto BufferPoolManager::DeletePage(
		page_id_t page_id
	) -> bool { 
		UNIMPLEMENTED("TODO(P1): Add implementation."); 
	}
	
	/**
	 * @brief Acquires an optional write-locked guard over a page of data. The user can specify an `AccessType` if needed.
	 *
	 * If it is not possible to bring the page of data into memory, this function will return a `std::nullopt`.
	 *
	 * Page data can _only_ be accessed via page guards. Users of this `BufferPoolManager` are expected to acquire either a
	 * `ReadPageGuard` or a `WritePageGuard` depending on the mode in which they would like to access the data, which
	 * ensures that any access of data is thread-safe.
	 *
	 * There can only be 1 `WritePageGuard` reading/writing a page at a time. This allows data access to be both immutable
	 * and mutable, meaning the thread that owns the `WritePageGuard` is allowed to manipulate the page's data however they
	 * want. If a user wants to have multiple threads reading the page at the same time, they must acquire a `ReadPageGuard`
	 * with `CheckedReadPage` instead.
	 *
	 * ### Implementation
	 *
	 * There are three main cases that you will have to implement. The first two are relatively simple: one is when there is
	 * plenty of available memory, and the other is when we don't actually need to perform any additional I/O. Think about
	 * what exactly these two cases entail.
	 *
	 * The third case is the trickiest, and it is when we do not have any _easily_ available memory at our disposal. The
	 * buffer pool is tasked with finding memory that it can use to bring in a page of memory, using the replacement
	 * algorithm you implemented previously to find candidate frames for eviction.
	 *
	 * Once the buffer pool has identified a frame for eviction, several I/O operations may be necessary to bring in the
	 * page of data we want into the frame.
	 *
	 * There is likely going to be a lot of shared code with `CheckedReadPage`, so you may find creating helper functions
	 * useful.
	 *
	 * These two functions are the crux of this project, so we won't give you more hints than this. Good luck!
	 *
	 * TODO(P1): Add implementation.
	 *
	 * @param page_id The ID of the page we want to write to.
	 * @param access_type The type of page access.
	 * @return std::optional<WritePageGuard> An optional latch guard where if there are no more free frames (out of memory)
	 * returns `std::nullopt`; otherwise, returns a `WritePageGuard` ensuring exclusive and mutable access to a page's data.
	 *
	 * expected algorithm:
	 * 	```code snippet
	 * 	std::unique_lock bpm_guard(*bpm_latch_);
	 * 	// CASE A: page resident.
	 * 	if (auto it = page_table_.find(page_id); it != page_table_.end()) {
	 * 		...
	 * 		gpm_guard.unlock();
	 * 		return WritePageGuard(...);
	 * 	}
	 * 	```
	 * 	release the bpm_latch_ *BEFORE* WritePageGuard{} ctor
	 * 	...GuardPage{} ctro takes rwlatch_ which can block
	 * 	do not take bpm_latch_ while blokcing
	 *	```code snippet
	 *	// CASE B/C: not resident, need a frame.
	 *	auto fid_opt = AllocateFrame();
	 *	if (!fid_opt) {
	 *		return std::nullopt;
	 *	}
	 *	```
	 *	all frames are pinned, therefore return std::nullopt
	 *	then schedule read, remember to dropping bpm_latch_ around future.get()
	 *	then bookkeep *BEFORE* blocking, so that other CheckedReadPage() for the same page_id sees "in flight"
	 *	and either join the wait, or get a separate fault-in from disk queued
	 *	fault-in is OK, since disk_manager_ per-page latch serializes
	 *	release bpm_latch_ before I/O waiting (i.e. it is blocking, do not hold bpm_latch_)
	 *
	 * the 3 cases lined out in .../project_1/README.md
	 * 	case 1:
	 * 		plenty of memory, case A as specced above, page resident
	 * 		or case B+C when free_frames_ is non-empty
	 * 	case 2:
	 * 		no I/O needed
	 * 		no I/O needed
	 * 		this will result in case A specced above
	 * 	case 3:
	 * 		need to evict
	 * 		case B+C, where AllocateFrame falls back to replacer_->Evict()
	 * 		and the victim is dirty so we have to flush it
	 * 		*AND THEN* read a new page
	 * 
	 * subtleties to verify with tests:
	 * 	- two threads racing CheckedWritePage(pid) where pid is not a resident
	 * 	  thread t1 takes bpm_latch_ + populates page_table_[pid] = fid
	 * 	  schedules a read
	 * 	  drops bpm_latch_
	 * 	  blocks thread
	 * 	  thread t2 which takes bpm_latch_, sees pid in page_table_, thus case A fires
	 * 	  thread t2 will try to acquire rwlatch_ on the frame, which is OK, since thread t1 does *NOT* hold rwlatch_ during the fault-in
	 * 	  do not lock rwlatch_ before disk read
	 * 	  we lock rwlatch_ at WritePageGuard{} ctor *AFTER* read completes
	 * 	  	
	 * 	  	wait... this is a problem because thread t2 could grab rwlatch_ first, and see garbage bytes (i.e. half-written, or not in the pool, or something)
	 * 	  	fix:
	 * 	  		take rwlatch_ exclusive before the read
	 * 	  		hold it across disk wait
	 * 	  		release implicitly when WritePageGuard{} ctor takes it
	 * 	  		
	 * 	  		OR
	 *
	 * 	  		take rwlatch_ at WritePageGuard{} ctor
	 * 	  		*AFTER* blocking on the future we created
	 *
	 * 	  	easier:
	 * 	  		case B/C
	 * 	  		lock rwlatch_ *before* unlocking bpm_latch_
	 * 	  		hold rwlatch_ across the disk read
	 * 	  		hand rwlatch_ frame to the WritePageGuard{} ctor
	 * 	  			which will not re-acquire it, since re-acquire would be a deadlock anyways
	*/
	auto BufferPoolManager::CheckedWritePage(page_id_t page_id, AccessType access_type) -> std::optional<WritePageGuard> {
		UNIMPLEMENTED("TODO(P1): Add implementation.");
	}
	
	/**
	 * @brief Acquires an optional read-locked guard over a page of data. The user can specify an `AccessType` if needed.
	 *
	 * If it is not possible to bring the page of data into memory, this function will return a `std::nullopt`.
	 *
	 * Page data can _only_ be accessed via page guards. Users of this `BufferPoolManager` are expected to acquire either a
	 * `ReadPageGuard` or a `WritePageGuard` depending on the mode in which they would like to access the data, which
	 * ensures that any access of data is thread-safe.
	 *
	 * There can be any number of `ReadPageGuard`s reading the same page of data at a time across different threads.
	 * However, all data access must be immutable. If a user wants to mutate the page's data, they must acquire a
	 * `WritePageGuard` with `CheckedWritePage` instead.
	 *
	 * ### Implementation
	 *
	 * See the implementation details of `CheckedWritePage`.
	 *
	 * TODO(P1): Add implementation.
	 *
	 * @param page_id The ID of the page we want to read.
	 * @param access_type The type of page access.
	 * @return std::optional<ReadPageGuard> An optional latch guard where if there are no more free frames (out of memory)
	 * returns `std::nullopt`; otherwise, returns a `ReadPageGuard` ensuring shared and read-only access to a page's data.
	 *
	 * algorithm:
	 * 	identical to CheckedWritePage(), with identical:
	 * 		- pin++
	 * 		  SetEvictable(false)
	 * 		  RecordAccess() dance
	 * 		  same lock ordering rules
	 * 		- same 3 case structure
	 * 			1. resident
	 * 			2. fault-in free
	 * 			3. fault-in with evict
	 * 	but different:
	 * 		- returns ReadPageGuard{}
	 * 		  takes rwlatch_ *SHARED* in its ctor (i.e. as opposed to exclusive in WritePageGuard{})
	 *
	 * the shared backbone between CheckedWritePage()/CheckedReadPage() is a strong argument for
	 * auto LookupOrFault(page_id_t, AccessType) -> std::optional<frame_id_t>;
	 * helper function, CheckedReadPage()/CheckedWritePage() would then become
	 * 	- call LookupOrFault() under bpm_latch_
	 * 	- if nullopt, then return nullopt
	 * 	- else
	 * 	  book-keeping
	 * 	  construct the appropriate guard (difference between CheckedWritePage()/CheckedReadPage() from here)
	 * 	  return
	 * we will diverge only in the guard type build at the end
	*/
	auto BufferPoolManager::CheckedReadPage(
		page_id_t page_id, 
		AccessType access_type
	) -> std::optional<ReadPageGuard> {
		UNIMPLEMENTED("TODO(P1): Add implementation.");
	}
	
	/**
	 * @brief A wrapper around `CheckedWritePage` that unwraps the inner value if it exists.
	 *
	 * If `CheckedWritePage` returns a `std::nullopt`, **this function aborts the entire process.**
	 *
	 * This function should **only** be used for testing and ergonomic's sake. If it is at all possible that the buffer pool
	 * manager might run out of memory, then use `CheckedPageWrite` to allow you to handle that case.
	 *
	 * See the documentation for `CheckedPageWrite` for more information about implementation.
	 *
	 * @param page_id The ID of the page we want to read.
	 * @param access_type The type of page access.
	 * @return WritePageGuard A page guard ensuring exclusive and mutable access to a page's data.
	 *
	 * already implemented for us
	 * note:
	 * 	std::abort() vs. BUSTUB_ENSURE()
	 * 	current code chose to use abort() with a stderr prntln() so that the failure mode is obvious in testing logs
	 * 	keep as is
	*/
	auto BufferPoolManager::WritePage(
		page_id_t page_id, 
		AccessType access_type
	) -> WritePageGuard {
		auto guard_opt = CheckedWritePage(page_id, access_type);
	
		if (!guard_opt.has_value()) {
			fmt::println(stderr, "\n`CheckedWritePage` failed to bring in page {}\n", page_id);
			std::abort();
		}
	
		return std::move(guard_opt).value();
	}
	
	/**
	 * @brief A wrapper around `CheckedReadPage` that unwraps the inner value if it exists.
	 *
	 * If `CheckedReadPage` returns a `std::nullopt`, **this function aborts the entire process.**
	 *
	 * This function should **only** be used for testing and ergonomic's sake. If it is at all possible that the buffer pool
	 * manager might run out of memory, then use `CheckedPageWrite` to allow you to handle that case.
	 *
	 * See the documentation for `CheckedPageRead` for more information about implementation.
	 *
	 * @param page_id The ID of the page we want to read.
	 * @param access_type The type of page access.
	 * @return ReadPageGuard A page guard ensuring shared and read-only access to a page's data.
	 *
	 * already implemented
	 * mirror of WritePage()
	 */
	auto BufferPoolManager::ReadPage(
		page_id_t page_id, 
		AccessType access_type
	) -> ReadPageGuard {
		auto guard_opt = CheckedReadPage(page_id, access_type);
	
		if (!guard_opt.has_value()) {
			fmt::println(stderr, "\n`CheckedReadPage` failed to bring in page {}\n", page_id);
			std::abort();
		}
	
		return std::move(guard_opt).value();
	}
	
	/**
	 * @brief Flushes a page's data out to disk unsafely.
	 *
	 * This function will write out a page's data to disk if it has been modified. If the given page is not in memory, this
	 * function will return `false`.
	 *
	 * You should not take a lock on the page in this function.
	 * This means that you should carefully consider when to toggle the `is_dirty_` bit.
	 *
	 * ### Implementation
	 *
	 * You should probably leave implementing this function until after you have completed `CheckedReadPage` and
	 * `CheckedWritePage`, as it will likely be much easier to understand what to do.
	 *
	 * TODO(P1): Add implementation
	 *
	 * @param page_id The page ID of the page to be flushed.
	 * @return `false` if the page could not be found in the page table; otherwise, `true`.
	 *
	 * expected algorithm:
	 * 	1. caller has external sync so we do not take bpm_latch_ rwlatch_ here
	 * 	   "unsafe"  = unsynchronized
	 *	2. ```code snippet
	 *	   auto it = page_table_.find(page_id);
	 *	   if (it == page_table_.end()) return false
	 *	   ```
	 *	3. ```code snippet
	 *	   frame_id_t fid = it->second;
	 *	   auto frame = frames_[fid];
	 *	   ```
	 *	4. Build DiskRequest{true, frame->GetData()-as-non-const, page_id, promise}
	 * 	   schedule future.get() *WITHOUT* bpm_latch_
	 * 	5. return frame_->is_dirty_ = false
	 * 	6. return true
	 *
	 * carefully consider if the toggle is dirty
	 * 	clear it after future returns successfulyl
	 * 	if we cleared it before then a concurrent eviction could see is_dirty_ is clean and skip the flush
	 * 	losing data if I/O actually fails
	 *
	 * used by:
	 * 	- FlushAllPagesUnsafe() (i.e. loops over the page_table_ calling this)
	 * 	- test teardown
	 * 	- internally by FlushPage() *after* it has taken bpm_latch_
	 * 	  i.e. so that FlushPage() does not recursively lock
	 */
	auto BufferPoolManager::FlushPageUnsafe(
		page_id_t page_id
	) -> bool { 
		UNIMPLEMENTED("TODO(P1): Add implementation."); 
	}
	
	/**
	 * @brief Flushes a page's data out to disk safely.
	 *
	 * This function will write out a page's data to disk if it has been modified. If the given page is not in memory, this
	 * function will return `false`.
	 *
	 * You should take a lock on the page in this function to ensure that a consistent state is flushed to disk.
	 *
	 * ### Implementation
	 *
	 * You should probably leave implementing this function until after you have completed `CheckedReadPage`,
	 * `CheckedWritePage`, and `Flush` in the page guards, as it will likely be much easier to understand what to do.
	 *
	 * TODO(P1): Add implementation
	 *
	 * @param page_id The page ID of the page to be flushed.
	 * @return `false` if the page could not be found in the page table; otherwise, `true`.
	 *
	 * expected algorithm:
	 * 	1. ```
	 * 	   std::unique_lock l(*bpm_latch)
	 * 	   ```
	 * 	2. ```code snippet
	 * 	   auto it = page_table_.find(page_id);
	 * 	   if (it == page_table_.end()) return false;
	 * 	   ```
	 * 	3. ```
	 * 	   auto frame = frames_[it->second];
	 * 	   ```
	 * 	4. ```
	 * 	   std::shared_lock fl(frame->rwlatch_);
	 * 	   ```
	 * 	   takes frame_->rwlatch_ shared
	 * 	   or exclusive, but shared is fine since flush only reads bytes
	 * 	   hold for the duration of I/O wait
	 * 	5. ```
	 * 	   l.unlock()
	 * 	   ```
	 * 	   can release bpm_latch_ now
	 * 	   we hold rwlatch_ preventing eviction
	 * 	   wait....
	 * 	   	eviction takes bpm_latch_, it does not take rwlatch_
	 * 	   	so releasing bpm_latch_ here lets an evictor grab the frame
	 * 	   	we need to keep pin_count > 0 to prevent eviction
	 * 	   	OR keep bpm_latch_ held
	 * 	   decision:
	 * 	   	pin the frame for the duration
	 * 	   	```
	 * 	   	frame->pin_count_.fetch_add(1);
	 * 	   	replacer_->SetEvictable(it->second, false);
	 * 	   	l.unlock();
	 * 	   	```
	 * 	 6. ```
	 * 	    Schedule(... DiskRequest{...})
	 * 	    future.get();
	 * 	    ```
	 * 	    schedule + future.get() *WITHOUT* bpm_latch_
	 * 	 7. ```
	 * 	    frame_->is_dirty_ = false
	 * 	    ```
	 * 	 8. ```
	 * 	    fl.unlock()
	 * 	    ```
	 * 	    shared lock release
	 * 	 9. ```
	 * 	    std::scoped_lock l2(*bpm_latch_);
	 * 	    size_t new_pin = frame->pin_count_.fetch_sub(1) - 1;
	 * 	    if (new_pin == 0) replacer_->SetEvictable(it->second, true);
	 * 	    ```
	 * 	    restore eviction state
	 * 	 10. ```
	 * 	     return true;
	 * 	     ```
	 *
	 * or, a simpler alternative (less throughput, less code):
	 * 	- hold bpm_latch_ the entire time even across future.get()
	 * 	- .../project_1/README.md explicitly warns against this for the *common path*
	 *	  FlushPage() is a rare admin-op, hence the throughput hit is acceptable
	 *	  if we are feeling cautious do this, and optimize later
	*/
	auto BufferPoolManager::FlushPage(
		page_id_t page_id
	) -> bool { 
		UNIMPLEMENTED("TODO(P1): Add implementation."); 
	}
	
	/**
	 * @brief Flushes all page data that is in memory to disk unsafely.
	 *
	 * You should not take locks on the pages in this function.
	 * This means that you should carefully consider when to toggle the `is_dirty_` bit.
	 *
	 * ### Implementation
	 *
	 * You should probably leave implementing this function until after you have completed `CheckedReadPage`,
	 * `CheckedWritePage`, and `FlushPage`, as it will likely be much easier to understand what to do.
	 *
	 * TODO(P1): Add implementation
	 *
	 * expected algorithm:
	 * 	```
	 * 	for (auto &[pid, fid] : page_table_) {
	 * 		FlushPageUnsage(pid);
	 * 	}
	 * 	```
	 * 	already iterates without taking bpm_latch_
	 *
	 * optimization (optional for leaderboard):
	 * 	batch all DiskRequests into one Schedule() call so that the disk scheduler can coalesce
	 */
	void BufferPoolManager::FlushAllPagesUnsafe() { 
		UNIMPLEMENTED("TODO(P1): Add implementation."); 
	}
	
	/**
	 * @brief Flushes all page data that is in memory to disk safely.
	 *
	 * You should take locks on the pages in this function to ensure that a consistent state is flushed to disk.
	 *
	 * ### Implementation
	 *
	 * You should probably leave implementing this function until after you have completed `CheckedReadPage`,
	 * `CheckedWritePage`, and `FlushPage`, as it will likely be much easier to understand what to do.
	 *
	 * TODO(P1): Add implementation
	 *
	 * expected algorithm:
	 * 	```code snippet
	 * 	   std::vector<page_id_t> pids;
	 * 	   {
	 *		std::scoped_lock l(*bpm_latch_);
	 *		pids.reserve(page_table_.size());
	 *		for (auto &kv : page_table_) pids.push_back(kv.first);
	 *	   }
	 *	   for (auto pid : pids) FlushPage(pid);
	 *	```
	 * 	snapshot page_id under bpm_latch_
	 * 		snapshot first because iterating page_table_ while holding bpm_latch_, and then calling FlushPage() would deadlock
	 * 		(i.e. because FlushPage() takes bpm_latch_)
	 * 		snapshotting page_id avoids re-entry
	 * 	and then release bpm_latch_, flushing each page (i.e. releases bpm_latch_ across I/O)
	 * 	edge case:
	 * 		a page in 'pids' might be evicted between snapshot and FlushPage() call
	 * 		FlushPage() handles "not resident" with 'return false;' so this is fine
	 */
	void BufferPoolManager::FlushAllPages() { 
		UNIMPLEMENTED("TODO(P1): Add implementation."); 
	}
	
	/**
	 * @brief Retrieves the pin count of a page. If the page does not exist in memory, return `std::nullopt`.
	 *
	 * This function is thread safe. Callers may invoke this function in a multi-threaded environment where multiple threads
	 * access the same page.
	 *
	 * This function is intended for testing purposes. If this function is implemented incorrectly, it will definitely cause
	 * problems with the test suite and autograder.
	 *
	 * # Implementation
	 *
	 * We will use this function to test if your buffer pool manager is managing pin counts correctly. Since the
	 * `pin_count_` field in `FrameHeader` is an atomic type, you do not need to take the latch on the frame that holds the
	 * page we want to look at. Instead, you can simply use an atomic `load` to safely load the value stored. You will still
	 * need to take the buffer pool latch, however.
	 *
	 * Again, if you are unfamiliar with atomic types, see the official C++ docs
	 * [here](https://en.cppreference.com/w/cpp/atomic/atomic).
	 *
	 * TODO(P1): Add implementation
	 *
	 * @param page_id The page ID of the page we want to get the pin count of.
	 * @return std::optional<size_t> The pin count if the page exists; otherwise, `std::nullopt`.
	 *
	 * expected algorithm:
	 * 	```
	 * 	   std:scoped_lock l(*bpm_latch_);
	 * 	```
	 * 	need this lock for page_table_ access
	 * 	```
	 *	   auto it = page_table_.find(page_id);
	 *	   if (it == page_table_.end()) return std::nullopt;
	 *	   return frames_[it->second]->pin_count_.load();
	 *	```
	 *
	 *	why take bpm_latch_?? even tho pin_count_ is atomic??
	 *		- page_table_ is *NOT* thread-safe
	 *		  iterating it, or reading requires bpm_latch_
	 *		- pin_count_.load() does not need a latch
	 *
	 * .../project_1/README.md hints to do:
	 * 	iterate this GetPinCount() method first as it is the simplest method
	 * 	giving a way to bootstrap the ...PageGuard{} tests without having to first implement CheckedReadPage()/CheckedWritePage()
	*/
	auto BufferPoolManager::GetPinCount(
		page_id_t page_id
	) -> std::optional<size_t> {
		UNIMPLEMENTED("TODO(P1): Add implementation.");
	}
	
}  // namespace bustub
