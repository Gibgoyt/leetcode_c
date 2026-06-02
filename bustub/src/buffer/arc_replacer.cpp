// :bustub-keep-private:
//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// arc_replacer.cpp
//
// Identification: src/buffer/arc_replacer.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/arc_replacer.h"
#include <optional>
#include "common/config.h"

namespace bustub {
	/*
	 *	global invariants we must add:
	 *		- curr_size_ == #{ e in alive_map_ : e.evictable_ == true }
	 *		- each frame_id *must* occur in at most either mru_/mfu_
	 *		  and only if it has a corresponding entry in alive_map_ whose arc_status_ matches the list it's in
	 *		- each page_id occurs in at most either mru_ghost_/mfu_ghost_
	 *		  only if it has a corresponding entry in ghost_map_ whose arc_status_ matches the list it's in
	 *		- alive_map_/ghost_map_ are disjoing in concept
	 *		  a given page is either alive (frame_id) or ghosted (page_id)
	 *		- |mru_| + |mru_ghost_| <= replacer_size_
	 *		  |mru_| + |mru_ghost_| + |mfu_| + |mfu_ghost_| <= 2 *	replacer_size_
	 *		  0 <= mru_target_size_ <= replacer_size_
	 *		- front of each list is most recent
	 *		  back of each list is least recent
	 *		  Evict() and ghost tail trimming must remove from the back
	 *		  RecordAccess always inserts from the front
	 *
	 *	every public method *MUST* preserve all of these invariants by the time it returns
	 *	right now function bodies are stub/TODO, comments describe what the function should do
	*/
	
	/**
	 *
	 *	TODO(P1): Add implementation
	 *
	 *	@brief a new ArcReplacer, with lists initialized to be empty and target size to 0
	 *	@param num_frames the maximum number of frames the ArcReplacer will be required to cache
	 *
	 *	sets up immutable replacer capacity (`c` in origin IBM ARC)
	 *	all other states defaults to their zero-initializd values
	 *		- 4 empty lists
	 *		- 2 empty maps
	 *		- curr_size = 0
	 *		- mru_target_size = 0
	 */
	ArcReplacer::ArcReplacer(
		size_t num_frames
	) : replacer_size_(num_frames) {}
	
	/**
	 *	TODO(P1): Add implementation
	 *
	 *	@brief Performs the Replace operation as described by the writeup
	 *	that evicts from either mfu_ or mru_ into its corresponding ghost list
	 *	according to balancing policy.
	 *
	 *	If you wish to refer to the original ARC paper, please note that there are
	 *	two changes in our implementation:
	 *	1. When the size of mru_ equals the target size, we don't check
	 *	the last access as the paper did when deciding which list to evict from.
	 *	This is fine since the original decision is stated to be arbitrary.
	 *	2. Entries that are not evictable are skipped. If all entries from the desired side
	 *	(mru_ / mfu_) are pinned, we instead try victimize the other side (mfu_ / mru_),
	 *	and move it to its corresponding ghost list (mfu_ghost_ / mru_ghost_).
	 *
	 *	@return frame id of the evicted frame, or std::nullopt if cannot evict
	 *
	 *	Algorithm:
	 *		must hold latch_ for the entire body
	 *		1. decide preferred size
	 *			prefer_mru = (mru_.size()) > mru_target_size_
	 *			if the mru_ list has grown to above its target, the next eviction should be deleted 
	 *			to shrink back towards target
	 *			or pull from mfu_ so mru_ can keep growin
	 *		2. scan preferred side from the back to front
	 *		   (back side is LRU side)
	 *		   look for first FrameStatus where (true == evictable_)
	 *			- found 	-> that is now the victim
	 *			- not found 	-> every entry there is pinned, repeat the scan on the other scan
	 *					-> not found on other side too
	 *						-> return std::nullopt
	 *		3. demote victim from alive->ghost
	 *			- erase victim from mru_/mfu_ alive list
	 *			- push victin page_id to FRONT of matching ghost list mru_ghost_/mfu_ghost_
	 *			- erase from alive_map_
	 *			  insert into ghost_map_
	 *			  arc_status_ = MRU_GHOST/MFU_GHOST as appropriate
	 *		4. decrement curr_size_ 
	 *		   since we removed an evictable entry
	 *		5. return victim.frame_id_
	 *
	 *	warning:
	 *		do **NOT** scan from front to back (LRU end is at the back)
	 *		ARC paper, and std::list, both agree on this convention
	*/
	auto ArcReplacer::Evict() -> std::optional<frame_id_t> { 
		return std::nullopt; 
	}
	
