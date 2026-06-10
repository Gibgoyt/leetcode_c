//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// page_guard.h
//
// Identification: src/include/storage/page/page_guard.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "buffer/arc_replacer.h"
#include "buffer/buffer_pool_manager.h"
#include "storage/disk/disk_scheduler.h"
#include "storage/page/page.h"

namespace bustub {
	/*
	 *	page gaurds
	 *	project 1, concurrency layer
	 *
	 *	Two RAII handles
	 *		- ReadPageGuard()
	 *		- WritePageGuard()
	 *		these two functions are the only way callers outside BPM can touch page's bytes
	 *		make the following concurrency invariants exist by construction:
	 *			1. only 1 version of a page in memory at a time
	 *			   BPM enforces this by funneling all access through the page_table_
	 *			   guards *DO NOT* need to re-check this
	 *			2. pin_count_ > 0, means page is not evictable
	 *			   guards bump pin_count_ on construction (i.e. handled by BPM right before handing guard out)
	 *			   drop on Drop() and ~Guard()
	 *			   when pin_count_ transitions to 0, guard tells ArcReplacer to SetEvictable(true) under bpm_latch_
	 *			3. reads can share; writes exclusive
	 *			   ReadPageGuard() holds FrameHeader::rwlatch_ in shared mode for its entire lifetime
	 *			   WritePageGuard() holds it exclusive
	 *			3. is_dirty_ tracks divergence from disk
	 *			   WritePageGuard() sets is_dirty_ on GetDataMut()/Flush() according to implementation choice
	 *			   @see per-method docs page_guard.cpp
	 *			   ReadPageGuard() never mutates is_dirty_
	 *	Lock ordering:
	 *		---
	 *		top -> bottom for lock acquire order
	 *		bottom -> top for lock release order
	 *		---
	 *			- bpm_latch_
	 *			  taken briefly during pin handshake
	 *			  also taken during fault-in at BPM
	 *			- FrameHeader::rwlatch_
	 *			  held for guard's entire lifetime
	 *			- future.get() on disk scheduler
	 *			  drop bpm_latch_ before blocking
	 *		---
	 *		following this order avoids the deadlock as specified at .../project_1/README.md 
	 *		that "§Implementation" warns about
	 *		(i.e. "you may need to release [bpm_latch_] early to prevent deadlocks")
	 *	
	 *	why every member is a shared_ptr<> into BPM???
	 *		a guard can outlive the call that minted it
	 *		caller stores it in a local, does work, eventually drops it
	 *		during its lifetime BPM is being mutated by other threads
	 *			and might evict/fault new pages around it
	 *		the guard needs a stable, ref-counted access to the following:
	 *			- frame_	- so it can release rwlatch_ and decrement pin_count_
	 *					  *ON THE RIGHT FRAME* even if BPM has re-arranged the buffer pool
	 *			- replacer_	- for the SetEvictable(true) call inside Drop()
	 *			- bpm_latch_	- to serialize SetEvictable() call against BPM's own pin_count_ tweaks
	 *			- disk_scheduler_	- so Flush() can schedule write without going through BPM
	 *		if any of these were raw pointers and not a shared pointer, then BPM destruction during a guard's
	 *		lifetime would UB on the guard
	 *		shared_ptr hence keeps the guards alive
	 *
	 *	move semantics:
	 *		part of project 1's concurrrency
	 *		be careful of breaking pin_count_
	 *			- default ctor
	 *				produces invalid guard (i.e. false == is_valid_)
	 *				used only as a stack placeholder before move-assignment
	 *			- copy ctor / copy assign:
	 *				DELETED
	 *				guards model unique resource ownership
	 *			- move ctor
	 *				copy all 5 fields from 'that', setting this->is_valid_ = true
	 *				setting that->is_valid_ = false
	 *				*DO NOT* call that->Drop() (i.e. because we transferred the resource, we did not release it)
	 *			- move assign
	 *				if this->is_valid_ == true then call this->Drop() first
	 *				(i.e. releasing the page that we previously owned)
	 *				then move-construct semantics
	 *				then invalidatea 'that'
	 *			- dtor
	 *				if this->is_valid_ == true, then call Drop()
	 *				else do nothing
	 *
	*/
	
	class BufferPoolManager;
	class FrameHeader;
	
