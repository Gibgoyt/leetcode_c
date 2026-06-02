
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.h
//
// Identification: src/include/buffer/arc_replacer.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <list>
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <unordered_map>

#include "common/config.h"
#include "common/macros.h"

namespace bustub {
	/*
	 *	ArcReplacer
	 *
	 *	decides which frame to kick out of the buffer pool when 
	 *	the page is full and a new page must be brought in from disk
	 *	the actual reading/writing of pages is BufferPoolManager
	 *	this calss only holds bookkeeping
	 *
	 *	implements a variant of IBM's ARC policy (i.e. adaptive replacement cache)
	 *	compared to plain LRU (i.e. least recently used), ARC splits cache entries by access patterns
	 *	so that a single sequential scan can not pollute the entire cache, adapting the
	 *	split point dynamically to the workload detected
	 *
	 * 	Four lists:
	 * 		mru_	- most recently used
	 * 			  pages currently in the buffer pool that have been access exactly once recently
	 * 			  acts as a landing pad for new pages and one-off scans
	 * 		mfu_	- most frequently used
	 * 			  pages currently in the buffer pool that have been accessed at least twice recently
	 * 			  these are hot pages that we want to keep
	 * 		mru_ghost_	- pages recently evicted from mru_
	 * 				  page itself no longer in the buffer pool
	 * 				  only remember "this page ID used to live here"
	 * 				  subsequent reference to a page in mru_ghost_ is a *pseudo-hit*
	 * 				  telling us that "we evicted it too soon", MRU side starving, grow its budget
	 * 		mfu_ghost_	- pages recently evicted from mfu_
	 * 				  symmetric to mru_ghost_
	 * 				  pseudo-hit here means that "we evicted a hot page too soon"
	 * 				  MFU side starving, shrink MRU budget
	 *
	 * 	five different sizes (do not conflate them):
	 * 		replacer_size_	- capacity = no. of frames in the buffer pool
	 * 				  equal to original ARC paper's `c` fixed at `ctor`
	 * 		mru_.size()	- actual no. of entries in mru_
	 * 		mru_.target_size_	- the targe size of mru_ (i.e. original paper's `p`
	 * 					  (adaptive)
	 * 					  grows on mru_ghost_ hits
	 * 					  shrinkgs on mfu_ghost_ hits
	 * 					  always in [0, replacer_size_]
	 * 					  starts at 0
	 * 		curr_size_	- no. of frames that are both alive + evictable
	 * 				  this is what Size() must return
	 * 				  ghost entries never count
	 * 				  pinned frames never count
	 * 		sum of all 4 lists	- bounded by 2 * replacer_size_
	 * 					  ghost lists give the extra c of "memory of recent evictions"
	 * 					  	|mru_| + |mru_ghost_| <= replacer_size_
	 * 					  	|mfu_| + |mfu_ghost_| <= replacer_size_
	 *
	 * 	while the page is in a buffer pool, then it is bound 1:1 to a frame
	 * 	as soon as it is evicted, that page no longer has a frame - only a page_id remaining
	 * 	hence, 2 different maps:
	 * 		alive_map_	- frame_id_t->FrameStatus
	 * 				  entries in mru_ or mfu_
	 * 				  frame_id is the durable ID while the page is in the pool
	 * 		ghost_map_	- page_id_t->FrameStatus
	 * 				  entries on mru_ghost_ or mfu_ghost_
	 * 				  only the page_id survives an eviction
	 * 	
	 * 	on an eviction, the entry pivots from alive_map_ (which is frame keyed) to ghost_map_ (which is page keyed)
	 *	but on a ghost hit re-admission, it pivots back
	 *
	 *	CONCURRENCY:
	 *		every public method takes latch_ for its entire body:
	 *			- public methods must never call other public methods on `this.`
	 *			  would attempt to re-lock a non-recursive mutex, resulting in undefined behaviour
	 *			- any factoring out of common logic should be into a **private + unsynchronized**
	 *			  helper that assumes that the caller already holds latch_
	 *
	 *	Performance Note
	 *		RecordAccess must run in amortized O(1)
	 *		std::list itself supporst O(1) splice/erase/push_front **GIVEN AN ITERATOR**
	 *		finding an iterator by value is O(n)
	 *		the starter relies on alive_map_ and ghost_map_ to give O(1) lookup of the entry
	 * 		for O(1) list mutation we will likely also require each FrameStatus to carry std::list::iterator
	 *		that points to its node (i.e. erase/splice directly without scanning the list)
	 * 		without this, RecordAccess() degenerates to O(n) per call
	*/

	  /*
	   *	AccessType
	   *	classification of why a page was accessed
	   *	only relevant for the leaderboard optimization in Project 1, Task 3, where you may want to treat sequential scans
	   *	differently from random lookups to prevent cache pollution
	   *
	   *	for the base RecordAccess algorithm, the value is ignored
	*/
	enum class AccessType { 
		Unknown = 0, 
		Lookup, 
		Scan, 
		Index 
	};
	
