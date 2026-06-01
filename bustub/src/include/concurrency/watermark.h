//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// watermark.h
//
// Identification: src/include/concurrency/watermark.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <unordered_map>

#include "concurrency/transaction.h"
#include "storage/table/tuple.h"

namespace bustub {
	/*
	 *	Watermark
	 *	tracks the **OLDEST** read timestamp still in use by any live txn
	 *
	 *	conceptually it is the multiset of `read_ts` values
	 *		- each call to AddTxn() increments a counter
	 *		- RemoveTxn() decrements counter
	 *		- reaching 0 removes the entry
	 *		GetWatermark() is the the minimum live read_ts
	 *			if no current read txn, then the most recent commit_ts
	 *		(i.e. nothing is older than the latest atomic write)
	 *	
	 *	why we care?
	 *		garbage collection can discard any undo log which ts is older than the watermark
	 *		(i.e. no live txn can ever read it)
	 *
	 * 	invariant policed in AddTxn()
	 * 		read_ts >= commit_ts_
	 * 		txn manager publishes latest commit_ts via UpdateCommitTs **BEFORE** calling RemoveTxn()
	 * 		i.e. watermark's floor only moves forward
	 *
	 * 	task 1:
	 * 		- AddTxn/RemoveTxn must maintain current_reads_ and watermark_
	 * 		- watermark_ should always equal min(current_reads_.keys) when the map is non-empty
	*/
	
	/*
	 * @brief tracks all the read timestamps.
	*/
	class Watermark {
		public:
			/*
			 *	initial commit_ts is the floor
			 *	without any readers, the watermark is the most recent commit
			*/
			explicit Watermark(
				timestamp_t commit_ts
			) : commit_ts_(commit_ts), watermark_(commit_ts) {	
			}
	
			/*
			 *	register a new reader at read_ts
			 *	throws if read_ts < commit_ts_
			*/
			auto AddTxn(
				timestamp_t read_ts
			) -> void;
	
			/*
			 *	unregister a reader previously added with the same read_ts
			*/
			auto RemoveTxn(
				timestamp_t read_ts
			) -> void;
	
			/*
			 *	The caller should update commit ts before removing the txn
			 *	from the watermark so that we can track watermark correctly. 
			*/
			auto UpdateCommitTs(
				timestamp_t commit_ts
			) { 
				commit_ts_ = commit_ts; 
			}
	
			/*
			 *	@return		- current safe horizon,
			 *			  either the min. live read_ts
			 * 			  if no live readers, then the most recent commit_ts
			 *
			*/
			auto GetWatermark() -> timestamp_t {
				if (current_reads_.empty()) {
					return commit_ts_;
				}
			  return watermark_;
			}
	
			/*
			 *	the most recent commit_ts_
			 *	floor below no new read_ts can fall
			 *	since Begin() reads from the last last_commit_ts_
			*/
			timestamp_t commit_ts_;
	
			/*
			 *	cached min. over the keys of current_reads_
			 *	only meaningful when current_reads_ is not empty
			*/
			timestamp_t watermark_;
	
			/*
			 *	multiset of live read_ts values:
			 *		ts->refcount
			 *		an entry is removed when its refcount hits 0
			*/
			std::unordered_map<timestamp_t, int> current_reads_;
	};
	
};  // namespace bustub
