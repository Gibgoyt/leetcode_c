//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_page.cpp
//
// Identification: src/storage/page/b_plus_tree_page.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/page/b_plus_tree_page.h"

namespace bustub {
	/*
	 *	b_plus_tree_page.cpp
	 *	implementation of BPlusTreePage{} shared/inherited by BPlusTreeLeafPage{}/BPlusTreeInternalPage{}
	 *
	 *	every method here is a one-liner over a private field
	 *
	 *	implementation notes:
	 *		- all methods touch only the 12 Byte header
	 *		  page_type_, size_, max_size_
	 *		  never read/write derived classes arrays
	 *		  hence, safe to call on a BPlusTreePage* before knowing if leaf/internal page
	 *		- no latching needed inside these methods
	 *		  caller expected to hold rwlatch_ (i.e. via ReadPageGuard{}/WritePageGuard{})
	 *		  for the duration of any sequnce of reads/writes
	 *		- no atomic operations needed either
	 *		  size_ may be read/written concurrently by other threads
	 *		  	*ONLY* if those threads hold a conflicting rwlatch_
	 *		  	this is impossible, WritePageGuard{} is exclusive, ReadPageGuard{} is shared and excludes writers
	 *		  plain 'int' access is safe under guard context
	 *
	 *	remove __unused__ from b_plus_tree_page.h as soon as the corresponding methods below actuall reference
	 *	the field
	 *	else compiler still considers it as "unused-but-with-tag" which is harmless but stale
	*/
	
	
	/*
	 * Helper methods to get/set page type
	 * Page type enum class is defined in b_plus_tree_page.h
	 */

	/*
	 *	@brief		- true iff page_type_ == IndexPageType::LEAF_PAGE
	 *
	 *	expected algorithM
	 *		return page_type_ == IndexPageType::LEAF_PAGE
	 *
	 *	notes:
	 *		- does *NOT* tread INVALID_INDEX_PAGE specially
	 *		  if called on a page whose Init() has not run yet, returns 'false'
	 *		  i.e. because page_type_ is INVALID after BPM's Reset() zeroes the frame
	 *		       and INVALID != LEAF
	 *		  the caller is responsible for not making that call
	 *		- asymmetry
	 *		  we have IsLeafPage(), but not IsInternalPage()
	 *		  this is intentional @see b_plus_tree_page.h
	*/
	auto BPlusTreePage::IsLeafPage() const -> bool { 
		UNIMPLEMENTED("TODO(P2): Add implementation."); 
	}
	
	/*
	 *	@brief		- write page_type_
	 *
	 *	expected algorithm:
	 *		page_type_ = page_type
	 *	
	 *	called only within derived classes Init()
	 *	i.e. BPlusTree{Internal,Leaf}Page::Init()
	*/
	void BPlusTreePage::SetPageType(
		IndexPageType page_type
	) { 
		UNIMPLEMENTED("TODO(P2): Add implementation."); 
	}
	
	/*
	 * Helper methods to get/set size (number of key/value pairs stored in that
	 * page)
	 */

	/*
	 *	@brief		- return size_
	 *	
	 *	expected algorithm:
	 *		return size_
	 *
	 *	size_ is the no. of occupied slots in a page
	 *	includes tombstoned entries in a leaf (i.e. tombstones are logical delete, not physical delete)
	 *	does *NOT* include the leading invalid slot of an internal page key_array_
	 *	both keys and child ptrs share the same slot count
	 *	@see b_plus_tree_page.h
	*/
	auto BPlusTreePage::GetSize() const -> int { 
		UNIMPLEMENTED("TODO(P2): Add implementation."); 
	}

	/*
	 *	@brief		- absolute set of size_
	 *
	 *	expected algorith<
	 *		size_ = size
	 *
	 *	caller's responsibility
	 *		0 <= size <= max_size_
	 *		Init() and resize callers must respect this
	 *
	 *	reasonable defensive implementation:
	 *		BUSTUB_ASSERT(
	 *			size >= 0 &&
	 *			size <= max_size_
	 *			"size out of rangee"
	 *		)
	 *		size_ = size
	*/
	void BPlusTreePage::SetSize(
		int size
	) { 
		UNIMPLEMENTED("TODO(P2): Add implementation."); 
	}

	/*
	 *	@brief		- size_ += amount
	 *	
	 *	expected algorithm:
	 *		size_ += amount
	 *
	 *	'amount' may be < 0
	 *		used when redistributing
	 *		donor page calls ChangeSizeBy(-k), and the recipient calls ChangeSizeBy(+k)
	 *	
	 *	reasonable defensive implementation:
	 *		```
	 *		int new_size = size_ += amount;
	 *		BUSTUB_ASSERT(
	 *			new_size >= 0 &&
	 *			new_size <= max_size_,
	 *			"ChangeSizeBy() would push size out of range"
	 *		);
	 *		size_ = new_size;
	 *		```
	*/
	void BPlusTreePage::ChangeSizeBy(
		int amount
	) { 
		UNIMPLEMENTED("TODO(P2): Add implementation."); 
	}
	
	/*
	 * Helper methods to get/set max size (capacity) of the page
	 */

	/*
	 *	@brief		- return max_size_
	 *
	 *	expected algorithm:
	 *		return max_size_
	 *
	 *	max_size_ is set once during Init() and is const thereafter
	 *	the on-disk layout of key_array_/value_array_ is size by the compile-time
	 *	*SLOT_CNT macros
	 *	so changing max_size_ at runtime will silently corrupt the page
	*/
	auto BPlusTreePage::GetMaxSize() const -> int { 
		UNIMPLEMENTED("TODO(P2): Add implementation."); 
	}

	/*
	 *	@brief		- write max_size_
	 *
	 *	expected algorithm:
	 *		max_size_ = size
	 *
	 * 	called from Init() of derived classes
	 * 	after that page's capacity is frozen
	*/
	void BPlusTreePage::SetMaxSize(
		int size
	) { 
		UNIMPLEMENTED("TODO(P2): Add implementation."); 
	}
	
	/*
	 * Helper method to get min page size
	 * Generally, min page size == max page size / 2
	 * But whether you will take ceil() or floor() depends on your implementation
	 */

	/*
	 *	@brief		- the "at least half full" threshold
	 *
	 *	expected algorithm
	 *		pick *ONE* and use consistently across the codebasd
	 *		option 1:
	 *			return max_size_ / 2
	 *		option 2:
	 *			return (max_size_ + 1) /2
	 *
	 *	trade off
	 *		- seil gives slighter stricter invariants
	 *		  pages can never drop below ceil(N/2)
	 *		  fewer redistributions/merges in a steady state
	 *		- floor matches textbook convention more closely and is what most of this codebase uses
	 *		  slightly more agressive merge/split behaviour
	 *
	 *	important:
	 *		whichever we use, stick to the same formula for every codepath that decides merge/split
	 *		the root, is however exempt from this
	 *			i.e. root can be single-key leaf
	 *			     or a one pointer internal node merge
	 *			whoever calls GetMinSize() is responsible for checking rootness first 
	 *			i.e. Context::IsRootPage()
	 *
	 *	reasonable defensive:
	 *		max_size_ < 2 is degenerate (i.e. no room for redistribution)
	 *		should never happen because Init() clamps max_size_ to SLOT_CNT() macro
	 *		which are always >> 2 for realistic key/value sizes
	*/
	*/
	auto BPlusTreePage::GetMinSize() const -> int { UNIMPLEMENTED("TODO(P2): Add implementation."); }
	
}  // namespace bustub
