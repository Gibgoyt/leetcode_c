//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_internal_page.cpp
//
// Identification: src/storage/page/b_plus_tree_internal_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "storage/page/b_plus_tree_page.h"
#include "storage/page/b_plus_tree_internal_page.h"

namespace bustub {
	/*
	 *	BPlusTreeInternalPage{}
	 *	
	 *	project 2, task 1
	 *		- Init(max_size)	- sets up a fresh internal page
	 *		- KeyAt(index)		- routing ket accessor
	 *		- SetKeyAt()		- routing key mutatot
	 *		- ValueIndex(value)	- reverse lookup a page_id_ from slot
	 *		- ValueAt(index)	- child/page_id accessor
	 *
	 *	project 2, task 2
	 *		- insert/delete that maintains sorted key order
	 *		- split/merge/redistribute on the internal level
	 *		- look helper
	 *		  FindChild()/RoutingIndex()
	 *		  usually a binary search over key_array_[1..(size-1)] returning the slot whose ptr to follow
	 *
	 *	implementation notes:
	 *		- latching:
	 *		  nothing here takes any latches
	 *		  caller holds ...PageGuard{} on the underlying frame
	 *		  the ...PageGuard{} rwlatch_ is the only necesasry mutex
	 *		- INDEX_TEMPLATE_ARGUMENTS prefix:
	 *		  every method must be tagged with this
	 *		  i.e. 3 param template declaration, no NumTombs
	 *		  template is the shorter macro
	 *		  LeafPage{}.cpp uses FULL_INDEX_TEMPLATE_ARGUMENTS
	 *		  do not mix them up (compile error)
	 *		- "first slot has an invalid key" lives in the semantics of KeyAt()/SetKeyAt()
	 *		  storage still allocates space for the slot
	 *		  callers responsiblity to avoid index 0 lookup
	 *
	 *	template instantiations:
	 *		@see bottom of class
	 *		...InternalPage{} only has 5 instantiations
	 *		i.e. one per generic key width (4, 8, 16, 32, 64)
	 *		all use page_id_t as ValueType
	 *		no NumTombs since internal pages do not have tombstones
	 *		contrasts with ...LeafPage{}.cpp which has 9 instantiations for tombstone variants
	*/

	/*****************************************************************************
	 * HELPER METHODS AND UTILITIES
	 *****************************************************************************/
	
	/**
	 * @brief Init method after creating a new internal page.
	 *
	 * Writes the necessary header information to a newly created page,
	 * including set page type, set current size, set page id, set parent id and set max page size,
	 * must be called after the creation of a new page to make a valid BPlusTreeInternalPage.
	 *
	 * @param max_size Maximal size of the page
	 *
	 *	expected algorithm:
	 *		```
	 *		SetPageType(IndexPageType::INTERNAL_PAGE)
	 *		SetSize(0)
	 *		SetMaxSize(max_size)
	 *		```
	 *		key_array_[]/page_id_array_[] left undefined
	 *		this is still safe because size == 0 gates all subsequent reads
	 *
	 *	caller contract/responsibility
	 *		- caller just NewPage() a new frame and called reinterpret_cast() to *BPlustTreeInternalPage{}
	 *		- frame bytes are all zeroed 
	 *		  BPM::Reset()
	 *		  page_type_ is already INVALID_INDEX_PAGE/0, size_/max_size_ also 0
	 *		  Init() flips page_type_ to INTERNAL_PAGE and sets max_size_
	 *		  everything else is fine, it is already what we want
	 *		  we will set them explicitly for clarity/robustness against future BPM behavioural changes
	 *
	 *	Notes:
	 *		- max_size supplied by BPlusTree at ctor
	 *		  defaults to INTERNAL_PAGE_SLOT_CNT via header's default arg
	 *		  tests use small caps (e.g. 5), forcing split/merges at low occupancy
	 *		- *DO NOT* set parent_id
	 *		  there is no parent_id field on internal page type
	 *		- *DO NOT_ set page_id
	 *		  page_id owned by BPM (i.e. 'page_id' we passed to NewPage())
	 *		  B+Tree pages do *NOT* store its own page_id internally
	 */
	INDEX_TEMPLATE_ARGUMENTS
	void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(
		int max_size
	) { 
		UNIMPLEMENTED("TODO(P2): Add implementation."); 
	}
	
	/**
	 * @brief Helper method to get/set the key associated with input "index"(a.k.a
	 * array offset).
	 *
	 * @param index The index of the key to get. Index must be non-zero.
	 * @return Key at index
	 *
	 *	expected algorithm:
	 *		return key_array_[index]
	 *
	 *	caller contract/responsiblity:
	 *		- 0 < index < GetSize()
	 *		  STRICTLY > 0
	 *		- a reasonable defensive implementation asserts both bounds
	 *		  ```
	 *		  BUSTUB_ASSERT(index > 0 && index < GetSize(), "internal page, KeyAt(). index must be in (0,size)")
	 *		  ```
	 *
	 *	project 2 README states "index must be non-zero", matching this
	 *	index = 0 is *EXPLICITLY DISALLOWED*
	 */
	INDEX_TEMPLATE_ARGUMENTS
	auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(
		int index
	) const -> KeyType {
		UNIMPLEMENTED("TODO(P2): Add implementation.");
	}
	
