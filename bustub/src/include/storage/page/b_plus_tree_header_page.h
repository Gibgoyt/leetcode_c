//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_header_page.h
//
// Identification: src/include/storage/page/b_plus_tree_header_page.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include "common/config.h"

namespace bustub {
	/*
	 *	BPlusTreeHeaderPage{} 
	 *	root pointer page for B+Tree
	 *
	 *	naive B+Tree will store root_page_id_ as plain member variable
	 *	this works in single threaded, breaks under concurrency:
	 *		when a root split happens, then two concurrent readers can observe different root IDs 
	 *		depending on when they read the variable
	 *	this BPlusTreeHeaderPage{} is to prevent race condition under the current on-disk lifetime
	 *
	 *	BusTube solves this with 1 level of indirection
	 *	a single dedicated BPM page (i.e. always at header_page_id_) stores the current root page id
	 *	any thread needing to read/write the root must acquire BPM rwlatch_ on this page
	 *	serializes all root ptr changes without re-acquiring a tree-wide mutex
	 *	a mutex is only needed on page of the root page
	 *
	 *	"page is buffer" pattern
	 *
	 *		like every other B+Tree, BPlusTreeHeaderPage{} is *NEVER* heap allocated
	 *		BPM hands raw 4KiB frame, and reinterpret_cast() it
	 *			
	 *			```
	 *			WritePageGuard guard = bpm->WritePageGuard(header_page_id_)
	 *			auto *hdr = guard.AsMut<BPlusTreeHeaderPage>
	 *			```
	 *
	 *		ctor/dtor deleted to prevent accidental stack/heap allocation
	 *		the only "initialization" is to write INVALID_PAGE_ID to root_page_id_
	 *		BPlusTree::BPlusTree() does exactly this at ctor
	 *
	 *	lifetime:
	 *		one header page exists for the entire lifetime of an index
	 *		its page_id (i.e. header_page_id_ in BPlusTree{}) never changes
	 *		only the value of root_page_id_ changes as the tree grows (i.e. root splits)
	 *			or shrinks (i.e. root underflows, and its only child becomes the new root)
	*/
	
	class BPlusTreeHeaderPage {
		public:
			// Delete all constructor / destructor to ensure memory safety
			BPlusTreeHeaderPage() = delete;
			BPlusTreeHeaderPage(
				const BPlusTreeHeaderPage &other
			) = delete;
	
			/*
			 *	@brief		- page ID of current root's node of the B+Tree
			 *
			 *	set INVALID_PAGE_ID when tree is empty
			 *	either before any key has been inserted or after last key has been removed
			 *	
			 *	writers (i.e. Insert()/Remove()) holds WritePageGuard{} on this header page
			 *	for duration of any operation that may change root
			 *		- root split
			 *		  old root split -> new root page allocated
			 *		  root_page_id_ updated to new root's Page ID
			 *		- root underflow
			 *		  root has one child left -> that child becomes new root
			 *		  root_page_id_ is updated, old root page is freed
			 *
			 *	the guard is released 
			 *	via Context::header_page_.reset()
			 *	as soon as operation confirmes the root will not change, allowing concurrent readers to proceed
			*/
			page_id_t root_page_id_;
	};
	
}  // namespace bustub
