//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// watermark.cpp
//
// Identification: src/concurrency/watermark.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/watermark.h"
#include <exception>
#include "common/exception.h"

namespace bustub {
	/*
	 *	both functions below are starter subs
	 *		the precondition check in AddTxn() is the only live behaviour today
	 *		task 1 needs us to actually keep `current_reads_` and `watermark_` in sync
	 *
	 *	suggested behaviour (to be implemented):
	 *		AddTxn(read_ts):
	 *			++current_reads_[read_ts]
	 *			if first reader overall:
	 *				watermark_ = read_ts;
	 *			else:
	 *				watermark_ = min(watermark_, read_ts);
	 *		RemoveTxn(read_ts):
	 *			if(--current_reads_[reads_ts] == 0):
	 *				current_reads_.erase(read_ts);
	 *				if current_reads_ is now empty:
	 *					leave watermark_ alone - GetWatermark() falls back to commit_ts_;	
	 *				else if reads_ts_ was min value:
	 *					recompute watermark_ = min over current_reads_.keys()
	 *
	 *	cost note:
	 *		the naive "recomputer min on removal" is O(N) per removal acceptable for this project's scale
	 *		Better to std::map + multiset would give O(logN), but not needed for correctness
	*/
	
	auto Watermark::AddTxn(timestamp_t read_ts) -> void {
		/*
		 *	precondition: read_ts cannot go below the latest commit_ts
		 *	since Begin() reads from last_commit_ts_ which is monotonic
		*/
		if (read_ts < commit_ts_) {
			throw Exception("read ts < commit ts");
		}
	
		// TODO(P4): implement me!
	}
	
	auto Watermark::RemoveTxn(timestamp_t read_ts) -> void {
		// TODO(P4): implement me!
	}
	
}  // namespace bustub
