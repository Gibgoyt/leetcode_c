//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree.h
//
// Identification: src/include/storage/index/b_plus_tree.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

/**
 * b_plus_tree.h
 *
 * Implementation of simple b+ tree data structure where internal pages direct
 * the search and leaf pages contain actual data.
 * (1) We only support unique key
 * (2) support insert & remove
 * (3) The structure should shrink and grow dynamically
 * (4) Implement index iterator for range scan
 */
#pragma once

#include <algorithm>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <string>
#include <vector>

#include "common/config.h"
#include "common/macros.h"
#include "storage/index/index_iterator.h"
#include "storage/page/b_plus_tree_header_page.h"
#include "storage/page/b_plus_tree_internal_page.h"
#include "storage/page/b_plus_tree_leaf_page.h"
#include "storage/page/page_guard.h"

/*
 *	BPlusTree{}
 *	
 *	implementation of B+Tree data structure when internal pages direct the search, leaf pages contain data
 *		- only support unique key
 *		- supports insert/remove
 *		- structure to shrink/grow dynamically
 *		- implement index iterator for range scan
 *
 *	high level architecture
 *		3 layers of pages that live in BPM
 *			- BPlusTreeHeaderPage		- one per index
 *							  only holds root_page_id_
 *			- BPlusTreeInternalPage		- routing nodes
 *							  key[i] dividing subtree[i-1] and subtree[i]
 *							  key[0] is *ALWAYS* INVALID sentinel/-1
 *							  size_ counting slots (= child ptrs, = real keys + 1)
 *			- BPlusTreeLeafPage		- data nodes
 *							  key/value pairs + optional tombstone ring buffer
 *							  spring of Spring 2026 BεTree feature
 *
 *			all these pages following the "page is buffer" pattern
 *			reinterpret_cast() from raw BUSTUB_PAGE_SIZE BPM frames
 *			never heap allocated
 *			no ctor/dtor
 *
 *	tree invariants:
 *		- all keys unique
 *		  no duplicate key support
 *		- every non-root page follows
 *		  GetMinSize() <= size_ <= max_size_
 *		  root being exempt from lower bound GetMinSize() (i.e. may hold 1 child/1 key)
 *		- leaf pages linked with next_page_id_ for range scans
 *
 *	template parameters:
 *		- KeyType	- type of search key
 *				  e.g. GenericKey<8>
 *		- ValueType	- type stored in leaf page
 *				  e.g. RID or PageID
 *		- KeyComparator	- strict-weak ordering function for KeyType
 *		- NumTombs	- tombstone buffer capacity per leaf
 *				  default 0
 *				  -1 disable tombstone at compile time
 *				  >0 enables BεTree lazy deletion semantics
 *				  @see BPlusTreeLeafPage{} for tombstone semantics
*/

namespace bustub {
	
	struct PrintableBPlusTree;
	
	/*
	 *	@brief Definition of the Context class.
	 *
	 *	Hint: This class is designed to help you keep track of the pages
	 *	that you're modifying or accessing.
	 *
	 *	traversible Context{} for B+Tree operations
	 *	Insert()/Remove()/GetValue()
	 *
	 *	Context{} bundling all mutable state that a tree traversal needs to carry as it descends from root to target leaf
	 *	using it is optional but strongly advised, prevents threading 5 separate parameters through every helper function
	 *
	 *	concurrency
	 *		project 2, task 4
	 *		B+Tree uses latch crabbing (i.e. lock coupling) for concurrent access
	 *		as descent from root to leaf:
	 *			1. acquire latch on child page
	 *			   push to write_set_/read_set_
	 *			2. if child is "safe" for op, release all ancestor guards held so far
	 *			   a safe insert node has size_ < max_size_
	 *			   a safe remove has size_ > GetMinSize()
	 *			3. else, keep the guards
	 *			   i.e. split/merge may need to propogate upwards
	 *		dropping WritePageGuard{}/ReadPageGuard{} releases BPM rwlatch_ on that frame
	 *		clearing write_set_ or destructing Context{} releases all held latches at once
	 *		a convenient "unlock everything" op
	*/
	class Context {
		public:
			/*
			 *	@brief		- WritePageGuard{} for header page
			 *			  holds root_page_id_
			 *
			 *	acquired at the start of Insert()/Remove()
			 *	because either may change root
			 *		root split -> new root
			 *		root underflow -> child promoted
			 *
			 *	drop ...PageGuard{} (i.e. setting it to std::nullopt)
			 *	as soon as we are certain that root will not change on traverse (i.e. it will not need writing to)
			 *	releases header latch, allows concurrent root ptr readers to proceed
			 *
			 *	remains nullopt initially
			 *	no latch held at ctor time
			 *
			 *	When you insert into / remove from the B+ tree, store the write guard of header page here.
			 *	Remember to drop the header page guard and set it to nullopt when you want to unlock all.
			*/
			std::optional<WritePageGuard> header_page_{std::nullopt};
	
