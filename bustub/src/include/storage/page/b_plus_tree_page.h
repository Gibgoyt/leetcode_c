//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_page.h
//
// Identification: src/include/storage/page/b_plus_tree_page.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>
#include <climits>
#include <cstdlib>
#include <string>

#include "buffer/buffer_pool_manager.h"
#include "storage/index/generic_key.h"

namespace bustub {
	/*
	 *	B+tree
	 *	project 2, task 1
	 *
	 *	BPlusTreePage{} is a common header included by both leaf pages and internal B+tree pages
	 *	it is *NEVER* instantiated on its own
	 *	both BPlusTreeLeafPage{} and BPlusTreeInternalPage{} inherit from it, overlaying additional fields on top of it
	 *
	 *	the "page is the buffer" pattern:
	 *		B+ tree is *NOT* a heap object
	 *		BPM hands us a buffer of size BUSTUB_PAGE_SIZE (i.e. FrameHeader::data_)
	 *		we reinterpret_cast that buffer into a BBlusTreePage*
	 *		the class here is therefore a memory layout template overlaid on the raw bytes from the buffer, not a normal C++ object
	 *		
	 *		- BPlusTreePage() = delete	- no ctor will ever run on these bytes
	 *		- BPlusTreePage(const&) = delete	- no copy either
	 *		- ~BPlusTreePage() = delete	- no dtor
	 *						  i.e. BPM owns the bytes
	 *		- Init(max_size)		- manual "ctor"
	 *						  called by BPM consumer after the cast
	 *
	 *	consequences when implementing project 2, task 1
	 *	i.e. .../project_2/README.md "§Common Pitfalls" section
	 *		- only add fields of TRIVIALLY-CONSTRUCTIBLE types
	 *		  int, size_t
	 *		  fixed-size arrays of trivial types
	 *		  plain enums
	 *		- no std::vector, std::string, std::unique_ptr, std::optional, std::shared_mutex
	 *		  or any field with a non-trivial ctor/dtor/vptor
	 *		  internal heap pointers / vtable pointers would not survive an eviction + fault-in round trip
	 *		- *DO NOT* modify key_array_/value_array_ in the derived classes
	 *		  its inline sizes are part of on-disk layout contract
	 *
	 *	header layout:
	 *	12 bytes total (i.e. INTERNAL_PAGE_HEADER_SIZE)
	 *	because internal page does not add anything beyond the base
	 *		TODO!!: proper diagram explaining the memory layout here with rectangle box stuff
	 *		offset 0	: page_type_	(int, 4 bytes)
	 *				  INDEX_PAGE_TYPE (INVALID, LEAF, INTERNAL)
	 *		offset 4	: size_		(int, 4 bytes)
	 *				  no. of key/value slots currently used
	 *		offset 8	: max_size_	(int, 4 bytes)
	 *				  capacity of the page (i.e. slot count)
	 *		offset 12	: <derived class field starts here>
	 *
	 *	internal page (i.e. BPlusTreeInternalPage{}) extends this with key_array_ + page_id_array_
	 *	leaf page (i.e. BPlusTreeLeafPage{}) extends this with:
	 *		next_page_id_, num_tombstones_, tombstones_[]
	 *		key_array_, rid_array_
	 *		header grows to 16 bytes for next_page_id_ field
	 *
	 *	templates:
	 *		- INDEX_TEMPLATE_ARGUMENTS
	 *		  3 params:
	 *		  	- KeyType
	 *		  	- ValueType
	 *		  	- KeyComparator
	 *		  used by INTERNAL page
	 *		  no NumTombs needed, since internal pages have no tombstones
	 *		- FULL_INDEX_TEMPLATE_ARUMENTS
	 *		  4 params:
	 *		  	- KeyType
	 *		  	- ValueType
	 *		  	- KeyComparator
	 *		  	- NumTombs
	 *		  used by LEAF page, IndexIterator, and BPlusTree itself
	 *		  no defaults on NumTombs (i.e. the user must supply it)
	 *		- FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN
	 *		  same as FULL_INDEX_TEMPLATE_ARGUMENTS but supplies a default NumTombs = 0
	 *		  used when DECLARING a class for the first time, so the default is in scope
	 *		  subsequent template instantiation must *NOT* redeclare the default (i.e. this is a C++ rule)
	 *		  	i.e. they use non-defn form
	 *
	 *	MappingType macro:
	 *		std::pair<KeyType, ValueType>
	 *		conventional short-hand used by code that wants to talk about "one B+tree slot"
	 *		without referring to key_array_[i] and value_array_[i] separately
	 *		mostly appears in signatures returning a single key/value pair
	 *
	 *	BPlusTreePage{} invariants:
	 *		1. every page is either LEAF_PAGE or INTERNAL_PAGE after Init()
	 *		   INVALID_INDEX_PAGE is the pre-Init() sentinel
	 *		2. 0 <= size_ <= max_size_
	 *		   at all times
	 *		3. non-root pages must satisfy MinSize() <= size_
	 *		   "at least half" rule specified at .../project_2/README.md
	 *		   the root is the only legal under full page
	 *		4. split decision uses size_ vs. max_size_
	 *		   merge/redistribute decision uses size_ vs. MinSize()
	*/
	
