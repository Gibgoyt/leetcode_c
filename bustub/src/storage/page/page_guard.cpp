//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// page_guard.cpp
//
// Identification: src/storage/page/page_guard.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/page/page_guard.h"
#include <memory>
#include "buffer/arc_replacer.h"
#include "common/macros.h"

namespace bustub {
	/*
	 *	page_guard.cpp
	 *	implementation for ReadPageGuard{} and WritePageGuard{}
	 *
	 *	two classes are deliberately near mirrors of each other
	 *	the only meaningful differences:
	 *		- reads uses rwlatch_ in shared mode
	 *		  writes use rwlatch_ in exclusive mode
	 *		- write exposes GetDateMut()/AsMut()
	 *		  write exposes is_dirty_ on any mutation
	 *
	 *	.../projects_1/README.md warns of 3 possible bugs:
	 *		1. forgetting to invalidate 'that' after a move
	 *		   this leads to double Drop()
	 *		2. wrong order of release in Drop()
	 *		   evicts pins or evicts under live readers
	 *		3. holding bpm_latch_ across disk_scheduler_ future.get(), this will cause every other BPM thread to deadlock
	 *
	 *	every algorithm below is then structured around avoiding these common bugs
	 *
	 *	style note:
	 *		keep ReadPageGuard{} and WritePageGuard{} implementations symmetrical
	 *		if asym. it will pass the simple tests but fail the concurrency testing suite
	 *		consider to derive a common impl header if duplication is difficult to maintain 
	 *		as long as the public API does not change
	*/
	
	/**
	 * @brief The only constructor for an RAII `ReadPageGuard` that creates a valid guard.
	 *
	 * Note that only the buffer pool manager is allowed to call this constructor.
	 *
	 * TODO(P1): Add implementation.
	 *
	 * @param page_id The page ID of the page we want to read.
	 * @param frame A shared pointer to the frame that holds the page we want to protect.
	 * @param replacer A shared pointer to the buffer pool manager's replacer.
	 * @param bpm_latch A shared pointer to the buffer pool manager's latch.
	 * @param disk_scheduler A shared pointer to the buffer pool manager's disk scheduler.
	 *
	 * expected algorithm
	 * 	1. initializer list has already moved the 5 input shared_ptrs into member fields
	 * 	2. frame_->rwlatch_.lock_shared()	// invariant 3 to force shared reader access
	 * 	3. is_valid_ = true			// gating every public accessor
	 * 
	 * caller (i.e. BufferPoolManager::CheckedReadPage) must have already done the following:
	 * 	- looked up or faulted-in page so frame_ is the correct page (i.e. page_id/frame_id)
	 * 	- frame_->pin_count_.fetch_add(1)
	 * 	- replacer_->SetEvictable(frame_->frame_id_, false)
	 * 	- replacer_->RecordAccess(frame_->frame_id_, page_id_)
	 * 	this must all happen under bpm_latch_
	 * 	we do not repeat any of that here since BPM did it
	 *
	 * TODO!!: remove UNIMPLEMENTED() call
	 */
	ReadPageGuard::ReadPageGuard(
		page_id_t page_id,
		std::shared_ptr<FrameHeader> frame,
		std::shared_ptr<ArcReplacer> replacer, 
		std::shared_ptr<std::mutex> bpm_latch,
		std::shared_ptr<DiskScheduler> disk_scheduler
	) : page_id_(page_id), frame_(std::move(frame)), replacer_(std::move(replacer)), bpm_latch_(std::move(bpm_latch)), disk_scheduler_(std::move(disk_scheduler)) {
		UNIMPLEMENTED("TODO(P1): Add implementation.");
	}
	
