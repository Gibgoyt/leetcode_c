//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_leaf_page.h
//
// Identification: src/include/storage/page/b_plus_tree_leaf_page.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "storage/page/b_plus_tree_page.h"

namespace bustub {
	/*
	 *	B+Tree leaf page
	 *	project 2, task 1
	 *	the leaf node is the bottom-level node
	 *
	 *	a leaf holds the actual indexed data of the leaf node inside out B+Tree
	 *	internal pages only exist to direct lookups to the correct leaf
	 *	leaves are where keys live alongside its record ID (i.e. rid)
	 *	@see rid.h
	 *
	 *	leaf node vs. internal node
	 *		internal:
	 *			m keys
	 *			m+1 child ptrs
	 *			key[0] is invalid
	 *		leaf:
	 *			m keys
	 *			m values (i.e.RIDs)
	 *			*EVERY* key slot is valid
	 *
	 *	Spring of 2026 has a novel addition:
	 *		TOMBSTONE_BUFFER (i.e. buffered deletions)
	 *		.../project_2/README.md describes as "simplified Bε-tree"
	 *		deletion does *NOT* immediately remove the key/rid from arrays
	 *		instead, the index of the deleted antry is appended to a fixed size FIFO buffer
	 *			i.e. ```tombstones_[0..num_tombstones_-1]```
	 *		only when the buffer overflows, an attempted (k+1)th deletion, then the *OLDEST* 
	 *		buffered deletion applied to key/value arrays
	 *		see "§Tombstone" semantics below
	 *
	 *	page is the buffer:
	 *		same as the overlay-on-raw-bytes pattern as the base class
	 *		@see b_plus_tree_page.h rationale
	 *		deleted ctor/dtor, manual Init(), all fields must be trivially constructible
	 *			i.e. this is why num_tombstones_ and tombstones_[] are size_t/size_t[N] instead of std::vector<size_t>
	 *
	 *	layout summary:
	 *	TODO!!: proper rectangular docs here please
	 *		offset 0	: BPlusTreePage header	(12 bytes)
	 *		offset 12	: next_page_id		(4 bytes)
	 *		offset 16	: num_tombstones_	(8 bytes, i.e. sizeof(size_t))
	 *		offset 24	: tombstones_[]		(8 bytes * LEAF_PAGE_TOMB_CNT)
	 *		varying offset	: keys_array_[]		(sizeof(KeyType) * LEAF_PAGE_SLOT_CNT)
	 *		varying offset	: rid	_array_[]	(sizeof(ValueType) * LEAF_PAGE_SLOT_CNT)
	 *
	 *	LEAF_PAGE_HEADER_SIZE = 16
	 *		in the macro below counts *ONLY* BPlusTreePage{} header + next_page_id_
	 *		*NOT* num_tombstones_/tombstones_[]
	 *		slot count formula subtracts those *SEPARATELY*
	 *		**BEWARE OF THIS ASYMETRY!**
	 *
	 *	Concurrency:
	 *		the leaf page is accessed through ReadPageGuard{}/WritePageGuard{},
	 *		this is the only way to touch leaf page bytes
	 *		inside a guard's lifetime, no other thread can mutate
	 *			i.e. Read() holds shared, Write() holds exclusive
	 *		all the methods below assume that the caller already holds the right guard
	 *	
	 *	Tombstone semantics
	 *		
	 *		```
	 *		k := LEAF_PAGE_TOMB_CNT
	 *		```
	 *		compile-time per template instantiation
	 *
	 *		insertion of key K:
	 *			1. if K exists as a tombstoned entry
	 *			   clear the tombstone
	 *			   the entry "resurrects"
	 *			2. else, insert into key_array_/rid_array_ at the sorted position
	 *
	 *		deletion of key K:
	 *			1. if K exists as a tombstoned entry
	 *			   clear the tombstone
	 *			1. find index i, where key_array_[i] == K
	 *			2. if k > 0
	 *				- if num_tombstones_ < 0
	 *				  tombstones_[num_tombstones_++] = i
	 *				- else
	 *				  i.e. buffer full
	 *					1. physically remove entry at tombstones_[0]
	 *					   i.e. the oldest pending deletion
	 *					2. shift every key_array_[]/rid_array_[] slot above the index down by 1
	 *					   i.e. shift left
	 *					   decrement size_
	 *					3. any tombstones_[j] > tombstones_[0], also shift left by 1
	 *					   i.e. its target indices moved
	 *					4. FIFO shift tombstones_[1..(k-1)] -> tombstones_[0..(k-2)]
	 *					5. tombstones_[k-1] = new index of k, after step 2
	 *			3. if k == 0
	 *			   physically remove key/rid immediately 
	 *			   no buffering
	 *
	 *	what about merge/redistribute
	 *	(i.e. project 2, task 2)
	 *		.../project_2/README.md is explicit
	 *		"When a leaf is coalesced, or redistributed into another leaf, then consider all of its pending deletions
	 *		to be more recent than any pending deletion in the recipient leaf"
	 *			the node with entries being inserted into it should have its tombstones processed first
	 *
	 *		practical interpration:
	 *			- ```source.tombstones_[]``` FIFO appended after *AFTER* ```dest.tombstones_[]```
	 *			- if the combined count exceeds k
	 *			  *EXCESS* oldest entries (which are dests) get physically applied first
	 *			  in FIFO order until the buffer fits
	 *			- indices in the appended source-tombstones must be *SHIFTED* to account to for dest existing entries
	 *			  their target slots now at index dest.size + source_local_index
	 *
	 *	read-side rules (project 2, task 3)
	 *		- IndexIterator must *SKIP* any key/rid whose physical index appears in tombstones_[0..(num_tombstones-1)]
	 *		- GetTombstones() returns coresponding *KEYS* (not indices)
	 *		  in oldest first order
	 *		  used for tests + visualization
	*/