	/*
	 *	@brief		- one B+Tree slot's worth of key/value
	 *
	 *	convenience macro used wherever code needs to pass/return a single key/value pair
	 *	without referring to parallel arrays explicitly
	 *	KeyType/ValueType, are the surrounding template params
	*/
	#define MappingType std::pair<KeyType, ValueType>
	
	/*
	 *	template parameter macros
	 *	@see file-level rationale above to decide which to use when
	 *
	 *		FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN	- declares NumTombs with default (i.e. 0)
	 *		FULL_INDEX_TEMPLATE_ARGUMENTS		- declares NumTombs without default
	 *		INDEX_TEMPLATE_ARGUMENTS		- no NumTombs at all
	 *							  internal page
	 *
	*/
	#define FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN \
			template <typename KeyType, typename ValueType, typename KeyComparator, ssize_t NumTombs = 0>
	#define FULL_INDEX_TEMPLATE_ARGUMENTS \
			template <typename KeyType, typename ValueType, typename KeyComparator, ssize_t NumTombs>
	#define INDEX_TEMPLATE_ARGUMENTS template <typename KeyType, typename ValueType, typename KeyComparator>
	
	/*
	 *	@brief		- the 3 possible values of BPlusTreePage::page_type_
	 *
	 *		INVALID_INDEX_PAGE	- the page has not yet been Init()d
	 *					  Init() will have zeroed-memory from a fresh frame
	 *					  a correctly implemented B+Tree should *NEVER* observe this transiently inside NewPage()->Init() during node creation
	 *		LEAF_PAGE		- this page is a BPlusTreeLeafPage{}
	 *		INTERNAL_PAGE		- this page is a BPlusTreeInternalPage{}
	 *
	 *	this enum is fixed-witdth as 'int' on the page so that on-disk byte layout is deterministic across compilers
	 *
	 *	define page type enum
	*/
	enum class IndexPageType { 
		INVALID_INDEX_PAGE = 0, LEAF_PAGE, INTERNAL_PAGE 
	};
	
	/*
	 * Both internal and leaf page are inherited from this page.
	 *
	 * It actually serves as a header part for each B+ tree page and
	 * contains information shared by both leaf page and internal page.
	 *
	 * Header format (size in byte, 12 bytes in total):
	 * ---------------------------------------------------------
	 * | PageType (4) | CurrentSize (4) | MaxSize (4) |  ...   |
	 * ---------------------------------------------------------
	 *
	 *	Inheritance shape
	 *		BPlusTreePage{}
	 *		+-- BPlusTreeInternalPage<K, page_id_t, Cmp>
	 *		+-- BPlusTreeLeafPage<K, RID, Cmp, NumTombs>
	 *
	 *	we will *ALMOST* interact with this class polymorphically
	 *		```
	 *		auto *base = guard.AsMut<BPlusTreePage>();
	 *		if (base->IsLeafPage()) {
	 *			auto *leaf = reinterpret_cast<LeafPage*>(base);
	 *			...
	 *		} else {
	 *			auto *internal = reinterpret_cast<InternalPage*>(base);
	 *			...
	 *		}
	 *		```
	 *		no vtable is in play (i.e. since we deleted everything)
	 *		dispatch is manaul via IsLeafPage()
	*/
	class BPlusTreePage {
		public:
			// Delete all constructor / destructor to ensure memory safety
			BPlusTreePage() = delete;
			BPlusTreePage(
				const BPlusTreePage &other
			) = delete;
			~BPlusTreePage() = delete;
	
			/*
			 *	@brief		- true iff page_type_ == LEAF_PAGE
			 *
			 *	no symmetric IsInternalPage(), the convention is:
			 *		```
			 *		if (page->IsLeafPage()) { ... } else { ...assume internal... }
			 *		```
			 *		this is safe *ONLY* if page_type_ is never INVALID_INDEX_PAGE at the point of inspection
			 *		i.e. Init() has already run
			 *		do not call on a freshly-fetched frame whose Init() has not yet been issued
			*/
			auto IsLeafPage() const -> bool;