	/**
	 * @brief Set key at the specified index.
	 *
	 * @param index The index of the key to set. Index must be non-zero.
	 * @param key The new value for key
	 *
	 *	expected algorithm:
	 *		```
	 *		key_array_[index = key;
	 *		```
	 *
	 *	caller's contract/responsibility:
	 *		- 0 < index < GetSize()
	 *		  STRICTLY > 0
	 *		- caller maintains sorted key invariants across the array
	 *		  writing a single key via SetKeyAt() without considering can produce inconsistent page
	 *		  used heavily 	by split/merge code, which is responsible for surrounding sortedness work
	 *
	 *	reasonable defensive implementation
	 *		```
	 *		BUSTUB_ASSERT(index > 0 && index < GetSize(), "internal page. SetKeyAt(). index must be in (0, size)")
	 *		key_array_[index] = key
	 *		```
	 */
	INDEX_TEMPLATE_ARGUMENTS
	void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(
		int index, 
		const KeyType &key
	) {
		UNIMPLEMENTED("TODO(P2): Add implementation.");
	}
	
	/*
	 *	@brief		- reverse lookup
	 *			  finds the slot index whose page_id_ matches 'value'
	 * 	@param value	- page_id_t to search for
	 * 	@return		- slot index in [0, size_)
	 * 			  page_id_array_[i] == value/-1
	 * 			  or size_ if no slot holds that value, per our convention that is yet to be implemented
	 *
	 * 	expected algorithm:
	 * 		```
	 * 		for (int i = 0; i < GetSize(); i++) {
	 * 			if (page_id_array_[i] == value) return i;
	 * 		}
	 * 		return -1;	// or something else??
	 * 		```
	 * 		if not found, depending on our implemented sentinel choice, do not have to return -1
	 * 	
	 * 	caller contract/responsibility:
	 * 		- value is presumed to actuall be present
	 * 		  typical use: "I am child, page_id X, find my position in my parent's ptr array"
	 * 		  a defensive implementation can BUSTUB_ASSERT() result is in [0, size_) at call site
	 *
	 *
	 * 	performance:
	 * 		O(size_)
	 * 		page_id NOT sorted 
	 * 		only keys are sorted
	 * 		binary search not applicable
	 *
	 * 	used by:
	 * 		- sibling finding during redistribution/merge (project 2, task 2)
	 * 		  "I am a child at slot i in my parent, left sibling is slot (i-1), right sibling is (i+1)"
	 * 		- internal page deletion when a leaf below is remove
	 * 		  find matching slot in the parent's page_id_array_[]
	 * 		  shift everything to the right of matching slot, down by 1
	*/
	INDEX_TEMPLATE_ARGUMENTS
		auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueIndex(
			const ValueType &value
		) -> const int {
			UNIMPLEMENTED("TODO(P2): Add implementation.");
		}
	/**
	 * @brief Helper method to get the value associated with input "index"(a.k.a array
	 * offset)
	 *
	 * @param index The index of the value to get.
	 * @return Value at index
	 *
	 *	expected algorithm:
	 *		```
	 *		return page_id_array_[index]
	 *		```
	 *
	 *	caller contract/responsibility:
	 *		- 0 <= index < GetSize()
	 *		- *UNLIKE* KeyAt(), index == 0 is 100% valid here
	 *		  page_id_array_[0] is the left-most child ptr
	 *		  subtree containing keys strictly less than KeyAt(1)
	 *		  reading this value is the first thing tree descent does, for any key X, that lands below first routing key
	 *
	 *	reasonable defensive implementation:
	 *		```
	 *		BUSTUB_ASSERT(index >= 0 && index < GetSize(), "internal page. ValueAt(), 'index' OOR")
	 *		return page_id_array_index_[index]
	 *		```
	*/
	INDEX_TEMPLATE_ARGUMENTS
	auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(
		int index
	) const -> ValueType {
		UNIMPLEMENTED("TODO(P2): Add implementation.");
	}
	
	/*
	 *	explicit template instantiations
	 *	one per GenericKey width
	 *
	 *	Why these specific widths???
	 *		- GenericKey<4>	- smallest key width tested
	 *		- GenericKey<8>	- workhorse key width 
	 *				  (shared with leaf tests)
	 *		- GenericKey<16>, <32>, <64>
	 *		  stress wider keys
	 *		  forces fewer slots per page -> slots per page -> earlier splits, more eviction pressure
	 *
	 *	all 5 have page_id_t as ValueType
	 *	no other ValueType for internal pages in this design
	 *	also no NumTombs variant since internal pages do not carry tombstones
	 *
	 *	if we add a few instantions while developing
	 *	add it here
	 *	otherwise LINKER error during runtime after successful compile
	 *	```undefined reference to `BPlusTreeInternalPage<...>::Method(...)```
	*/

	// valuetype for internalNode should be page id_t
	template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
	template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
	template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
	template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
	template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