			/*
			 *	@brief		- snapshot of root_page_id_ read from header page at start of traversal
			 *
			 *	cached here so IsRootPage() does not re-read header page on every comparison
			 *	must be updated whenever root change is committed
			 *	e.g. after allocating new root at split
			 * Save the root page id here so that it's easier to know if the current page is the root page.
			*/
			page_id_t root_page_id_{INVALID_PAGE_ID};
	
			/*
			 *	@brief		- WritePageGuard{}s accumulate along root
			 *			  current node path
			 *
			 *	push to back as descent
			 *	front() is the highest ancestor still latched
			 *	*POP* from the front (releasing the guard) when latch crabbing determines ancestor is safe
			 *
			 *	on insert:
			 *		back() will eventually be the leaf receiving the new key
			 *	on remove:
			 *		back() will be the leaf losing the key
			 *
			 *	if write_set_ non-empty when Context{} dtor
			 *	all remaining write latches released automatically via WritePageGuard{} dtor
			 *
			 *	Store the write guards of the pages that you're modifying here.
			*/
			std::deque<WritePageGuard> write_set_;
	
			/*
			 *	@brief		- ReadPageGuard{}s accumulated along the root
			 *			  current node path
			 *
			 *	used on read path (i.e. GetValue())
			 *	a read guard blocks concurrent writers but allows concurrent readers
			 *	release parent ReadPageGuard{} as soon a child ReadPageaGuard{} acquierd
			 *
			 *	no need to hold entire ancestor guard chain for a read traversal
			 *
			 *	You may want to use this when getting value, but not necessary.
			*/
			std::deque<ReadPageGuard> read_set_;
	
			/*
			 *	@brief		- returns true is page_id is current root
			 *
			 *	compare against locally cached root_page_id_
			 *	avoids re-acquiring header page latch on every check
			*/
			auto IsRootPage(
				page_id_t page_id
			) -> bool { 
				return page_id == root_page_id_; 
			}
	};
	
	/*
	 *	@brief		- short hand macro for BPlusTree method def in *.cpp files
	 *	
	 *	expands to ```BPlusTree<KeyType, ValueType, KeyComparator, NumTombs>```
	 *
	 *	usage at b_plus_tree.cpp:
	 *		```
	 *		FULL_INDEX_TEMPLATE_ARGUMENTS
	 *			auto BPLUSTREE_TYPE::Insert(...) -> bool { ... }
	 *		```
	 *
	 *	avoids repeating the 4 param template on every out-of-line definition
	*/
	#define BPLUSTREE_TYPE BPlusTree<KeyType, ValueType, KeyComparator, NumTombs>
	
	/*
	 *	@brief		- main B+Tree index class
	 *	
	 *	provides Insert()/Remove()/GetValue(), and range scan iterators
	 *	over a persistent B+Tree stored in BPM
	 *
	 *	type aliases
	 *		private for casting BPM frames inside implementation
	 *		- InternalPage	- ```BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>```
	 *		- LeafPage	- ```BPlusTreeLeafPage<KeyType, ValueType, KeyComparator, NumTombs>```
	 *
	 *	header page (root ptr indirection)
	 *		tree never stores root_page_id_ as plain member
	 *		keeps permanent header_page_id_ pointing to BPlusTreeHeaderPage{} in BPM
	 *		page's root_page_id_ is authorative root ptr
	 *		holding a write latch on header page while changing root makes update atomic on concurrent access
	 *
	 *	BPM wrappin
	 *		ctor accepts raw BufferPoolManager*
	 *		immediately wraps in raw_ptr<TracedBPM>
	 *		all subsequent calls go through bpm_ (i.e. the traced BPM wrapper), never through raw pointer
	 *		*DO NOT* change bpm_ type (test harness reads it direclty)
	*/
	FULL_INDEX_TEMPLATE_ARGUMENTS_DEFN
	class BPlusTree {
			using InternalPage = BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>;
			using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator, NumTombs>;
	