	/**
	 *	TODO(P1): Add implementation
	 *
	 *	@brief Record access to a frame, adjusting ARC bookkeeping accordingly
	 *	by bring the accessed page to the front of mfu_ if it exists in any of the lists
	 *	or the front of mru_ if it does not.
	 *
	 *	Performs the operations EXCEPT REPLACE described in original paper, which is
	 *	handled by `Evict()`.
	 *
	 *	Consider the following four cases, handle accordingly:
	 *	1. Access hits mru_ or mfu_
	 *	2/3. Access hits mru_ghost_ / mfu_ghost_
	 *	4. Access misses all the lists
	 *
	 *	This routine performs all changes to the four lists as preperation
	 *	for `Evict()` to simply find and evict a victim into ghost lists.
	 *
	 *	Note that frame_id is used as identifier for alive pages and
	 *	page_id is used as identifier for the ghost pages, since page_id is
	 *	the unique identifier to the page after it's dead.
	 *	Using page_id for alive pages should be the same since it's one to one mapping,
	 *	but using frame_id is slightly more intuitive.
	 *
	 *	@param frame_id id of frame that received a new access.
	 *	@param page_id id of page that is mapped to the frame.
	 *	@param access_type type of access that was received. This parameter is only needed for
	 *	leaderboard tests.
	 *
	 *	algorithm
	 *		case 1:
	 *			alive hit (entry exists in alive_map_ (i.e. mru_/mfu_)
	 *				- if mru_, unlink from mru_, push to front of mfu_
	 *				- if mfu_, unlink from mfu_, push to front of mfu_ (i.e. moved from back to front of mfu_)
	 *				- update FrameStatus.arc_status_ = MFU
	 *				- curr_size_ unchanged
	 *				  mur_target_size unchanged
	 *		case 2:
	 *			pseudo hit on mru_ghost_
	 *				- adapt UP (i.e. mru_ side too small)
	 *				  delta = (|mru_ghost_| >= |mfu_ghost_|) ? (1) : (|mfu_ghost_| / |mru_ghost_|)	// floor div	
	 *				  mru_target_size_ = min(replacer_size_, mru_target_size_ + delta)
	 *				- then promote ghost into alive mfu_ entry
	 *					- remove page_id from mru_ghost_ and ghost_map_
	 *					- push frame_id to front of mfu_
	 *					  insert into alive_map_ with arc_status = MFU and evictable = false
	 *			caller BPM has already loaded the page into frame_id
	 *		case 3:
	 *			pseudo hit on mfu_ghost_
	 *			symmetric to case 2, but adapt down
	 *				- adapt DOWN (i.e. mfu_ side too small)
	 *				   delta = (|mfu_ghost_| >= |mru_ghost_|) ? (1) : (|mfu_ghost_| / |mru_ghost_|)	// floor div
	 *				   mru_target_size = (mru_target_size_ > delta) ? (mru_target_size_ - delta) : (0)
	 *				 - promote ghost into alive mfu_ entry (exactly as case 2)
	 *		case 4:
	 *			full miss (page_id not in alive_map_/ghost_map_)
	 *				- apply capacity invariants
	 *				  ghost lists must be non-empty
	 *					mru_ = replacer_size, which can not happen
	 *					- If |mru_| + |mru_ghost_| == replacer_size_:
	 *					  no ghosts (mru_ghost_ = 0, meaning mru_ = replacer_size_ which can never happen)
	 *						pop_back from mru_ghost_
	 *						erase page from ghost_map_
	 *					- Else (so |mru_| + |mru_ghost_| < replacer_size_):
	 *						- If |mru_| + |mru_ghost_| + |mfu_| + |mfu_ghost_| == 2*replacer_size_:
	 *							pop_back from mfu_ghost_
	 *							erase page from ghost_map_
	 *						- Else:
	 *							no ghost trimming needed
	 *				- insert new entry
	 *					- push_front frame_id into mru_
	 *					- insert into alive_map_ with arc_status_ = MRU, evictable = false
	 *
	 *	for every case insert from the *FRONT* so Evict() can scan from the back
	 *
	 *	performance note:
	 *		each list mutation must be O(1)
	 *		store list iterators in FrameStatus
	 *		that way "remove from X, push to front of Y" is std::list::erase + std::list::push_front (no scan)
	 */
	void ArcReplacer::RecordAccess(
		frame_id_t frame_id,
		page_id_t page_id, 
		[[maybe_unused]] AccessType access_type
	) {
		// 
	}
	
