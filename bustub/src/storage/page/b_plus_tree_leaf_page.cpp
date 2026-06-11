//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_leaf_page.cpp
//
// Identification: src/storage/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <sstream>

#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {
	/*
	 *	BPlusTreeLeafPage{}
	 *	implementations of leaf page helpers
	 *	required by project 2, task 1
	 *
	 *	task 1 scope
	 *		- Init(max_size)	- sets up a fresh leaf
	 *		- GetTombstones()	- set tombstone INDEX -> KEY dereference
	 *		- GetNextPageId()/SetNextPageId()
	 *		  sibling ptr accessors
	 *		- KeyAt(index)		- physical key access, ignores tombstones
	 *
	 * 	task 2 scope
	 * 		- Insert() and Delete()
	 * 		  must maintain sorted order
	 * 		  must maintain tombstone book-keeping
	 * 		- Split/merge/redistribute
	 * 		- GetValue()/FindKey()
	 *
	 *	implementation notes
	 *		- latching
	 *		  nothing in this files takes any latch
	 *		  caller expected to hold ReadPageGuard{}/WritePageGuard{} on the underlying frame
	 *		  the guard's rwlatch_ provides all the mutex we need here
	 *		- FULL_INDEX_TEMPLATE_ARGUMENTS prefix
	 *		  every method must be tagged with this (i.e. 4 param template declaration, no defaults)
	 *		  so that the template instantion actually finds matching declaration (@see $bottom of file)
	 *		  forgetting it can cause linking errors
	 *		- per-instantiation slot counts
	 *		  LEAF_PAGE_SLOT_CNT is a macro that expands to a different integer for every template instantiation
	 *		  different KeyType/NumTombs combos
	 *		  *EVERY* method below implicitly closes over the right value for the right type
	 *
	 *	template instantiations:
	 *	@see bottom of file
	 *	the block of ```template class BPlusTreeLeafPage<...>``` lines at the end of the file forces the compiler to emit symbols for each
	 *	(i.e. Key, Value, Cmp, NumTombs)
	 *	this is the combination that the rest of the codebase needs
	 *	without these explicit instantiations the linker would error out
	 *	templates in *.cpp do not auto-instantiate from another translation unit
	 *	
	 *	the set covers:
	 *		- GenericKey<4>		- 1 instantiation
	 *		- GenericKey<8>		- 5 instantiations
	 *					  NumTombs in {0, 3, 2, 1, -1}
	 *		- GenericKey<16>	- 1 instantiation
	 *		- GenericKey<32>	- 1 instantiation
	 *		- GenericKey<64>	- 1 instantiation
	 *
	 *	the 5 GenericKey<8> exists because the test suite stresses the tombstone buffer logic at multiple sizes (i.e. 1, 2, 3)
	 *	as well as with "no enforcement" leaderboard mode
	 *	-1, which fails back to LEAF_PAGE_DEFAULT_TOMB_CNT
	 *	the default no tombs entry (i.e. 0) keeps the classical B+Tree covered
	*/
	
	/*****************************************************************************
	 * HELPER METHODS AND UTILITIES
	 *****************************************************************************/
	
	/**
	 * @brief Init method after creating a new leaf page
	 *
	 * After creating a new leaf page from buffer pool, must call initialize method to set default values,
	 * including set page type, set current size to zero, set page id/parent id, set
	 * next page id and set max size.
	 *
	 * @param max_size Max size of the leaf node
	 *
	 * expected algorithm:
	 * 	SetPageType(IndexPageType::LEAF_PAGE)
	 * 	SetSize(0)
	 * 	SetMaxSize(max_size)
	 * 	SetNextPageId(INVALID_PAGE_ID)
	 * 	num_tombstones_ = 0
	 * 	please note the following:
	 * 		tombstones_[], key_array_[], rid_array_[] left undefined - safe 
	 * 		because size_ == 0 and num_tombstones_ == 0 gate all subsequent reads
	 *  
	 * caller contract:
	 * 	caller just called NewPage() for a new fid
	 * 	reinterpret_cast() the frame into BPlusTreeLeafPage{}
	 * 	frame bytes all zero (i.e. BPM::Reset()), so page_type_ is INVALID_INDEX_PAGE/0
	 * 	size_ is 0, max_size_ is 0, etc...
	 * 	Init() flips page_type_ to LEAF_PAGE and sets max_size_
	 * 	everything else is already what we want from the zeroing
	 * 	but we still set explicitly for clarity and robustness against future BPM behaviour changes
	 *
	 * notes:
	 * 	- max_size is the parameter SUPPLIED by BPlusTree at ctor time
	 * 	  defaults to LEAF_PAGE_SLOT_CNT via the header's default arg
	 * 	  this allows tests to deliberately shrink page capacity to force splits/merges with very few entries
	 * 	  visualizer cook book (testing suite) uses '5 5', max 5 keys for leaf/internal node)
	 * 	- *DO NOT* set parent_id
	 * 	  there is no parent ID on a leaf in this design
	 * 	  .../project_2/README.md mentions "set page id/parent id" in passing,
	 * 	  but the current BPlusTreePage{} layout has neither field
	 * 	  ignore that part of the docs then (it might be leftover from a previous design)
	*/
	FULL_INDEX_TEMPLATE_ARGUMENTS
	void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(
		int max_size
	) { 
		UNIMPLEMENTED("TODO(P2): Add implementation."); 
	}
	
	/**
	 * @brief Helper function for fetching tombstones of a page.
	 * @return The last `NumTombs` keys with pending deletes in this page in order of recency (oldest at front).
	 *
	 * expected algorithm:
	 * 	```
	 * 	std::vector<KeyType> result;
	 * 	result.reserve(num_tombstones_);
	 * 	for (size_t i = 0; i < num_tombstones_; i++) {
	 * 		result.push_back(key_array_[tombstones_[i]]);
	 * 	}
	 * 	return result;
	 * 	```
	 *
	 * why this is safe with the "page is buffer" rule:
	 * 	std::vector ctored on caller's stack, not on the page
	 * 	we never store std::vector inside a leaf page object (i.e. violating the trivial field rule)
	 * 	just hand one back by value as convenience for callers (i.e. the visualizer, and testing suite)
	 *
	 * ordering invariant:
	 * 	tombstones_[0] is the *OLDEST* pending deletion
	 * 	tombstones_[num - 1] is the *NEWEST*
	 * 	FIFO order in which buffered deletions will eventually be physically applied
	 * 	@see tombstone semantics at header files
	 *
	 * edge cases:
	 * 	- LEAF_PAGE_TOMB_CNT == 0 -> num_tombstones_ is always 0 -> return empty vector
	 * 	  no need for special case, the loop just does not run
	 * 	- a tombstone index i refers to key_array_[i]
	 * 	  that key still physically exists (but KeyAt() returns it regardless)
	 * 	  so that the de-reference is well defined
	*/
	FULL_INDEX_TEMPLATE_ARGUMENTS
	auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetTombstones() const -> std::vector<KeyType> {
		UNIMPLEMENTED("TODO(P2): Add implementation.");
	}
	
	/**
	 * Helper methods to set/get next page id
	 */
	
	/*
	 *	@brief		- read next_page_id
	 *	
	 *	expected algorithm:
	 *		return next_page_id_
	 *
	 *	INVALID_PAGE_ID/-1 is legal return value
	 *	means "this is the last leaf; iteration ends here"
	 *	IndexIterator() in project 2, task 3 uses this as its end-of-stream sentinel
	*/
	FULL_INDEX_TEMPLATE_ARGUMENTS
	auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t { 
		UNIMPLEMENTED("TODO(P2): Add implementation."); 
	}
	
	/*
	 *	@brief		- write next_page_id_
	 *
	 *	expected algorithm:
	 *		next_page_id_ = next_page_id
	 *
	 *	called from:
	 *		- split path
	 *		  when leaf L splits into L + L`
	 *		  set L`->next = L->next
	 *		  then L->next = L`
	 *		  order matters, because don't lose the original next before saving it
	 *		- merge path
	 *		  when L absorbs L`
	 *		  set L->next = L`->next
	 *		  then de-allocate L`
	 *		- tree initialization
	 *		  set to INVALID_PAGE_ID for a freshly Init() leaf
	 *		  already done by Init()
	*/
	FULL_INDEX_TEMPLATE_ARGUMENTS
	void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(
		page_id_t next_page_id
	) {
		UNIMPLEMENTED("TODO(P2): Add implementation.");
	}
	
	/*
	 * Helper method to find and return the key associated with input "index" (a.k.a
	 * array offset)
	 */
	
	/*
	 *	@brief		- key at physical slot 'index'
	 *			  ignores tombstones
	 *
	 *	expected algorithm:
	 *		return key_array_[index]
	 *
	 *	caller contract:
	 *		0 <= index <= GetSize()
	 *		a reasonable defensive implementation will assert this
	 *
	 *	.../project_2/README "§Task 1" is explicit
	 *		this *MUST* return the physical key even if tombstoned
	 *		callers that need to skip tombstoned entries (i.e. IndexIterator()/GetValue()) should cross reference tombstones_[] itself
	 *
	 *	reasonable defensive implementation:
	 *		```
	 *		BUSTUB_ASSERT(index >= 0 && index < GetSize(), "KeyAt(), index OOB");
	 *		return key_array_[index]
	 *		```
	 *
	 *	cross reference:
	 *		ValueAt() equivalent for the rid_array_
	 *		not provided in KeyAt() API
	 *		RidAt(int index) mirror of this
	*/
	FULL_INDEX_TEMPLATE_ARGUMENTS
	auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(
		int index
	) const -> KeyType { 
		UNIMPLEMENTED("TODO(P2): Add implementation."); 
	}
	
	/*
	 *	explicit template instantiations
	 *	one per (Key, Value, Cmp, NumTombs) combination that the rest of the project would then use
	 *
	 *		- GenericKey<4>: smallest key width tested.
	 *		- GenericKey<8>:
	 *		  workhouse key width
	 *		  5 variants with NumTombs in {default-0, 3, 2, 1, -1} stress the tombstone buffer book-keeping at varying buffer depths
	 *		  -1 variant is the leaderboard's "no enforcement" mode that defaults to LEAF_PAGE_DEFAULT_TOMB_CNT
	 *		  currently 0, but can be tuned in the header
	 *		- GenericKey<{16,32,64}>
	 *		  large key widths for stress testing
	 *
	 *	if we need a template variant during development, then add it here
	 *	forgetting to add it produces a linker error (i.e. undefined reference to ```BPlusTreeLeafPage<...>::Method(...)```)
	 *	not a compiler error, so the error message would point at the test file
	*/
	template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;
	
	template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
	template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 3>;
	template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 2>;
	template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, 1>;
	template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>, -1>;
	
	template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;
	
	template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;
	
	template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