	/*
	 *	@brief		- typedef templated for short-hand leaf-page type
	 *
	 *	used in *.cpp to write B_PLUS_TREE_PAGE_TYPE::Init()
	 *	instead of BPlusTreePage<KeyType, ValueType, KeyComparator, NumTombs>::Init() for every method definition
	 *	pairs with FULL_INDEX_TEMPLATE_ARGUMENTS above each *.cpp method
	*/
	#define B_PLUS_TREE_LEAF_PAGE_TYPE BPlusTreeLeafPage<KeyType, ValueType, KeyComparator, NumTombs>

	/*
	 *	@brief		- bytes accounted in the "fixed header" portion of the leaf page
	 *
	 *	breakdown (16 bytes)
	 *		- 12 bytes from BPlusTreePage
	 *		  page_type_, size_, max_size_
	 *		- 4 bytes for next_page_id_
	 *
	 *	Note:
	 *		this does *NOT* include num_tombstones_ (8 bytes)
	 *		or tombstones_[] (8 * LEAF_PAGE_TOMB_COUNT bytes)
	 *		LEAF_PAGE_SLOT_CNT macro here subtracts those explicitly on top of LEAF_PAGE_HEADER_SIZE
	 *		do not conflate/confused "header size", with "bytes consumed before the key array"
	*/
	#define LEAF_PAGE_HEADER_SIZE 16

	/*
	 *	@brief		- the tombstone buffer capacity when NumTombs is specified
	 *
	 *	Zero by default:
	 *		a fresh template instantiation with no NumTombs argument
	 *		i.e. thanks to FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN having NumTombs = 0
	 *		has *NO* tombstone buffer at all - deletions are applied immediately
	 *		matches classic B+Tree
	 *
	 *	tuning knob for the leaderboard:
	 *		.../project_2/README.d "§Leaderboard Task"
	 *		the leaderboard uses NumTombs == -1, triggers fallback to LEAF_PAGE_TOMB_CNT below to use this default
	 *		override LEAF_PAGE_DEFAULT_TOMB_CNT if we want our leaderboard runs to use non-zero buffer (e.g. 4, 8, 16)
	 *		without changing explicit template instantiations
	*/
	#define LEAF_PAGE_DEFAULT_TOMB_CNT 0
	
	/*
	 *	@brief		- the actual tombstone capacity used by *THIS* instantiation
	 *
	 *	conditions:
	 *		- NumTombs < 0	- use LEAF_PAGE_DEFAULT_TOMB_CNT 
	 *				  i.e. the leaderboard's "no enforcement, you pick" mode
	 *		- NumTombs >= 0	- use NumTombs directly
	 *
	 *	this is a compile-time constant per template instantiation
	 *	ends up baked into the sizes of tombstones_[]/key_array_[] for that particular BPlusTreeLeafPage<> type
	*/
	#define LEAF_PAGE_TOMB_CNT ((NumTombs < 0) ? LEAF_PAGE_DEFAULT_TOMB_CNT : NumTombs)
	
