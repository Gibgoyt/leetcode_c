//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.h
//
// Identification: src/include/execution/executors/insert_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/insert_plan.h"
#include "storage/table/tuple.h"

namespace bustub {
	/*
	 *	InsertExecutor
	 *		DML executor for insert
	 *
	 *	pulls tuples from a child executor + writes into target table heap
	 *	in project 4 we must also:
	 *		- stamp inserted base tuple with the current txn's "temp ts"
	 *		  (i.e. the txnId, so that other txns can recognize uncommitted writes)
	 *		- record the RID in the txn's write_set_ so commit/abort can later stamp/undo change
	 *
	 *	Next() returns the count (i.e. no. of inserted rows) wrapped in a single tuple, exactly once
	 *	subsequent calls return false
	*/
	
	/*
	 * InsertExecutor executes an insert on a table.
	 * Inserted values are always pulled from a child executor.
	*/
	class InsertExecutor : public AbstractExecutor {
		public:
			InsertExecutor(
				ExecutorContext *exec_ctx, 
				const InsertPlanNode *plan,
			        std::unique_ptr<AbstractExecutor> &&child_executor
			);
	
			void Init() override;
	
			auto Next(
				std::vector<bustub::Tuple> *tuple_batch, 
				std::vector<bustub::RID> *rid_batch, 
				size_t batch_size
			) -> bool override;
	
			/** @return The output schema for the insert */
			auto GetOutputSchema() const -> const Schema & override { 
				return plan_->OutputSchema(); 
			};
	
		private:
			/** The insert plan node to be executed*/
			const InsertPlanNode *plan_;
	};
	
}  // namespace bustub
