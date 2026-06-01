//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.h
//
// Identification: src/include/execution/executors/seq_scan_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/seq_scan_plan.h"
#include "storage/table/tuple.h"

namespace bustub {
	/*
	 *	SeqScanExecutor walks an entire table heap + yields tuples
	 *
	 *	in project 4, this is also the yield path that **HAS TO** honor MVCC
	 *	for each tuple it visits, if the base version's timestamp is greater than the txn's read_ts
	 *	(or is a tentative txnId of an uncommitted writer that is **NOT US**)
	 *	it must walk the undo chain + reconstruct the visible version
	 *	(or skip the row if no visible version exists)
	 *
	 *	Next() interface is **BATCHED**:
	 *		i.e. we will fill up to batch_size tuples and its respective RIDs into the out vector per call
	 *		returning false means no more tuples ever (i.e. the executor is exhausted)
	*/

	/**
	 *	The SeqScanExecutor executor executes a sequential table scan.
	*/
	class SeqScanExecutor : public AbstractExecutor {
		public:
			SeqScanExecutor(
				ExecutorContext *exec_ctx, 
				const SeqScanPlanNode *plan
			);
	
			/*
			 *	reset internal iterator state to the start of the table
			*/
			void Init() override;
	
			/*
			 *	pull up to `batch_size` tuples from the scan into out vectors
			 *
			 *	@return		- true if at least one tuple was produced this call
			 *			  false if the scan is exhausted
			*/
			auto Next(
				std::vector<bustub::Tuple> *tuple_batch, 
				std::vector<bustub::RID> *rid_batch, 
				size_t batch_size
			) -> bool override;
	
			/*
			 *	@return The output schema for the sequential scan 
			*/
			auto GetOutputSchema() const -> const Schema & override { 
				return plan_->OutputSchema(); 
			}
	
		private:
			/** The sequential scan plan node to be executed */
			const SeqScanPlanNode *plan_;
	};
}  // namespace bustub