	 /*
	  *	ArcStatus
	  *	which of the 4 arc lists does the entry currently belong to
	  *	status is part of FrameStatus so each entry knows its own list
	  *	and we can branch RecordAccess in O(1) without scanning the list
	*/
	enum class ArcStatus { 
		MRU, 
		MFU, 
		MRU_GHOST, 
		MFU_GHOST 
	};
	
	// TODO(student): You can modify or remove this struct as you like.
	/*
	 *	FrameStatus
	 *
	 *	per entry bookkeeping shared between maps and lists
	 *
	 *	page_id_	- the logical page that this entry refers to
	 *			  always valid, even for ghosts, which is the entire point of ghost entries
	 *	frame_id_	- frame slot in the buffer pool that currently holds this page
	 *			  meaningful **ONLY** when alive (MRU/MFU)
	 *			  ghost entries has no frame, so this is not used
	 *	evictable_	- whether the buffer pool considers this *frame* a legal eviction candidate *right now*
	 *			  (i.e. 0 == pin_count)
	 *			  ghost entries should *always* be considered non-evictable, since Evict() does not look at them
	 *			  they have no frame to evict
	 *	arc_status_	- which of the 4 lists this entry currently lives in
	 *
	 *	Performance Note:
	 *		for O(1) list mutation in RecordAccess, also store a std::list<...>::iterator pointing
	 *		to this entry's node in its current list
	 *		letting you list.erase(), list.push_front() in O(1) without searching
	*/
	struct FrameStatus {
		page_id_t page_id_;
		frame_id_t frame_id_;
		bool evictable_;
		ArcStatus arc_status_;
		FrameStatus(
			page_id_t pid, 
			frame_id_t fid, 
			bool ev, 
			ArcStatus st
		) : page_id_(pid), frame_id_(fid), evictable_(ev), arc_status_(st) {
			//
		}
	};
	
	/**
	 *	ArcReplacer implements the ARC replacement policy.
	 *
	 *	public API contract, this is just the header
	 *
	 *		Size()		- no. of alive + evictable frames
	 *		SetEvictable(f,e)	- flip evictable flag on alive frame
	 *					  adjusts curr_size_.
	 *					  no-op on ghost/unknown frames
	 *					  invalid frame_id is a programming error
	 *		RecordAccess(f, p, t)	- register an access
	 *					  four case dispatch	@see arc_replacer.cpp
	 *					  always ends with the entry in front of mfu_ for a hit, and in front of mru_ for a miss
	 *		Evict()			- chose eviction per ARC eviction rule
	 *					  move to corresponding ghost list
	 *					  drop from alive_map_ and add to ghost_map_
	 *					  return freed frame_id
	 *					  nullopt if nothing is evictable
	 *		Remove(f)		- externally drops a frame whose page is being deleted from the system
	 *					  unlike eviction, this does **NOT** create ghost entry, page is GONE
	 */
	class ArcReplacer {
		public:
			/*
			 *	@brief	- construct an empty ArcReplacer
			 *	@param num_frames	- the buffer pool's frame count (original ARC's `c`)
			 *
			 *	initial state:
			 *		all 4 lists empty
			 *		curr_size = 0
			 *		mru_target_size_ = 0
			 *		replacer_size_ = num_frames
			*/
			explicit ArcReplacer(
				size_t num_frames
			);
		
			DISALLOW_COPY_AND_MOVE(ArcReplacer);
		
			/**
			 * TODO(P1): Add implementation
			 *
			 * @brief Destroys the LRUReplacer.
			 */
			~ArcReplacer() = default;
		
			/*
			 *	@brief		- choose + remove a victim frame per ARC eviction rule
			 *
			 *	the chosen alive entry moves from its alive list (i.e. mru_/mfu_)
			 *	to the corresponding ghost lists (i.e. mru_ghost_/mfu_ghost_)
			 *	mapping moves from alive_map_ to ghost_map_
			 *	curr_size_ decremented
			 *
			 * 	eviction preference:
			 * 		if mru_.size() < mru_target_size_ then try MFU first, else try MRU first
			 * 		if preferred side has no evictable entry, try the other side
			 * 		if nothing, return std::nulopt
			 *
			 * 	@return		- frame_id of evicted frame
			 * 			  std::nullopt if NULL
			*/
			auto Evict() -> std::optional<frame_id_t>;