		public:
			/*
			 *	@brief		- ctor a B+Tree index
			 *	wraps BPM in TracedBPM wrapper and stores it at bpm_
			 *	acquires rwlatch_ on header's page + sets it as root_page_id_ = INVALID_PAGE/-1
			 *	because tree starts empty
			 *
			 *	expected algorithm:
			 *		1. init member fields with init list
			 *		2. ```guard = bpm_->WritePage(header_page_id_)```
			 *		3. ```hdr   = guard.AsMut<BPlusTreeHeaderPage>()```
			 *		4. ```hdr->root_page_id_ = INVALID_PAGE_ID```
			 *		guard dtors here
			 *		header's rwlatch_ released before ctor returns
			 *
			 *	@param name	- human readabke index name
			 *			  stored at index_name_
			 *	@param header_page_id	- BPM page ID of pre-allocaed header page
			 *				  must already exist in BPM
			 *				  will be overwritten root_page_id_ = INVALID
			 *	@param buffer_pool_manager	- raw BPM ptr
			 *					  wrapped internally
			 *					  *DO NOT* store/use this after ctor
			 *	@param comaparator	- key comparator function
			 *				  copied
			 *	@param leaf_max_size	- max size in the leaf before split
			 *				  defaults to LEAF_PAGE_SLOT_CNT
			 *	@param internal_max_size	- max slots on internal page before split
			 *					  defaults to INTERNAL_PAGE_SLOT_CNT
			*/
			explicit BPlusTree(
				std::string name, 
				page_id_t header_page_id, 
				BufferPoolManager *buffer_pool_manager,
				const KeyComparator &comparator, 
				int leaf_max_size = LEAF_PAGE_SLOT_CNT,
				int internal_max_size = INTERNAL_PAGE_SLOT_CNT
			);
	
			/*
			 *	@brief		- returns true if this B+Tree has no keys/values
			 *
			 *	expected algorithm:
			 *		1. acquire ReadPageGuard{} on header_page_id
			 *		2. read `hdr->root_page_id_`
			 *		3. return `root_page_id_ == INVALID_PAGE_ID`
			*/
			auto IsEmpty() const -> bool;
	
			/*
			 *	@brief		- insert key/value pair into B+Tree
			 *
			 *	unique key only
			 *		returns false immediately if key already exists
			 *
			 *	high-level algorithm:
			 *		1. acquire WritePageGuard{} on header page
			 *		  read/cache root_page_id_ into ctx.root_page_id_
			 *		  store guard in ctx.header_page_
			 *		2. if tree empty
			 *		   allocate new leaf page
			 *		   write key/value
			 *		   set it as root
			 *		   update hdr->root_page_id
			 *		   return true
			 *		3. descend into correct leaf with cts.write_set_
			 *			1. at each internal node
			 *			   binary search for child subtree
			 *			   whose range contains key
			 *			2. latch crabbing
			 *			   if child is "safe for insert"
			 *			   i.e. size_ < max_size_
			 *			   release all ancestore guards
			 *		4. at the leaf
			 *		   if key already exists
			 *		   return false
			 *		5. insert key/value into leaf
			 *		   in sorted order
			 *		6. if leaf overflow
			 *		   size_ > leaf_max_size_
			 *			1. allocate sibling leaf
			 *			   move upper half entries to it
			 *			2. link
			 *			   ```leaf->next_page_id_ = sibling->page_id_```
			 *			3. push sibling first key to parent
			 *			   InsertIntoParent()
			 *			4. recurse upwards if parent also overflow
			 *			5. if root split
			 *			   allocate new root
			 *			   update ctx + header
			 *		7. return true
			 *	
			 *	@param key	- the key to insert
			 *	@param value	- associated value
			 *			  stored in a leaf page
			 *
			 *	@return		- true on success
			 *			  false if key already exists
			*/
			auto Insert(
				const KeyType &key, 
				const ValueType &value
			) -> bool;
	