	/**
	 *	@brief An RAII object that grants thread-safe read access to a page of data.
	*
	 *	The _only_ way that the BusTub system should interact with the buffer pool's page data is via page guards. Since
	 *	`ReadPageGuard` is an RAII object, the system never has to manually lock and unlock a page's latch.
	*
	 *	With `ReadPageGuard`s, there can be multiple threads that share read access to a page's data. However, the existence
	 *	of any `ReadPageGuard` on a page implies that no thread can be mutating the page's data.
	 *
	 *	mechanism:
	 *		- constructor
	 *		  private, friended to BPM
	 *		  takes frame->rwlatch_ in *SHARED* mode
	 *		  BPM has already incremented frame->pin_count_ + called replacer_->SetEvictable(frame_id, false) under bpm_latch_ before invoking this ctor
	 *		- for a guard's entire lifetime
	 *		  frame->rwlatch_ is held as *SHARED*
	 *		  any WritePageGuard() req for the same page blocks
	 *		- Drop() 
	 *		  releases shared lock
	 *		  decrements pin_count_
	 *		  if hits 0, flips SetEvictable(frame_id, true) under the bpm_latch_
	*/
	class ReadPageGuard {
			/** @brief Only the buffer pool manager is allowed to construct a valid `ReadPageGuard.` */
			friend class BufferPoolManager;
	
		public:
			/**
			 * @brief The default constructor for a `ReadPageGuard`.
			 *
			 * Note that we do not EVER want use a guard that has only been default constructed. The only reason we even define
			 * this default constructor is to enable placing an "uninitialized" guard on the stack that we can later move assign
			 * via `=`.
			 *
			 * **Use of an uninitialized page guard is undefined behavior.**
			 *
			 * In other words, the only way to get a valid `ReadPageGuard` is through the buffer pool manager.
			 *
			 * implementation notes:
			 * 	- is_valid_ defaults to false via in-class init at field declaration (@see bottom of class)
			 * 	  all other fields are defualt constructed
			 * 	  	page_id = 0 (i.e. junk, never read)
			 * 	  	frame_/replacer_/bpm_latch_/disk_scheduler_ = null shared_ptrs
			 * 	- the accessor BUSTUB_ENSURE gaurds against these defaults
			*/
			ReadPageGuard() = default;
	
			ReadPageGuard(const ReadPageGuard &) = delete;
			auto operator=(const ReadPageGuard &) -> ReadPageGuard & = delete;

			/*
			 *	@brief		- move ctor
			 *			  transfer ownership from 'that' to 'this'
			*/
			ReadPageGuard(
				ReadPageGuard &&that
			) noexcept;

			/*
			 *	@brief		- move assignment
			 *			  release curent ownership, taking the ownership of 'that'
			 *
			 * 	steps:
			 * 		1. if &that == this
			 * 		   no-op (i.e. a self-move)
			 * 		2. if is_valid_, then Drop()
			 * 		   i.e. releasing the page that we currently own
			 * 		3. move all 5 fields from 'that'
			 * 		4. this.is_valid_ = that.is_valid_
			 * 		5. that.is_valid_ = false
			 *		
			 *		skipping step 2 leaks a pin (i.e. pin not freed)
			 *		skipping step 5 double frees
			*/
			auto operator=(
				ReadPageGuard &&that
			) noexcept -> ReadPageGuard &;

			/*
			 *	@brief		- page_id of the page that this guard is protecting
			 *
			 *	BUSTUB_ENSURE ensures is_valid_ (i.e. cheap getter, safe to call repeatedly)
			*/
			auto GetPageId() const -> page_id_t;
			
			/*
			 *	@brief		- read-only view of the page bytes (length BUSTUB_PAGE_SIZE)
			 *
			 *	safe because we hold frame->rwlatch_ shared for the guard's entire lifetime
			 *	pointer remains valid until this is dropped or destroyed
			*/
			auto GetData() const -> const char *;

			/*
			 *	@brief		- typed view
			 *			  reinterpret_case over GetData()
			 *
			 *	use this for slot-page/metadata-page/b+tree-node access in later projects
			 *	caller is responsible for matching 'T' to the actual layout
			*/
			template <class T>
			auto As() const -> const T * {
				return reinterpret_cast<const T *>(
					GetData()
				);
			}
			
			/*
			 *	@brief		- read FrameHeader::is_dirty_
			 *
			 *	reader never ever sets is_dirty_, but can still observe if a previous writer left the page dirty
			 *	safe because we hold rwlatch_ shared (i.e. not exclusive because writers are excluded)
			*/
			auto IsDirty() const -> bool;

