//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_internal_page.h
//
// Identification: src/include/storage/page/b_plus_tree_internal_page.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <queue>
#include <string>

#include "storage/page/b_plus_tree_page.h"

namespace bustub {
	/*
	 *	B+Tree internal page
	 *	project 2, task 1 (inner node of the B+tree)
	 *
	 *	an internal page is an inner node of a B+Tree
	 *	contains NO indexed data of its own
	 *	its only job is to route lookups, inserts, and deletes, from the root towards the correct leaf
	 *	internal pages are a pure navigational infrastructure
	 * 	
	 * 	internal vs. leaf 
	 * 		- internal
	 * 		  m keys
	 * 		  m+1 child ptrs
	 * 		  key[0] is INVALID/-1 (it is a placeholder)
	 * 		  ValueType is page_id_t
	 * 		- leaf
	 * 		  m keys
	 * 		  m values (RIDs)
	 * 		  *EVERY* key slot is valid
	 * 		  ValueType is rid
	 * 		  carries next_page_id_ + tombstones_[]
	 *
	 * 	off-by-one layout
	 * 		an internal page logically stores n keys, n+1 child ptrs
	 * 		BUSTUB crams both arrays into the same INTERNAL_PAGE_SLOT_CNT sized layout
	 * 		so size_ counts slots, each slot being key/page_id pair
	 * 		the trick
	 * 			slot index 0's key into a sentinel that is NEVER read by lookup code
	 *
	 * 		slot index:	0		1		2		...	n-1
	 * 		kay_array_:	INVALID		K(1)		K(2)		...	K(n-1)
	 * 		page_id_array_:	P(0)		P(1)		P(2)		...	P(n-1)
	 *
	 * 	routing invariant:
	 * 		for any key, X, to be inserted or looked up, find the largest i in [1, size_)
	 * 		such that K(i) <= X
	 * 		child to descend into is page_id_array_[i]
	 * 		if no such i exists (i.e. X < K(1)), then descend into page_id_array_[0]
	 * 	
	 * 	equivalent phrasing
	 * 		.../project_2/README.md "§Internal Page" section:
	 * 		"Pointer PAGE_ID(i) points to a subtree in which all keys K satisfy K(i) <= K < K(i+1)."
	 * 		here, PAGE_ID(i) lives at page_id_array_[i] and (i, i+1) range over [0, size_)
	 * 		K(0) is conceptually -infinity, while K(size_) is conceptually +infinity
	 *
	 * 	so size_ in an internal page is no. of child ptrs (i.e. no. of real keys + 1)
	 * 	"n keys, n+1 pointers" maps to our "size_ slots, of which index 0 is invalid"
	 *
	 * 	"page is the buffer"
	 * 		the same overlay onto raw bytes pattern as the base BPlusTreePage{} class
	 * 		deleted ctor/dtor
	 * 		manual Init()
	 * 		all fields must be trivially constructible
	 * 	
	 * 	no tombstones:
	 * 		internal pages *DO NOT* have tombstone buffer
	 * 		deletion of routing entries (i.e. happen during leaf merge and propogate upward) are applied immediately, no buffering, no FIFO
	 * 		hence the simpler template parameter list (INDEX_TEMPLATE_ARUMENTS, 3 params, no NumTombs)
	 * 		vs. the leaf's parameter list (FULL_INDEX_TEMPLATE_ARGUMENTS, 4 params)
	 *
	 * 	layou summary:
	 * 		offset 0	: BPlusTreePage{} header	(12 bytes)
	 * 		offset 12	: key_array_[]			(sizeof(KeyType) * INTERNAL_PAGE_SLOT_CNT)
	 * 		offset ...	: page_id_array_[]		(sizeof(ValueType) * INTERNAL_PAGE_SLOT_CNT)
	 *
	 * 		ValueType for BPlusTreeInternalPage{} is always page_id_t
	 * 		(@see explicit template instantiotions bottom b_plus_tree_internal_page.cpp)
	 * 		.../project_2/README.md "§Note" on leaf vs. internal max_size_
	 * 			highlights leaf must carry rid (8 byte)
	 * 			while internal carries page_id_t (4 bytes)
	 * 			so internal slots are typically larger per key than leaf slots (i.e. more key, less value)
	 * 			only if KeyType is small
	 * 			so internal slots are typically larger per key than leaf slots (i.e. more key, less value)
	 * 		in common case, GenericKey<8>:
	 * 			leaf	- 8 for key + 8 for value = 16 bytes
	 * 			internal	- 8 for key, 4 for value = 12 bytes
	 * 			internal page fits more slots than key page on the same key width
	 * 	concurrency:
	 * 		accessed through ReadPageGuard{}/WritePageGuard{} like every other B+Tree page
	 * 		all methods here assume that the caller already holds the appropriate guard
	 * 		the B+Tree latch-crabbing scheme (project 2, task 4) determines when to release internal page guards as descending towards the leaf
	 *
	 * 	half-full rule
	 * 		N.B.!! read this before starting with project 2, task 2 
	 * 		internal pages MUST satisfy size_ >= GetMinSize() at all times, *EXCEPT* for root
	 * 		from .../project_2/README.md "each internal page should be at least half full"
	 * 		when an insertion would push size_ > max_size_, then split or redistribute
	 * 		when a deletion would pull size_ < MinSize, then merge or redistribute
	 * 		same rules as for leaf pages
	 *
	 * 	what internal pages do *NOT* have
	 * 		- no next_page_id
	 * 		  no sibling chaining at the internal level
	 * 		- num_tombstones_/tombstones_[]
	 * 		- values array distinct from routing pointer array
	 * 		  page_id_[] is the value array
	 * 		  it is just named for what it holds
	 * 		- a parent_id_ field
	 * 		  i.e. parent tracking is done via Context{} class during a tree traversal, not stored persistently
	*/
	