			/*
			 *	@brief		- write page_type_
			 *
			 *	called exactly twice per page:
			 *		1. inside Init() of the derived class
			 *		   to set LEAF_PAGE/INTERNAL_PAGE right after BPM hands the buffer over
			 *		2. implicitly back to INVALID when page is deleted
			 *		   deleted with BPM::DeletePage()
			 *		   BPM Reset() frame to all-zeroed bytes, which is IndexPageType::INVALID_INDEX_PAGE since INVALID = 0
			 *
			 *	Outside Init() we should never need to call this directly
			*/
			void SetPageType(
				IndexPageType page_type
			);
	
			/*
			 *	@brief		- no. of slots currently occupied in this page
			 *
			 *	for internal pages:
			 *		no. of invalid_or_valid_key/page_id slots
			 *		which = no. of child ptrs
			 *		recall key[0] is invalid, so the page logically has (size_ - 1) keys, and size_ child ptrs
			 *	
			 *	for leaf pages:
			 *		no. of key/rid slots
			 *		tombstoned entries *STILL* count towards size_
			 *		tombstones are logically deleted but physically present
			 *		@see b_plus_tree_leaf_page.h for more details
			*/
			auto GetSize() const -> int;

			/*
			 *	@brief		- absolute set of size_
			 *
			 *	used by Init() and bulk operations
			 *	prefer ChangeSizeBy() for incremental updates so that the intent is clear
			*/
			void SetSize(
				int size
			);
			
			/*
			 *	@brief		- size_ += amount
			 *			  'amount' may be negative
			 *
			 *	convenience over SetSize(GetSize() + amount)
			 *	useful for code that does B+Tree redistribution
			 *		i.e. ```source->ChangeSizeBy(-k); dest->ChangeSizeBy(+k);```
			 *
			 *	no bounds checking in the API contract
			 *	but BUSTUB_ASERT() in impl guarding 0 <= new_size <= max_size_ is a reasonable defensive choice while debugging
			*/
			void ChangeSizeBy(
				int amount
			);
	
			/*
			 *	@brief		- capacity/max slots of this page
			 *
			 *	set once at Init() and never modified thereafter
			 *	= INTERNAL_PAGE_SLOT_CNT for internal pages
			 *	= LEAF_PAGE_SLOT_CNT for leaf pages 
			 *	those SLOT_CNT macros are derived from BUSTUB_PAGE_SIZE - corresponding header size
			*/
			auto GetMaxSize() const -> int;

			/*
			 *	@brief		- write max_size_
			 *			  only Init() should call this
			*/
			void SetMaxSize(
				int max_size
			);

			/*
			 *	@brief		- the "at least half full" thereshold for this page
			 *
			 *	non-root pages must satisfy GetSize() >= GetMinSize() at all times
			 *	when a deletion would drop a page below MinSize(), then the B+Tree must either:
			 *		- redistribute one slot from a sibling
			 *		- OR merge this page with sibling
			 *
			 *	generally min page size == max_page_size / 2
			 *	but ceil vs floor is an implementation choice
			 *	@see b_plus_tree_page.cpp for the trade off
			 *	root is exempt
			 *		root can have as few as 1 entry
			 *		i.e. internal root with 1 child + zero keys
			 *		     or a single-key leaf
			*/
			auto GetMinSize() const -> int;
	
			/*
			 * TODO(P2): Remove __attribute__((__unused__)) if you intend to use the fields.
			 */
		private:
			/*
			 *	@brief		- one of {INVALID_KEY, LEAF, INTERNAL}
			 *
			 *	stored as IndexPageType (i.e. fixed width enum class)
			 *	4 bytes on disk (i.e. default uint32_t on 64 bit x86/arm)
			 *	
			 *	__attribute__(__unused__) tag silences unused-field warning in the current code
			 *	remove it once IsLeafPage()/SetPageType() has been implemented and actually reference page_type_
			 *
			 *	Member variables, attributes that both internal and leaf page share
			*/
			IndexPageType page_type_ __attribute__(
				(__unused__)
			);

			/*
			 *	@brief		- current slot count
			 *			  4 bytes
			 *
			 *	__unused__ needs to be removed here too once implementation
			 *	Number of key & value pairs in a page
			*/
			int size_ __attribute__(
				(__unused__)
			);
			/*
			 *	@brief		- capacity
			 *			  max_slot_ count
			 *			  4 bytes
			 *
			 *	__unused__ needs to be removed here too
			 *
			 *	Max number of key & value pairs in a page
			*/
			int max_size_ __attribute__(
				(__unused__)
			);
	};
}  // namespace bustub