			/*
			 *	@brief		- flush this page to disk sync
			 *	
			 *	expected algorithm:
			 *		1. BUSTUB_ENSURE(is_valid_)
			 *		2. we already hold frame->rwlatch_ shared anyways, so the bytes are stable
			 *		3. build out a DiskRequest
			 *		   {is_write=true, data=frame_->GetData(), page_id_, promise}
			 *		4. hand to disk_scheduler_->Schedule()
			 *		5. future.get() to block
			 *		   do *NOT* take bpm_latch_ around this, it is not needed for the flush
			 *		   holding bpm_latch_ would serialize all BPM ops on PageGuard I/O
			 *		6. frame_->is_dirty_ = false
			 *
			 *	Note:
			 *		technically a Read guard flushing is unusual
			 *		reads do not write, hence do not dirty pages
			 *		it is still useful to a forced durability config for higher layers
			*/
			void Flush();

			/*
			 *	@brief		- release guard's resources *NOW*
			 *			  i.e. as opposed to releasing guard's resources at dtor
			 *
			 * 	expected algorithm:
			 * 		1. if (!is_valid_)
			 * 		   then return (i.e. idempotency)
			 * 		2. release the shared frame_->rwlatch_
			 * 		3. decrement frame->pin_count_ (i.e. an atomic fetch_stub)
			 * 		4. if new pin_count == 0
			 * 		   lock bpm_latch_
			 * 		   replacer_->SetEvictable(frame_->frame_id_, true)
			 * 		   unlock bpm_latch_
			 * 		5. is_valid = false
			 * 		6. optionally null shared_ptrs to drop refcounts early
			 *
			 * 		why step 2 before 3?? why release rwlatch_ before decrementing pin_count???
			 * 		because holding rwlatch_ while we decrement gaurantees an evictor
			 * 		evictor is woken by SetEvictable() in step 4
			 * 		we can *NOT* grab the frame and Reset() while another CPU still draws rays out of the pointer
			 * 		but the order between steps 2 and 3 does *NOT MATTER IF* pin_count_ > 0 (i.e. already gates eviction)
			 *
			 * 		why does step 4 take bpm_latch_???
			 * 		replacer_->SetEvictable() is not internally synced to BPM's pin_count_ tweaks
			 * 		and BPM's pin_count_ can be tweaked on other therads
			 * 		hence, taking bpm_latch_ keeps BPM's perspective consistent
			*/
			void Drop();

			/*
			 *	@brief		- dtor
			 *			  identical to Drop() IFF is_valid_ = true
			 *
			 *	body is literally same as Drop() 
			 *	i.e. Drop() is the idempotent path
			*/
			~ReadPageGuard();
	
		private:
			/*
			 *	@brief Only the buffer pool manager is allowed to construct a valid `ReadPageGuard.` 
			*/
			explicit ReadPageGuard(
				page_id_t page_id, 
				std::shared_ptr<FrameHeader> frame, 
				std::shared_ptr<ArcReplacer> replacer,
				std::shared_ptr<std::mutex> bpm_latch, 
				std::shared_ptr<DiskScheduler> disk_scheduler
			);
	
			/** @brief The page ID of the page we are guarding. */
			page_id_t page_id_;
	
			/**
			 * @brief The frame that holds the page this guard is protecting.
			 *
			 * Almost all operations of this page guard should be done via this shared pointer to a `FrameHeader`.
			 *
			 * shared_ptr, not raw, so that FrameHeader survives BPM re-arrangement while we still hold it
			 */
			std::shared_ptr<FrameHeader> frame_;
	
			/**
			 * @brief A shared pointer to the buffer pool's replacer.
			 *
			 * Since the buffer pool cannot know when this `ReadPageGuard` gets destructed, we maintain a pointer to the buffer
			 * pool's replacer in order to set the frame as evictable on destruction.
			 *
			 * only used by Drop() when calling SetEvictable(frame_id, true) call when pin_count_ hits 0
			 */
			std::shared_ptr<ArcReplacer> replacer_;
	
			/**
			 * @brief A shared pointer to the buffer pool's latch.
			 *
			 * Since the buffer pool cannot know when this `ReadPageGuard` gets destructed, we maintain a pointer to the buffer
			 * pool's latch for when we need to update the frame's eviction state in the buffer pool replacer.
			 *
			 * taken briefly in Drop() to serialize the SetEvictable() call
			 * never ever held while blocking on disk_scheduler_ future (i.e. causes a deadlock)
			 */
			std::shared_ptr<std::mutex> bpm_latch_;
	