	/*
	 *	@brief		- typedef shorthand for templated internal page type
	 *
	 *	used in b_plus_tree_internal_page.cpp to write ```B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init()```
	 *	instead of the full template spelled form
	 *	pairs with INDEX_TEMPLATE_ARGUMENTS above each method at *.cpp file
	*/
	#define B_PLUS_TREE_INTERNAL_PAGE_TYPE BPlusTreeInternalPage<KeyType, ValueType, KeyComparator>

	/*
	 *	@brief		- bytes consumed by the fixed header before key_array_ begins
	 *
	 *	breakdown:
	 *		12 bytes from BPlusTreePage
	 *		page_type_, size_, max_size_
	 *
	 *	that is the whole entire header on an internal page
	 *	no next_page_id_, no tombstone metadata
	 *	unlike LEAF_PAGE_HEADER_SIZE, this is not misleading:
	 *		slot count formula below subtracts this, then divides
	*/
	#define INTERNAL_PAGE_HEADER_SIZE 12
	
	/*
	 *	@brief		- max no. of key/pageId slots on an internal page
	 *
	 *	derivation, per template instantion derived at compile time
	 *		BUSTUB_PAGE_SIZE = 8192
	 *		- INTERNAL_PAGE_HEADER_SIZE = 12
	 *		--------
	 *		8180 bytes for key_array_[]/page_id_array_[]
	 *
	 *	example for <GenericKey<8>, page_id_t, GenericComparator<8>>:
	 *		(8192 - 12) / (8 + 4) = 8180 / 12 = 681 slots.
	 *
	 *	compared to LEAF_PAGE_SLOT_CNT with default NumTombs
	 *		- leaf		(8192 - 16 - 8 - 0) / (8 + 8) = 8168 / 16 = 510 slots
	 *		- internal	(8192 - 12)         / (8 + 4) = 8180 / 12 = 681 slots
	 *		Internal pages fit ~33% more slots in this configuration because their values are 4B (page_id_t) vs. the leaf's 8B (RID), 
	 *		AND they don't carry the leaf-only header bytes (next_page_id_, tombstones)
	 *
	 *	The `(int)` cast on the divisor is there to make the macro safe under ```-Werror=sign-compare```, without it, sizeof returns size_t (unsigned)
	 *	and the BUSTUB_PAGE_SIZE - INTERNAL_PAGE_HEADER_SIZE numerator could get implicitly promoted in surprising ways.
	*/
	#define INTERNAL_PAGE_SLOT_CNT \
		 ((BUSTUB_PAGE_SIZE - INTERNAL_PAGE_HEADER_SIZE) / ((int)(sizeof(KeyType) + sizeof(ValueType))))  // NOLINT
	