	/*
	 *	@brief		- max no. of key/rid slots on a leaf page
	 *
	 *	derivation
	 *	(per template instantiation, evaluated at compile time)
	 *		BUSTUB_PAGE_SIZE	- total bytes available
	 *					  e.g. 8192 bytes
	 *		LEAF_PAGE_HEADER_SIZE	- base header + next_page_id_
	 *					  16 bytes
	 *		sizeof(size_t)		- num_tombstones_ field
	 *					  8 bytes on x86/arm 64
	 *		LEAF_PAGE_TOMB_CNT * sizeof(size_t)	- tombstones_[]
	 *		------------
	 *		remaining bytes for key_array_[]/rid_array_[]
	 *
	 *	each slot takes up sizeof(KeyType) + sizeof(ValueType) bytes
	 *	so slot count = (remaining bytes) / (per-slot size)
	 *
	 *	example:
	 *		<GenericKey<8>, RID, Comparator<8>, 0>
	 *		(8192 - 16 - 8 - 0) / (8 + 8)
	 *		= 8168 / 16
	 *		= 510 slots
	 *
	 *		OR
	 *
	 *		NumTombs = 3
	 *		( 8192 - 16 - 8 - 3(8)) / (8 + 8)
	 *		= 8144 / 16
	 *		= 509 slots
	 *		adding tombstone slots eats key/value capacity
	 *	
	 *	internal page comparison:
	 *		INTERNAL_PAGE_SLOT_CNT = ( 8192 - 12 ) / 16 = 511
	 *		internal pages have 1 more slot because they do not carry next_page_id_, num_tombstones_, or tombstones_[]
	*/
	#define LEAF_PAGE_SLOT_CNT                                                                               \
			((BUSTUB_PAGE_SIZE - LEAF_PAGE_HEADER_SIZE - sizeof(size_t) - (LEAF_PAGE_TOMB_CNT * sizeof(size_t))) / \
			 (sizeof(KeyType) + sizeof(ValueType)))  // NOLINT
	
	/**
	 *	Store indexed key and record id(record id = page id combined with slot id,
	 *	see include/common/rid.h for detailed implementation) together within leaf
	 *	page. Only support unique key.
		*
	 *	Leaf pages also contain a fixed buffer of "tombstone" indexes for entries
	 *	that have been deleted.
		*
	 *	Leaf page format (keys are stored in order, tomb order is up to you):
	 *	 --------------------
	 *	| HEADER | TOMB_SIZE | (where TOMB_SIZE is num_tombstones_)
	 *	 --------------------
	 *	 -----------------------------------
	 *	| TOMB(0) | TOMB(1) | ... | TOMB(k) |
	 *	 -----------------------------------
	 *	 ---------------------------------
	 *	| KEY(1) | KEY(2) | ... | KEY(n) |
	 *	 ---------------------------------
	 *	 ---------------------------------
	 *	| RID(1) | RID(2) | ... | RID(n) |
	 *	 ---------------------------------
		*
	 *	 Header format (size in byte, 16 bytes in total):
	 *	 -----------------------------------------------
	 *	| PageType (4) | CurrentSize (4) | MaxSize (4) |
	 *	 -----------------------------------------------
	 *	 -----------------
	 *	| NextPageId (4) |
	 *	 -----------------
	 *
	 *	 cross-reference with file-level rationale (at the top of current file)
	 *	 	- HEADER above
	 *	 	  BPlusTreePage{} 12 bytes (page_type_, size_, max_size_)
	 *	 	  + next_page_id_ 4 bytes
	 *	 	  = LEAF_PAGE_HEADER_SIZE 16 bytes
	 *	 	- TOMB_SIZE
	 *	 	  num_tombstones_ 8 bytes (i.e. sizeof(size_t))
	 *	 	- TOMB(0..k) = tombstones_[]
	 *	 	  k * 8 bytes
	 *	 	  k = LEAF_PAGE_TOMB_CNT
	 *	 	- n = curent_size_
	 *	 	  the arrays have capacity = LEAF_PAGE_SLOT_CNT
	 *	 	  but only the first 'n' entries are valid (the rest is not yet written to)
	 *
	 *	 unique key invariant:
	 *	 	at .../project_2/README.md "§Task 2" states that unique keys are requirex
	 *	 	the base contract on this leaf being:
	 *	 		at most 1 slot per key
	 *	 		when a key is deleted via tombstone, its slot is still occupied physically
	 *	 		re-inserting same key resurrects tombstoned slot rather than adding a duplicaet
	 *
	 *	 sibling linkage:
	 *	 	next_page_id_ points to the leaf to the right in the singley linked list of leaves
	 *	 	no prev pointer
	 *	 	enough to support IndexIterator left-to-right scan (i.e. project 2, task 3)
	 *	 	.../project_2/README.md's "§Task 3" calls this out as the reason for sibling pointers at all
	*/
	FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN
	class BPlusTreeLeafPage : public BPlusTreePage {
		public:
			// Delete all constructor / destructor to ensure memory safety
			BPlusTreeLeafPage() = delete;
			BPlusTreeLeafPage(const BPlusTreeLeafPage &other) = delete;
	