	/**
	 * @brief The move constructor for `ReadPageGuard`.
	 *
	 * ### Implementation
	 *
	 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are many
	 * great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
	 *
	 * Make sure you invalidate the other guard; otherwise, you might run into double free problems! For both objects, you
	 * need to update _at least_ 5 fields each.
	 *
	 * TODO(P1): Add implementation.
	 *
	 * @param that The other page guard.
	 *
	 * expected algorithm:
	 * 	- page_id_ = that.page_id_
	 * 	- frame_ = std::move(that.frame_)
	 * 	- replacer_ = std::move(that.replacer_)
	 * 	- bpm_latch_ = std::move(that.bpm_latch_)
	 * 	- disk_scheduler_ = std::move(that.disk_scheduler_)
	 * 	- is_valid_ = std::move(that.is_valid_)
	 * 	- that.is_valid_ = false
	 *
	 * notes:
	 * 	- the 5 "fields each" .../project_1/README.md mentions are the 5 owning fields
	 * 	  we should also touch is_valid_ (i.e. the 6th, but is what enforces correctness)
	 * 	- *DO NOT* call that.Drop() here
	 * 	  we transferred ownership
	 * 	  we **DID NOT** release ownership
	 * 	- rwlatch_ stays held
	 * 	  ownership just moved from 'that' to '*this'
	 */
	ReadPageGuard::ReadPageGuard(
		ReadPageGuard &&that
	) noexcept {}
	
	/**
	 * @brief The move assignment operator for `ReadPageGuard`.
	 *
	 * ### Implementation
	 *
	 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are many
	 * great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
	 *
	 * Make sure you invalidate the other guard; otherwise, you might run into double free problems! For both objects, you
	 * need to update _at least_ 5 fields each, and for the current object, make sure you release any resources it might be
	 * holding on to.
	 *
	 * TODO(P1): Add implementation.
	 *
	 * @param that The other page guard.
	 * @return ReadPageGuard& The newly valid `ReadPageGuard`.
	 *
	 * expected algorithm:
	 * 	1. if (this == &that)
	 * 	   return *this
	 * 	   i.e. self-moved guard
	 * 	2. if is_valid_
	 * 	   Drop()
	 * 	   i.e. releases the current page first
	 *	3. move ctor body:
	 *		1. page_ = that.page_id_
	 *		2. frame_ = std::move(that.frame_);
	 *		3. replacer_ = std::move(that.replacer_);
	 *		4. bpm_ = std::move(that.bpm_latch_);
	 *		5. disk_ = std::move(that.disk_scheduler_);
	 *		6. is_ = that.is_valid_;
	 *		7. that.is_ = false;
	 *	4. return *this;
	 *
	 *	skipping Drop() in step 2 will pin leak onto the page that we *USED* to own
	 *	skipping self move check in step 1 would Drop() our own resources then move from already-cleared fields (i.e. UB on the latch unlock)
	 */
	auto ReadPageGuard::operator=(
		ReadPageGuard &&that
	) noexcept -> ReadPageGuard & { 
		return *this; 
	}
	
	/**
	 * @brief Gets the page ID of the page this guard is protecting.
	 *
	 * no latches needed
	 * page_id_ is cheap to read int set at construction set at construction and never mutated thereafter
	 * BUSTUB_ENSURE() keeps invalid guards from returning junk
	 */
	auto ReadPageGuard::GetPageId() const -> page_id_t {
		BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
		return page_id_;
	}
	
	/**
	 * @brief Gets a `const` pointer to the page of data this guard is protecting.
	 *
	 * safe because we hold frame_->rwlatch_ shared for the guard's entire lifetime
	 * no writer can modify data with rwlatch_ pointer is in use
	 * pointer invalidated by Drop()/dtor/move-from
	 * caller should not store it beyond the guard's lifetime
	 */
	auto ReadPageGuard::GetData() const -> const char * {
		BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
		return frame_->GetData();
	}
	
	/**
	 * @brief Returns whether the page is dirty (modified but not flushed to the disk).
	 *
	 * reads from frame_->is_dirty_ which is a plain bool, this is safe because:
	 * 	- hold rwlatch_ shared
	 * 	  writers are excluded (i.e. writers that can mutate is_dirty_)
	 * 	- BPM's eviction path also takes rwlatch_ exclusive before touching it
	*/
	auto ReadPageGuard::IsDirty() const -> bool {
		BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
		return frame_->is_dirty_;
	}
	