	/**
	 *	Store `n` indexed keys and `n + 1` child pointers (page_id) within internal page.
	 *	Pointer PAGE_ID(i) points to a subtree in which all keys K satisfy:
	 *	K(i) <= K < K(i+1).
	 *	NOTE: Since the number of keys does not equal to number of child pointers,
	 *	the first key in key_array_ always remains invalid. That is to say, any search / lookup
	 *	should ignore the first key.
	 *
	 *	Internal page format (keys are stored in increasing order):
	 *	 ---------
	 *	| HEADER |
	 *	 ---------
	 *	 ------------------------------------------
	 *	| KEY(1)(INVALID) | KEY(2) | ... | KEY(n) |
	 *	 ------------------------------------------
	 *	 ---------------------------------------------
	 *	| PAGE_ID(1) | PAGE_ID(2) | ... | PAGE_ID(n) |
	 *	 ---------------------------------------------
	 *
	 *	cross reference with the file-level rationale above
	 *		- HEADER = BPlusTreePage's 12B (page_type_, size_, max_size_)
	 *		- KEY(1) is the INVALID slot-0 sentinel
	 *		  Treat it as undefined bytes
	 *		- KEY(i) for i in [2, n] are the REAL routing keys
	 *		- PAGE_ID(i) for i in [1, n] are child pointers
	 *		  PAGE_ID(1) is the "below all keys" leftmost subtree
	 *		- "n" in this ASCII drawing = size_
	 *		  the count of slots (= count of  child pointers = count of real-keys + 1))
	 *
	 *	One-based vs. zero-based indexing
	 *		the .../project_2/README.md and the ASCII picture use 1-based indexing (KEY(1), PAGE_ID(1), ...)
	 *		but the actual C++ code uses 0-based array indexing
	 *		(key_array_[0], page_id_array_[0], ...)
	 *		translation from README to C++:
	 *			- README KEY(i)		<-> key_array_[i - 1]
	 *			- README PAGE_ID(i)	<-> page_id_array_[i - 1]
	 *
	 *	So "KEY(1) is invalid" means "key_array_[0] is invalid"
	 *	and "lookups start from the second key" means "iterate i from 1, not 0."
	 *	The .cpp method docs use the C++ 0-based convention.
	*/
	INDEX_TEMPLATE_ARGUMENTS
	class BPlusTreeInternalPage : public BPlusTreePage {
		public:
		 // Delete all constructor / destructor to ensure memory safety
		 BPlusTreeInternalPage() = delete;
		 BPlusTreeInternalPage(
			const BPlusTreeInternalPage &other
		) = delete;
	
		 void Init(int max_size = INTERNAL_PAGE_SLOT_CNT);
	
		 auto KeyAt(int index) const -> KeyType;
	
		 void SetKeyAt(int index, const KeyType &key);
	
		 /**
			* @param value The value to search for
			* @return The index that corresponds to the specified value
			*/
		 auto ValueIndex(const ValueType &value) const -> int;
	
		 auto ValueAt(int index) const -> ValueType;
	
		 /**
			* @brief For test only, return a string representing all keys in
			* this internal page, formatted as "(key1,key2,key3,...)"
			*
			* @return The string representation of all keys in the current internal page
			*/
		 auto ToString() const -> std::string {
			 std::string kstr = "(";
			 bool first = true;
	
			 // First key of internal page is always invalid
			 for (int i = 1; i < GetSize(); i++) {
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
		 // Array members for page data.
		 KeyType key_array_[INTERNAL_PAGE_SLOT_CNT];
		 ValueType page_id_array_[INTERNAL_PAGE_SLOT_CNT];
		 // (Spring 2025) Feel free to add more fields and helper functions below if needed
	};
	
}  // namespace bustub