			/*
			 *	@brief		- manual ctor
			 *			  call exactly once per page lifetime
			 *
			 *	@param max_size	- leaf page capacity
			 *			  deafaults to LEAF_PAGE_SLOT_CNT
			 *			  i.e. "use as much as the page can fit"
			 *
			 *	called by B+Tree in project 2, task 2
			 *	immediately after fetcing a new page from BPM and reinterpret_cast()ing it, setting:
			 *		- page_type_	= LEAF_PAGE	(via SetPageType())
			 *		- size_		= 0 		(via SetSize())
			 *		- max_size_	= max_size	(via SetMaxSize())
			 *		- next_page_id_	= INVALID_PAGE_ID	(no sibling yet)
			 *		- num_tombstones_	= 0	(no buffered deletions yet)
			 *
			 *	does *NOT* touch tombstones_[], key_array_[], rid_array_[]
			 *	since their contents are undefined until the corresponding indices are written
			*/
			void Init(
				int max_size = LEAF_PAGE_SLOT_CNT
			);
	
			/*
			 *	@brief		- the keys of the currently-tombstones entries
			 *			  oldest first
			 *
			 *
			 *	@return		- vector of num_tombstones_ KeyType values
			 *			  in FIFO order
			 *			  oldest at the front, newest at the back
			 *
			 *	tombstones store indices noy keys
			 *		this getter dereferences through the indices to return the keys
			 *		this is what our tests and the visualize want
			 *
			 *	important:
			 *		this is the *ONLY* page in the leaf API where it is okay to return std::vector
			 *			- the function calls by *VALUE*
			 *			  the vector lives on the caller's stack/heap, and *NOT* in the page
			 *			- no std::vector field is ever stored in the page
			 *			  this would violate "page is buffer" rule
			 *
			 *	used by:
			 *		- ToString()
			 *		  visualizer renders these keys on the 3rd row of LeafNode{} boxes
			 *		  @see .../project_2/README.md "§Tree Visualization" section
			 *		- IndexIterator()
			 *		  for project 2, task 3
			 *		  only by reading tombstones_[] directly, not via GetTombstones()
			 *		- tests verifying tombstones book-keeping
			*/
			auto GetTombstones() const -> std::vector<KeyType>;
	
			// Helper methods
			
			/*
			 *	@brief		- page_id_t of the right sibling
			 *			  or INVALID_PAGE_ID if none
			 *
			 *	singley linked
			 *	INVALID_PAGE_ID (-1) means "this is the rightmost leaf of the iteration"
			 *	used by IndexIterator() (i.e. project 2, task 3)
			 *		to advance from one leaf to the next during in-order scan
			*/
			auto GetNextPageId() const -> page_id_t;

			/*
			 *	@brief		- write next_page_id_
			 *
			 *	called during:
			 *		- insert path on split
			 *		  the new right sibling adopts the leaf's next_page_id_
			 *		  the old leaf's next_page_id_ is updated to point to the new sibling
			 *		- Delete path on merge
			 *		  surviving leaf adopts "merged away" leaf's next_page_id_
			*/
			void SetNextPageId(
				page_id_t next_page_id
			);
			
			/*
			 *	@brief		- physical key at slot 'index'
			 *			  IGNORES tombstones
			 *
			 *	@param index	- 0-based slot index into key_array_
			 *			  0 <= index < GetSize()
			 *
			 *	@return		- KeyType at that slot
			 *
			 *	Project 2, Task 1:
			 *		KeyAt() must return physical entry at a given index regardless of whethere tombstone exists for that entry
			 *		i.e. if a tombstone currently records that index 'i' is pending deletion
			 *		     KeyAt(i) still returns the original key
			 *		     the entry is logically deleted but physically present
			 *
			 *	callers that want to skip tombstones entries must cross reference tombstones_[] themselves
			 *	IndexIterator() does this
			*/
			auto KeyAt(
				int index
			) const -> KeyType;
	