			/**
			 * @brief A shared pointer to the buffer pool's disk scheduler.
			 *
			 * Used when flushing pages to disk.
			 */
			std::shared_ptr<DiskScheduler> disk_scheduler_;
	
			/**
			 * @brief The validity flag for this `ReadPageGuard`.
			 *
			 * Since we must allow for the construction of invalid page guards (see the documentation above), we must maintain
			 * some sort of state that tells us if this page guard is valid or not. Note that the default constructor will
			 * automatically set this field to `false`.
			 *
			 * If we did not maintain this flag, then the move constructor / move assignment operators could attempt to destruct
			 * or `Drop()` invalid members, causing a segmentation fault.
			 *
			 * set false by:
			 * 	- default ctor
			 * 	- move-from (i.e. after move ctor/move assign)
			 * set true by:
			 * 	- private paramatized ctor (i.e. the ctor that BPM calls)
			 * read by:
			 * 	- every public accessor (i.e. via BUSTUB_ENSURE)
			 * 	- Drop()
			 * 	- dtor
			 */
			bool is_valid_{false};
	
			/**
			 * TODO(P1): You may add any fields under here that you think are necessary.
			 *
			 * If you want extra (nonexistent) style points, and you want to be extra fancy, then you can look into the
			 * `std::shared_lock` type and use that for the latching mechanism instead of manually calling `lock` and `unlock`.
			 *
			 * std::shared_lock<std::shared_mutex> would let us RAII the rwlatch_ acquisition
			 * mirroring the rest of this class' RAII story
			 * trade off:
			 * 	we must default ctor it (i.e. adding another "invalid" state to it via std::defer_lock)
			 * 	or wrap in std::optional
			 * 	picking std::optional<std::shared_mutex<>> is also a common path
			 */
	};
	
	/**
	 *	@brief An RAII object that grants thread-safe write access to a page of data.
	*
	 *	The _only_ way that the BusTub system should interact with the buffer pool's page data is via page guards. Since
	 *	`WritePageGuard` is an RAII object, the system never has to manually lock and unlock a page's latch.
	*
	 *	With a `WritePageGuard`, there can be only be one thread that has exclusive ownership over the page's data. This
	 *	means that the owner of the `WritePageGuard` can mutate the page's data as much as they want. However, the existence
	 *	of a `WritePageGuard` implies that no other `WritePageGuard` or any `ReadPageGuard`s for the same page can exist at
	 *	the same time.
	 *
	 *	mechanism:
	 *		- identical to ReadPageGuard(), except rwlatch_ is acquired in exclusive mode (i.e. as opposed to ReadPageGuard() in shared mode)
	 *		- GetDataMut()/AsMut() exposes mutable bytes
	 *		  calling them is the natural place to set frame_->is_dirty_ = true
	 *
	 *	lock ordering and pin count handshake is identical to ReadPageGuard
	 *	@see the header-level rationale at the top of this file
	*/
	class WritePageGuard {
			/*
			 *	@brief Only the buffer pool manager is allowed to construct a valid `WritePageGuard.` 
			*/
			friend class BufferPoolManager;
	
		public:
			/**
			 * @brief The default constructor for a `WritePageGuard`.
			 *
			 * Note that we do not EVER want use a guard that has only been default constructed. The only reason we even define
			 * this default constructor is to enable placing an "uninitialized" guard on the stack that we can later move assign
			 * via `=`.
			 *
			 * **Use of an uninitialized page guard is undefined behavior.**
			 *
			 * In other words, the only way to get a valid `WritePageGuard` is through the buffer pool manager.
			 */
			WritePageGuard() = default;
	
			WritePageGuard(const WritePageGuard &) = delete;
			auto operator=(const WritePageGuard &) -> WritePageGuard & = delete;

			/*
			 *	@brief		- move ctor
			 *	
			 *	identical semantics to ReadPageGuard() ctor, see there
			*/
			WritePageGuard(
				WritePageGuard &&that
			) noexcept;

			/*
			 *	@brief		- move assign
			 *
			 *	identical semantics to ReadPageGuard() move assign, see there
			*/
			auto operator=(
				WritePageGuard &&that
			) noexcept -> WritePageGuard &;

			auto GetPageId() const -> page_id_t;
			auto GetData() const -> const char *;

			template <class T>
			auto As() const -> const T * {
				return reinterpret_cast<const T *>(GetData());
			}
			