			/*
			 *	@brief		- record that page_id was just accessed in frame_id
			 *
			 *	4 case dispatch (@see arch_replacer.cpp)
			 *		- hit in mru_/mfu_, then move to front of mfu_
			 *		- hit in mru_ghost_, then grow mru_target_size_, promote to mfu_
			 *		- hit in mfu_ghost_, shrink mru_target_size_, promote into mfu_
			 *		- total miss, maybe drop a ghost tail + insert in front of mru_
			 *	
			 *	@param frame_id		- frame currently holding the page (i.e. alive ID)
			 *	@param page_id		- page being accessed (i.e. durable ID)
			 *	@param access_type	- hint for leaderboard optimization; ignored otherwise
			*/
			void RecordAccess(
				frame_id_t frame_id, 
				page_id_t page_id, 
				AccessType access_type = AccessType::Unknown
			);

			/*
			 *	@brief	- mark frame_id evictable or not
			 *		  and adjusts curr_size_ accordingly
			 *
			 *
			 *	called by BufferPoolManager whenever a page's pin_count transitions across zero
			 *	pin_count 0→>0 means SetEvictable(f, false)
			 *	>0->0 means SetEvictable(f, true)
			 *	a frame must already be inside an alive list (i.e. mru_/mfu_)
			 *	calls on ghost/missing frame is no-ops
			 *	invalid frame_id is a programming error and should abort/throw
			*/
			void SetEvictable(
				frame_id_t frame_id, 
				bool set_evictable
			);

			/*
			 *	@brief		- removes an evictable + alive frame from the replacer entirely
			 *
			 *	used by BufferPoolManager whenever a page is deleted (not just evicted, fully deleted)
			 *	unlike Evict(), this does not create a ghost entry, because the page itself is being destroyed 
			 *	so there is nothing to "remeber evicting"
			 *
			 *	preconditions:
			 *		- if frame_id is unknown/ghost then no-op (do nothing)(
			 *		- if frame_id alive, but non-evictable, it is a caller bug
			 *		  a page should never be deleted while pinned
			 *		  abort throw @see arch_replacer.cpp
			*/
			void Remove(frame_id_t frame_id);

			/*
			 *	@brief		- no. of alive + evictable frames (i.e. == curr_size_)
			 *
			 *	ghost entries do not count
			 *	pinned + alive entries do not count
			*/
			auto Size() -> size_t;
		private:
			// TODO(student): implement me! You can replace or remove these member variables as you like.
			/*
			 *	4 ARC lists
			 *
			 *	each list is maintained
			 *	front = mru_ end
			 *	back = mfu_ end
			 *	insert always from the front, evict always from the back
			 *
			 *	mru_/mfu_ holds frame_ids because alive entries UID by frame_id (i.e. 1:1 with page *while in pool*)
			 *	ghost lists hold page_id because there is no frame once page has been evicted
			*/
			std::list<frame_id_t> mru_;
			std::list<frame_id_t> mfu_;
			std::list<page_id_t> mru_ghost_;
			std::list<page_id_t> mfu_ghost_;
		
			/*
			 *	index maps for O(1) lookup
			 *	
			 *	2 maps because alive keyed by frame_id (i.e. durable while alive)
			 *	ghost entries keyed by page_id (i.e. the only ID that survives eviction)
			 *	lookup either O(1)
			 *	(for O(1) list mutation store std::list::iterator inside FrameStatus)
			*/
			/* 
			 *	record entries in mru_ and mfu_
			 *	this uses frame_id_t to guarantee no duplicate records for the same
			 *	frame when they are alive 
			*/
			std::unordered_map<frame_id_t, std::shared_ptr<FrameStatus>> alive_map_;
			/*
			 *	record entries in mru_ghost_ and mfu_ghost_
			 *	this uses page_id_t but not frame_id_t because page_id is the unique
			 *	identifier in ghost lists
			*/
			std::unordered_map<page_id_t, std::shared_ptr<FrameStatus>> ghost_map_;
		
			/*
			 *	adaptive size counters
			*/

			/*
			 *	alive, evictable entries count 
			 *
			 * 	returned by Size()
			 * 	incremented on SetEvictable(*, true) of an alive + not already evictable frame
			 * 	decremented on reverse transition
			 * 	decremented also on Evict() and/or Remove() of an evictable frame
			*/
			[[maybe_unused]] size_t curr_size_{0};

			/*
			 *	p as in original paper
			 *
			 *	the adaptive target for mru_.size()
			 *	grows on hits to mru_ghost_ (i.e. mru_ was starving)
			 *	shrinks on mfu_ghost_ hits (i.e. mfu_ was starving)
			 *	clamped to [0, replacer_size_]
			*/
			[[maybe_unused]] size_t mru_target_size_{0};
			/* 
			 *	c as in original paper 
			 *	fixed at construction
			 *	equals buffer pools frame count
			*/
			[[maybe_unused]] size_t replacer_size_;

			/*
			 *	coarse grained mutex held for entire body of every public method
			 *	do not call public methods from other public methods
			 *	std::mutex is non recursive
			 *	use private unlocked helpers to share logic across public entry points
			*/
			std::mutex latch_;
		
			// TODO(student): You can add member variables / functions as you like.
	};
	
}  // namespace bustub