	/**
	 * @brief Flushes this page's data safely to disk.
	 *
	 * TODO(P1): Add implementation.
	 *
	 * expected algorithm:
	 * 	1. BUSTUB_ENSURE(is_valid_, ...)
	 * 	2. we already hold rwlatch_ shared (i.e. bytes are stable for the duration)
	 * 	3. ```code snippet
	 * 	   auto promise = disk_scheduler_->CreatePromise();
	 * 	   auto future  = promise.get_future()
	 * 	    DiskRequest req{ 	false	i.e. false, but we actually want write
	 * 	    			frame_->GetDataMut(), or const_char<char*>(GetData()), DiskRequest::data_ is char*, but the worker only read from it during write
	 * 	    			page_id
	 * 	    			std::move(promise)
	 * 	    		}
	 * 	    ```
	 * 	4. ```code snippet
	 * 	   std::vector<DiskRequest> reqs;
	 *	   reqs.push_back(std::move(req));
	 *	   disk_scheduler_->Schedule(reqs);
	 *	   ```
	 *	5. ```code snippet
	 *	   future.get()		// 3rd bug, do not hold bpm_latch_ beyond this line
	 *	   ```
	 *	6. ``` code snippet
	 *	   frame_->is_dirty_ = false
	 *	   ```
	 *	   technicaly requires exclusive rwlatch_
	 *	   a Read guard flushing would mean we are racing is_dirty_ with whoever has set it
	 *	   in practice only WritePageGuard::Flush() is well define	 
	 *
	 * 	we could decide that ReadPageGuard::Flush() is either no-op, or unsupported
	 * 	i.e. no writer could have dirty page after shared lock was acquired
	 * 	this is a defense design too, and much more explicit
	*/
	void ReadPageGuard::Flush() { 
		UNIMPLEMENTED("TODO(P1): Add implementation."); 
	}
	
	/**
	 * @brief Manually drops a valid `ReadPageGuard`'s data. If this guard is invalid, this function does nothing.
	 *
	 * ### Implementation
	 *
	 * Make sure you don't double free! Also, think **very** **VERY** carefully about what resources you own and the order
	 * in which you release those resources. If you get the ordering wrong, you will very likely fail one of the later
	 * Gradescope tests. You may also want to take the buffer pool manager's latch in a very specific scenario...
	 *
	 * TODO(P1): Add implementation.
	 *
	 * expected algorithm:
	 * 	1. ```code snippt
	 * 	   if (!is_valid)
	 * 	   return
	 * 	   ```
	 * 	   bug no. 1 (idempotency)
	 * 	2. ```code snippet
	 * 	   frame_->rwlatch_.unlock_shared()
	 * 	   ```
	 * 	   release the page latch
	 *	3. ```code snippet
	 *	   size_t new_pin = --frame_->pin_count_
	 *	   ```
	 *	   atomic fetch_sub -=1
	 *	4. ```code snippet
	 *	   if (0 == new_pin) {
	 *	   	std::scoped_lock l(*bpm_latch)	 
	 *	  	replacer_->SetEvictable(frame_->frame_id, true)
	 *	   }
	 *	   ```
	 *	   scoped lock to avoid invariant 2 @see page_guard.h top for invariants
	 *	   pin_count_ > 0, means page is not evictable
	 *	   and bug 3 @see top of current file
	 * 	   holding bpm_latch_ across disk_scheduler_ future.get(), this will cause every other BPM thread to deadlock
	 * 	   .../project_1/README.md "you may also want to keep the BPM's latch in a very specific scenario"
	 * 	5. ```code snippet
	 * 	   is_valid_ = false
	 * 	   ```
	 * 	6. optional, but tidy
	 * 	   null out all the shared_ptrs so that we drop the refcounts now rather than at the next move-construction
	 *
	 * order:
	 * 	- unlocking the latch in step 2 *BEFORE* pin_count_ decrement in step 3 means a reader holding shared
	 * 	  and another reader on another CPU can interleave cleanly
	 * 	  i.e. since each one's pin gates eviction
	 * 	- taking only bpm_latch_ around SetEvictable() in step 4 keeps the critical section tiny
	 * 	  avoids holding the latch during anything that could blokc
	 * 	- setting is_valid_ false at the last step makes dtor idempotent
	 * 	  even if someone explicitly uses Drop(), ~ReadPageGuard() has a Drop() that short-circuits
	 *
	 */
	void ReadPageGuard::Drop() { 
		UNIMPLEMENTED("TODO(P1): Add implementation."); 
	}
	
	/*
	 *	@brief The destructor for `ReadPageGuard`. This destructor simply calls `Drop()`. 
	 *
	 *	idempotent, thanks to is_valid_ short circuiting at the top of Drop()
	 *	if a caller has already somehow explicitly called Drop(), then this dtor is a no-op
	*/
	ReadPageGuard::~ReadPageGuard() { 
		Drop(); 
	}
	