	/**
	 *	TODO(P1): Add implementation
	 *
	 *	@brief Toggle whether a frame is evictable or non-evictable. This function also
	 *	controls replacer's size. Note that size is equal to number of evictable entries.
	 *
	 *	If a frame was previously evictable and is to be set to non-evictable, then size should
	 *	decrement. If a frame was previously non-evictable and is to be set to evictable,
	 *	then size should increment.
	 *
	 *	If frame id is invalid, throw an exception or abort the process.
	 *
	 *	For other scenarios, this function should terminate without modifying anything.
	 *
	 *	@param frame_id id of frame whose 'evictable' status will be modified
	 *	@param set_evictable whether the given frame is evictable or not
	 *
	 *	algorithm:
	 *		must hold latch_
	 *			1. look up frame_id in alive_map_
	 *				- not present -> other scenario -> silently return
	 *				  frame is either ghost/unknown
	 *				  ghosts have no pin/unpin semantics 
	 *			2. validate frame_id (e.g. < replacer_size_)
	 *			   otherwise abort/throw as per doc contract above
	 *			   BufferPoolManager should never pass garbage in
	 *			3. if entry.evicatable_ = set_evictable, then no-op
	 *			4. else flip entry.evictable_ + adjust curr_size_ ±1
	 *				- set_evictable_ = true, then curr_size ++
	 *				- set_evictable_ = false, then curr_size --
	 *
	 *	lists are not modified here
	 *	evictabilty is a separate axis from mru_/mfu_ membership
	 */
	void ArcReplacer::SetEvictable(
		frame_id_t frame_id, 
		bool set_evictable
	) {
		//
	}
	
	/**
	 *	TODO(P1): Add implementation
	 *
	 *	@brief Remove an evictable frame from replacer.
	 *	This function should also decrement replacer's size if removal is successful.
	 *
	 *	Note that this is different from evicting a frame, which always remove the frame
	 *	decided by the ARC algorithm.
	 *
	 *	If Remove is called on a non-evictable frame, throw an exception or abort the
	 *	process.
	 *
	 *	If specified frame is not found, directly return from this function.
	 *
	 *	@param frame_id id of frame to be removed
	 *
	 *	algorithm:
	 *		must hold latch_
	 *			1. look up frame_id in alive_map_
	 *				- no present, then return
	 *				  it is either ghost or unknown, caller is deleting a page that is not currently in the pool
	 *			2. if !entry.evictable_, then abort/throw
	 *			   BufferPoolManager must gaurantee that the page is unpinned before deletion
	 *			3. unlink entry from current alive list mru_/mfu_
	 *			4. erase form alive_map
	 *			5. decrement curr_size_
	 *	
	 *	note:
	 *		no ghost entry is created
	 *		page is being permanently deleted, so nothing to remember about future re-reference
	 */
	void ArcReplacer::Remove(
		frame_id_t frame_id
	) {
		//
	}
	
	/**
	 *	TODO(P1): Add implementation
	 *
	 *	@brief Return replacer's size, which tracks the number of evictable frames.
	 *
	 *	@return size_t
	 *
	 *	must hold latch_ to read curr_size_ atomically with respect to other mutations
	 *	returns curr_size_
	 *	ghost entries never count
	 *	non-evictable alive entries never count
	 */
	auto ArcReplacer::Size() -> size_t { 
		return 0; 
	}
	
}  // namespace bustub