			/*
			 *	@brief		- for the test only return a string representing all keys
			 *			  in this leaf page formatted as "(tombkey1,tombkey2...|key1,key2,...)"
			 *
			 *	@return		- std::string
			 *
			 *	format:
			 *		( tomb_key_0 , tomb_key_1 , ... | key_0 , key_1 , key_2 , ... )
			 *
			 *	the pipe '|' separates tombstoned keys from physical keys
			 *	right side includes tombstoned keys too since KeyAt() does not skip them
			 *	used by visualizer and unit tests
			 *
			 *	implementation note:
			 *		this method is defined inline in the header	@see b_plus_tree_leaf_page.h
			 *		so it works without a corresponding entry at b_plus_tree_leaf_page.cpp
			 *		so it is available right now, as it is not implemented at *.cpp
			*/
			auto ToString() const -> std::string {
				std::string kstr = "(";
				bool first = true;
	
				auto tombs = GetTombstones();
				for (size_t i = 0; i < tombs.size(); i++) {
					kstr.append(std::to_string(tombs[i].ToString()));
					if ((i + 1) < tombs.size()) {
						kstr.append(",");
					}
				}
	
				kstr.append("|");
	
				for (int i = 0; i < GetSize(); i++) {
					KeyType key = KeyAt(i);
					if (first) {
						first = false;
					} else {
						kstr.append(",");
					}
	
					kstr.append(std::to_string(key.ToString()));
				}
				kstr.append(")");
	
				return kstr;
			}
	
		private:
			/*
			 *	@brief		- page_id of the next sibling leaf
			 *			  or INVALID_PAGE_ID
			 *
			 *	part of the leaf page header (offset 12, 4 bytes)
			 *	maintained by GetNextPageId(), SetNextPageId()
			 *	treated as opaque by everything except split-merge code in IndexIterator
			*/
			page_id_t next_page_id_;
			
			/*
			 *	@brief		- no. of currently buffered deletions
			 *			  0 <= num <= LEAF_PAGE_TOMB_CNT
			 *
			 *	size_t (8 bytes on 64) for aligntment/consistenct with tombstones_[]
			 *	when num_tombstones_== LEAF_PAGE_TOMB_CNT, the buffer is still full
			 *	the next deletion forces "apply oldest" operation 
			 *	@see "§Tombstone" notes in this file-level rationale
			*/
			size_t num_tombstones_;

			/*
			 *	@brief		- FIFO ring of `slot` indices
			 *			  e.g. size_ - 1 pending deletion
			 *
			 *	tombstones_[0] is the oldest pending deletion
			 *	tombstones_[num-1] is the newest
			 *
			 *	each value being an index into key_array_/rid_array_
			 *	but is not the key, it is the indice
			 *	cross-reference via key_array_[tombstones_[i]] when we need the actual key
			 *	
			 *	Fixed-size tombstone buffer (indexes into key_array_ / rid_array_).
			*/
			size_t tombstones_[LEAF_PAGE_TOMB_CNT];

			/*
			 *	@brief		- sorted array of indexed keys
			 *	
			 *	slots 0..(slots_ - 1) are tombstoned (i.e. regardless of whether its tombstoned)
			 *	slots size_..(LEAF_PAGE_SLOT_CNT - 1) contains garbage from prior uses of this physical frame
			 *	never read bytes past size_
			 *
			 *	keys kept in ascending sorted order at all times (i.e. under supplied KeyCamparator)
			 *	project 3's README "§Requirements and Hints" recommends binary search for both lookups and insertion position
			 *		it's a correctness-adjacent perf requirement (else timeout)
			 *
			 *	project 3's README "§Common Pitfalls" section
			 *		does not modify size/type of this array
			 *
			 *	auto grader depends on the exact byte order
			 *
			 *	Array members for page data.
			*/
			KeyType key_array_[LEAF_PAGE_SLOT_CNT];

			/*
			 *	@brief		- parallel array of RIDs corresponding to key_array_
			 *
			 *	rid_array_[i] is the rid for the row indexed by key_array_[i]
			 *	a rid is (page_id_t, slot_num), 8 bytes on 64
			 *	@see rid.h
			 *
			 *	same length/validiity/no-modifcation rules as key_array_
			*/
			ValueType rid_array_[LEAF_PAGE_SLOT_CNT];

			// (Spring 2025) Feel free to add more fields and helper functions below if needed
	};
	
}  // namespace bustub
