//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// rid.h
//
// Identification: src/include/common/rid.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <sstream>
#include <string>

#include "common/config.h"

namespace bustub {
	/*
	 *	RID
	 *	record indentifier
	 *
	 *	a RID is the physical address of a single tuple (i.e. row) inside BusTub's heap storage
	 *	a pair:
	 *		- page_id_	- page_id_t/int_32
	 *				  which page the tuple lives on (handed out from BPM)
	 *		- slot_num_	- uint32_t
	 *				  which slot within that page
	 *
	 *		together they occupy 8 bytes
	 *		RID value type used by BPlusTreeLeafPage{}s
	 *		each leaf entry mapping an idex key -> RID so that an index scan can jump
	 *		directly to the correct heap page + slot without a full table scan
	 *
	 *	packed from:
	 *		Get() and the int64_t ctor move between struct and 64 bit in
	 *			bits 63-32	- page_id_
	 *					  sign-32 int32_t stored in upper 32 bits
	 *			bits 31-0	- slot_num_
	 *					  uint32_t stored in lower 32 bits
	 *
	 *			pack		- int64_t/rid64
	 *					  ```int64_t(page_id_) << 32 | slot_num_```
	 *					  used by std::hash<RID> so that a single hash<int64_t> call covers both fields
	 *					  no extra work
	 *			unpack		- ```
	 *					  page_id_ = static_case<page_id_t>(rid64 >> 32)
	 *					  slot_num_ = static_case<uint32_t>(rid64)
	 *					  ```
	 *	invalid RID
	 *		default ctor produces invalid RID
	 *			page_id_ = INVALID_PAGE_ID/-1
	 *			slot_num_ = 0
	 *		caller's responsiblity to check return != -1 before referencing it
	*/
	
	class RID {
		/*
		 *	RID class
		 *
		 *	trivially constructible/copyable so it can be stored directly in fixed-size arrays of B+Tree leaf pages
		 *	without violating "page is buffer"
		 *
		 *	no non-trivial ctor/dtor allowed in page memory
		*/

		 public:
			/*
			 *	The default constructor creates an invalid RID
			 *
			 * 	page_id_ init to INVALID_PAGE_ID/-1
			 * 	slot_num init to 0
			 *
			 * 	always check GetPageId() != INVALID_PAGE_ID before using this default ctored ctor
			*/
			RID() = default;
		
			/**
			 * Creates a new Record Identifier for the given page identifier and slot number.
			 * @param page_id page identifier
			 * @param slot_num slot number
			 *
			 *	this is the primary ctor used when tuple is inserted into heap page
			 *	resulting page_id/slot pair must be stored in B+Tree leaf as its value
			 *
			 *	@param page_id	- page indentifier
			 *			  which heap page holds the tuple
			 *	@param slot_num	- slot no.
			 *			  logical offset within the page (0-based)
			*/
			RID(
				page_id_t page_id, 
				uint32_t slot_num
			) : page_id_(page_id), slot_num_(slot_num) {
				// 
			}
		
			/*
			 *	@brief		- unpacks RID from 64-bit packed representation
			 *
			 * 	@param rid	- 64 bit packed RID produced by Get()
			 *
			 *	inverse of Get()
			 *		```
			 *		page_id_ = static_cast<page_id_t>(rid >> 32)	// upper 32 bits
			 *		slot_num_ = static_cast<uint32_t>(rid)		// lower 32 bits
			 *		```
			 *
			 *	explicit keyword prevents accidental implicit conversion from int64_t to a RID
			 *
			*/
			explicit RID(
				int64_t rid
			) : page_id_(static_cast<page_id_t>(rid >> 32)), slot_num_(static_cast<uint32_t>(rid)) {
				//
			}
		
			/*
			 *	@brief		- packs page_id_/slot_num_ into 64 bit integer
			 *
			 *	@return		- 64 bit packed RID
			 *
			 *	bits 63-32	- page_id_	(sign-extended int32_t)
			 *	bits 31-0	- slot_num	(uint32_t)
			 *
			 *	used by std::hash<RID>
			 *	anywhere a compact/hashable representation of RID is needed
			*/
			inline auto Get() const -> int64_t { 
				return (static_cast<int64_t>(page_id_)) << 32 | slot_num_; 
			}
		
			/*
			 *	@brief		- return page identifier of this RID
			*/
			inline auto GetPageId() const -> page_id_t { 
				return page_id_; 
			}
		
			/*
			 *	@brief		- return slot no. component of this RID
			 *			  0 based logical offset
			*/
			inline auto GetSlotNum() const -> uint32_t { 
				return slot_num_; 
			} 
			
			/*
			 *	@brief		- mutates RID in place
			 *
			 *	used when a tuple is moved
			 *	e.g. after an update that changes tuple's physical location
			 *	so that index entry can be updated without constructing new RID object
			 *
			 *	@param page_id	- new page identifier
			 *	@param slot_num	- new slot no.
			*/
			inline void Set(
				page_id_t page_id, 
				uint32_t slot_num
			) {
				page_id_ = page_id;
				slot_num_ = slot_num;
			}
		
			/*
			 *	@brief		- return a human readble page id
			 *
			 *	"page_id: X. slot_num: Y\n"
			*/
			inline auto ToString() const -> std::string {
				std::stringstream os;
				os << "page_id: " << page_id_;
				os << ". slot_num: " << slot_num_ << "\n";
		
				return os.str();
			}
		
			/*
			 *	@brief		- streams result of ToString()
			 *
			 *	useful for logging and tests
			*/
			friend auto operator<<(std::ostream &os, const RID &rid) -> std::ostream & {
				os << rid.ToString();
				return os;
			}
		
			/*
			 *	@brief		- 2 RIDs are equal IFF both page_id_/slot_num_ match
			 *
			 *	required by B+Tree while scaninng for an exacty key->RID pair during deletion
			 *	ValueIndex in leaf page iterates rid_array_[] (i.e. using ```==```)
			*/
			auto operator==(
				const RID &other
			) const -> bool { 
				return page_id_ == other.page_id_ && slot_num_ == other.slot_num_; 
			}
		
		 private:
			page_id_t page_id_{INVALID_PAGE_ID};
			uint32_t slot_num_{0};  // logical offset from 0, 1...
		};
	
	}  // namespace bustub
	
	/*
	 *	std::hash<bustub::RID>
	 *
	 *	specialized outside namepsace bustub so that RID can be used as a key in std::unordered_map/std::unordered_set
	 *	without extra boilerplage at the call site
	 *	
	 *	implementation notes;
	 *		delegates hash<int64_t> applied to return of Get()
	 *		packing both fields into single 64 bit word
	 *		hash sensitive to every bit of both page_id_/slot_num_
	*/
	namespace std {
	template <>
	struct hash<bustub::RID> {
		auto operator()(
			const bustub::RID &obj
		) const -> size_t { 
			return hash<int64_t>()(obj.Get()); 
		}
	};
}  // namespace std