			/*
			 *	@brief		- delete key/value pair with given key
			 *
			 *	if tree empty return immediately
			 *	if key not found return immediately
			 *
			 *	expected algorithm:
			 *		1. acquire WritePageGuard{}
			 *		   on header page
			 *		   read root_page_id_ into ctx
			 *		2. if empty
			 *		   return
			 *		3. descend to target leaf
			 *		   via ctx.write_set_ (i.e. latch crabbing)
			 *		   safe for remove
			 *		   	size > GetMinSize()
			 *		   	i.e. will not underflow
			 *		   release ancestor guards when safe
			 *		4. on the leaf
			 *		   If NumTombs > 0
			 *		   	add deletion to tombstone ring buffer
			 *		   	when ring is full write oldest tombstone (i.e. compact array)
			 *		   else,
			 *		   	physically shif remove slot
			 *		5. if leaf underflows
			 *		   size_ < GetMinSize()
			 *			1. try borrow from siblin
			 *			   pull one entry across
			 *			   update separator key in parent
			 *			2. if borrow impossible
			 *			   merge with sibling
			 *			   delete separator key from parent
			 *		6. propogate merges upwards as needed
			 *		7. if root has 1 child remaining
			 *		   promote child to root
			 *		   free old page root
			 *		   update header
			 *		
			 *	@param key	- key to be removed
			*/
			void Remove(
				const KeyType &key
			);
	
			/*
			 *	@brief		- return value associated with a given key
			 *			  i.e. point query
			 *
			 *	expected algorithm:
			 *		1. acquire ReadPageGuard{} on header_page_id_
			 *		   read root_page_id_
			 *		2. if empty
			 *		   return false
			 *		3. descend internal pages
			 *		   latch crabbing for reads
			 *			1. cast frame into InternalPage{}
			 *			2. binary search
			 *			   keys[1..(size_-1)]
			 *			   largest key[i] <= search key
			 *			3. follow child_ptr[i]
			 *			4. push child ReadPageGuard{} to ctx.read_set_
			 *			   release parent
			 *		4. at leaf
			 *			1. cast frame to LeafPage{}
			 *			2. binary search the key
			 *			   tombstoned slots invisible to KeyAt()
			 *			   skipped auto
			 *			3. if found
			 *			   result->push_back(ValueAt(slot))
			 *			   return true
			 *		5. return false
			 *
			 *	@param key	- key to look up
			 *	@param result	- output vector
			 *			  matching value appeneded if found
			 *
			 *	@return		- true if key found
			 *			  otherwise false
			*/
			auto GetValue(
				const KeyType &key, 
				std::vector<ValueType> *result
			) -> bool;
	
			// Return the page id of the root node
			auto GetRootPageId() -> page_id_t;
	
			// Index iterator
			auto Begin() -> INDEXITERATOR_TYPE;
	
			auto End() -> INDEXITERATOR_TYPE;
	
			auto Begin(const KeyType &key) -> INDEXITERATOR_TYPE;
	
			void Print(BufferPoolManager *bpm);
	
			void Draw(BufferPoolManager *bpm, const std::filesystem::path &outf);
	
			auto DrawBPlusTree() -> std::string;
	
			// read data from file and insert one by one
			void InsertFromFile(const std::filesystem::path &file_name);
	
			// read data from file and remove one by one
			void RemoveFromFile(const std::filesystem::path &file_name);
	
			void BatchOpsFromFile(const std::filesystem::path &file_name);
	
			// Do not change this type to a BufferPoolManager!
			std::shared_ptr<TracedBufferPoolManager> bpm_;
	
		private:
			void ToGraph(page_id_t page_id, const BPlusTreePage *page, std::ofstream &out);
	
			void PrintTree(page_id_t page_id, const BPlusTreePage *page);
	
			auto ToPrintableBPlusTree(page_id_t root_id) -> PrintableBPlusTree;
	
			// member variable
			std::string index_name_;
			KeyComparator comparator_;
			std::vector<std::string> log;  // NOLINT
			int leaf_max_size_;
			int internal_max_size_;
			page_id_t header_page_id_;
	};
	
	/**
		* @brief for test only. PrintableBPlusTree is a printable B+ tree.
		* We first convert B+ tree into a printable B+ tree and the print it.
		*/
	struct PrintableBPlusTree {
			int size_;
			std::string keys_;
			std::vector<PrintableBPlusTree> children_;
	
			/**
			 * @brief BFS traverse a printable B+ tree and print it into
			 * into out_buf
			 *
			 * @param out_buf
			 */
			void Print(std::ostream &out_buf) {
				std::vector<PrintableBPlusTree *> que = {this};
				while (!que.empty()) {
					std::vector<PrintableBPlusTree *> new_que;
	
					for (auto &t : que) {
						int padding = (t->size_ - t->keys_.size()) / 2;
						out_buf << std::string(padding, ' ');
						out_buf << t->keys_;
						out_buf << std::string(padding, ' ');
	
						for (auto &c : t->children_) {
							new_que.push_back(&c);
						}
					}
					out_buf << "\n";
					que = new_que;
				}
			}
	};
	
}  // namespace bustub