	/**********************************************************************************************************************/
	/**********************************************************************************************************************/
	/**********************************************************************************************************************/
	
	/**
	 * @brief The only constructor for an RAII `WritePageGuard` that creates a valid guard.
	 *
	 * Note that only the buffer pool manager is allowed to call this constructor.
	 *
	 * TODO(P1): Add implementation.
	 *
	 * @param page_id The page ID of the page we want to write to.
	 * @param frame A shared pointer to the frame that holds the page we want to protect.
	 * @param replacer A shared pointer to the buffer pool manager's replacer.
	 * @param bpm_latch A shared pointer to the buffer pool manager's latch.
	 * @param disk_scheduler A shared pointer to the buffer pool manager's disk scheduler.
	 *
	 * expected algorithm:
	 * 	1. initializer list moves the 5 inputs into members (i.e. already written)
	 * 	2. ```code snippet
	 * 	   frame_->rwlatch_.lock()
	 * 	   ```
	 * 	   invariant 3, this is write, therefore exclusive
	 * 	   "reads can share; writes exclusive"
	 *
	 * 	3. ```code snippet
	 * 	   is_valid_ = true
	 * 	   ```
	 * 	BPM has already done pin++ + SetEvictable(false) + RecordAccess() under bpm_latch_
	*/
	WritePageGuard::WritePageGuard(
		page_id_t page_id, 
		std::shared_ptr<FrameHeader> frame,
		std::shared_ptr<ArcReplacer> replacer, 
		std::shared_ptr<std::mutex> bpm_latch,
		std::shared_ptr<DiskScheduler> disk_scheduler
	) : page_id_(page_id), frame_(std::move(frame)), replacer_(std::move(replacer)), bpm_latch_(std::move(bpm_latch)), disk_scheduler_(std::move(disk_scheduler)) {
		UNIMPLEMENTED("TODO(P1): Add implementation.");
	}
	
	/**
	 * @brief The move constructor for `WritePageGuard`.
	 *
	 * ### Implementation
	 *
	 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are many
	 * great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
	 *
	 * Make sure you invalidate the other guard; otherwise, you might run into double free problems! For both objects, you
	 * need to update _at least_ 5 fields each.
	 *
	 * TODO(P1): Add implementation.
	 *
	 * @param that The other page guard.
	 *
	 * expected algorithm:
	 * 	identical to ReadPageGuard{} ctor
	 * 	rwlatch_ exclusive ownership moves the frame_ shared_ptr
	 * 	(i.e. the lock is still held, on the same shared_mutex, just by 'this' now, instead of 'that')
	 */
	WritePageGuard::WritePageGuard(
		WritePageGuard &&that
	) noexcept {}
	
	/**
	 * @brief The move assignment operator for `WritePageGuard`.
	 *
	 * ### Implementation
	 *
	 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are many
	 * great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
	 *
	 * Make sure you invalidate the other guard; otherwise, you might run into double free problems! For both objects, you
	 * need to update _at least_ 5 fields each, and for the current object, make sure you release any resources it might be
	 * holding on to.
	 *
	 * TODO(P1): Add implementation.
	 *
	 * @param that The other page guard.
	 * @return WritePageGuard& The newly valid `WritePageGuard`.
	 *
	 * expected algorithm:
	 * 	identical to ReadPageGuard's move-assign
	 * 	self-move guard
	 * 	Drop() if currently valid, then move-consruct semantics, then invalidate 'that'
	 */
	auto WritePageGuard::operator=(
		WritePageGuard &&that
	) noexcept -> WritePageGuard & { 
		return *this; 
	}
	
	/**
	 * @brief Gets the page ID of the page this guard is protecting.
	 */
	auto WritePageGuard::GetPageId() const -> page_id_t {
		BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
		return page_id_;
	}
	
	/**
	 * @brief Gets a `const` pointer to the page of data this guard is protecting.
	 *
	 * even tho we hold rwlatch_ exclusive (i.e. we are the only lock)
	 * exposing a const view is sometimes convenient (e.g. peeking before deciding to mutate)
	 * does *NOT* set is_dirty_
	 */
	auto WritePageGuard::GetData() const -> const char * {
		BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
		return frame_->GetData();
	}
	