			/*
			 *	@brief		- mutable view of the page bytes (length BUSTUB_PAGE_LENGTH)
			 *
			 *	important:
			 *		this would be the conventional page to mark dirty page
			 *		i.e. frame_->is_dirty_ = true
			 *
			 *	.../project_1/README.md is loose on where exactly to set is_dirty_
			 *	setting it here means that the moment any caller asks for a mutable view, we assume the intent to mutate
			 *	alternative is to set it lazily in Flush() based on observed mutation, but that is harder to track and tests do not reward it
			 *
			 *	holding an exclusive rwlatch_ for the gaurd's lifetime makes this safe
			*/
			auto GetDataMut() -> char *;

			/*
			 *	@brief		- typed mutable view
			 *			  same caveats as GetDataMut()
			 *
			 *	sets is_dirty_ for the same reason that GetDataMut() does
			 *	i.e. it ultimately calls through
			*/
			template <class T>
			auto AsMut() -> T * {
				return reinterpret_cast<T *>(
					GetDataMut()
				);
			}

			auto IsDirty() const -> bool;

			/*
			 *	@brief		- flush the page to disk synchronously
			 *
			 *	expected algorithm:
			 *		1. BUSTUB_ENSURE(is_valid_)
			 *		2. hold rwlatch_ exclusive
			 *		   bytes are stable
			 *		   no concurrent reader/writer is observing an intermediate state
			 *		3. schedule a write via disk_scheduler_
			 *		   future.get()
			 *		   with*OUT* bpm_latch_
			 *		4. frame_->is_dirty_ = false
			 *
			 *	Note:
			 *		Flush() does *NOT* release the guard
			 *		the page stays pinned to ArcReplacer
			*/
			void Flush();
			
			/*
			 *	@brief		- release the guard
			 *	
			 *	identical to ReadPageGuard::Drop() algorithm but releases rwlatch_ exclusive instead of shared
			*/
			void Drop();

			~WritePageGuard();
	
		private:
			/*
			 *	@brief Only the buffer pool manager is allowed to construct a valid `WritePageGuard.` 
			*/
			explicit WritePageGuard(
				page_id_t page_id, 
				std::shared_ptr<FrameHeader> frame, 
				std::shared_ptr<ArcReplacer> replacer,
				std::shared_ptr<std::mutex> bpm_latch,
				std::shared_ptr<DiskScheduler> disk_scheduler
			);
	
			/*
			 *	@brief The page ID of the page we are guarding. 
			*/
			page_id_t page_id_;
	
			/**
			 * @brief The frame that holds the page this guard is protecting.
			 *
			 * Almost all operations of this page guard should be done via this shared pointer to a `FrameHeader`.
			 */
			std::shared_ptr<FrameHeader> frame_;
	
			/**
			 * @brief A shared pointer to the buffer pool's replacer.
			 *
			 * Since the buffer pool cannot know when this `WritePageGuard` gets destructed, we maintain a pointer to the buffer
			 * pool's replacer in order to set the frame as evictable on destruction.
			 */
			std::shared_ptr<ArcReplacer> replacer_;
	
			/**
			 * @brief A shared pointer to the buffer pool's latch.
			 *
			 * Since the buffer pool cannot know when this `WritePageGuard` gets destructed, we maintain a pointer to the buffer
			 * pool's latch for when we need to update the frame's eviction state in the buffer pool replacer.
			 */
			std::shared_ptr<std::mutex> bpm_latch_;
	
			/**
			 * @brief A shared pointer to the buffer pool's disk scheduler.
			 *
			 * Used when flushing pages to disk.
			 */
			std::shared_ptr<DiskScheduler> disk_scheduler_;
	
			/**
			 * @brief The validity flag for this `WritePageGuard`.
			 *
			 * Since we must allow for the construction of invalid page guards (see the documentation above), we must maintain
			 * some sort of state that tells us if this page guard is valid or not. Note that the default constructor will
			 * automatically set this field to `false`.
			 *
			 * If we did not maintain this flag, then the move constructor / move assignment operators could attempt to destruct
			 * or `Drop()` invalid members, causing a segmentation fault.
			 */
			bool is_valid_{false};
	
			/**
			 * TODO(P1): You may add any fields under here that you think are necessary.
			 *
			 * If you want extra (nonexistent) style points, and you want to be extra fancy, then you can look into the
			 * `std::unique_lock` type and use that for the latching mechanism instead of manually calling `lock` and `unlock`.
			 *
			 * std::unique_lock<std::shared_mutex> for exclusive lock variant
			 * std::optional<> wrapping trick applies the same as ReadPageGuard
			 */
	};
	
}  // namespace bustub