	/**
	 * @brief Gets a mutable pointer to the page of data this guard is protecting.
	 *
	 * implementation choice (please commit to one, and stick to it)
	 * 	1. setting frame_->is_dirty_ on every call
	 * 	   simple, safe
	 * 	   slight over-eager as caller might call GetDataMut() and not write
	 * 	   but tests do not care
	 * 	   #if defined(WRITE_PAGE_GUARD_GET_DATA_MUT_OVER_EAGER) or something? so that we can easily swap approaches
	 * 	   TODO!!: get a better defined name, and should if be #if, #else, or should there be #define for both option 1/2 ???
	 * 	2. leaving is_dirty_ alone and relying on the caller to set it
	 * 	   less defensive, more error prone
	 * 
	 * first option should be the default I think, #if defined(<OPTION_2>) then non default
	 * option 1 is the standard interpretation of "dirty flag tracks the intent to mutate, and the only way to mutate is through this function"
	 *
	 * no additional latching is needed
	 * as we already hold rwlatch_ exclusive
	 */
	auto WritePageGuard::GetDataMut() -> char * {
		BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
		return frame_->GetDataMut();
	}
	
	/**
	 * @brief Returns whether the page is dirty (modified but not flushed to the disk).
	 */
	auto WritePageGuard::IsDirty() const -> bool {
		BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
		return frame_->is_dirty_;
	}
	
	/**
	 * @brief Flushes this page's data safely to disk.
	 *
	 * TODO(P1): Add implementation.
	 *
	 * expected algorithm:
	 * 	1. ```code snippet
	 * 	   BUSTUB_ENSURE(is_valid_)
	 * 	   ```
	 * 	2. build DiskRequest {...}
	 * 	   and Schedule() it
	 * 	3. future.get() *without* bpm_latch_
	 * 	4. frame_->is_dirty_ = false
	 * 
	 * holds rwlatch_ exclusive throughout (i.e. we already have it in exclusive mode)
	 * so disk worker is reading from an stable, already owned buffer
	 *
	 * does *NOT* release guard on return
	 * caller still owns page and can keep mutating
	 * is_dirty_ will flip back to true on the next GetDataMut() call
	 */
	void WritePageGuard::Flush() {
		UNIMPLEMENTED("TODO(P1): Add implementation."); 
	}
	
	/**
	 * @brief Manually drops a valid `WritePageGuard`'s data. If this guard is invalid, this function does nothing.
	 *
	 * ### Implementation
	 *
	 * Make sure you don't double free! Also, think **very** **VERY** carefully about what resources you own and the order
	 * in which you release those resources. If you get the ordering wrong, you will very likely fail one of the later
	 * Gradescope tests. You may also want to take the buffer pool manager's latch in a very specific scenario...
	 *
	 * TODO(P1): Add implementation.
	 *
	 * expected algorithm:
	 * 	1. ```code snippet
	 * 	   if (!is_valid_) 
	 * 	   return
	 * 	   ```
	 * 	2. ```code snippet
	 * 	   frame_->rwlatch_.unlock()
	 * 	   ```
	 * 	   release exclusive (not shared, this is write not read)
	 * 	3. ```code snippet
	 * 	   size_t new_pin = --frame_->pin_count_;
	 * 	   ```
	 * 	4. ```code snippet
	 * 	   if (0 == new_pin) {
	 *		std::scoped_lock l(*bpm_latch_);
	 *		replacer_->SetEvictable(frame_->frame_id_, true);
	 * 	   }
	 * 	   ```
	 * 	5. is_valid_ = false
	 * 
	 * important:
	 * 	we do *NOT* auto flush on Drop()
	 * 	if caller dirties page and never calls Flush(), dirtied bytes stays in the frame until next eviction
	 * 	the next eviction will see is_dirty_ == true, scheduling a write back
	 * 	or an explicit BPM::FlushPage()/FlushAllPages()
	 * 	this is the standard behaviour (i.e. caller flushing), auto-flush on Drop() would serialize every page write through disk and take perf
	*/
	void WritePageGuard::Drop() { 
		UNIMPLEMENTED("TODO(P1): Add implementation."); 
	}
	
	/** @brief The destructor for `WritePageGuard`. This destructor simply calls `Drop()`. */
	WritePageGuard::~WritePageGuard() { Drop(); }
	
}  // namespace bustub
