//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.h
//
// Identification: src/include/execution/executors/update_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/update_plan.h"
#include "storage/table/tuple.h"

namespace bustub {
	/*
	 *	UpdateExecutor
	 *		DML executor for update
	 *	
	 *	MVCC aware update flow (project 4, tasks 3/4)
	 *		1. read the current base tuple + its undo link head
	 *		2. detect write-write conflicts
	 *		   either someone else's tentative or any committed version newer than my read_ts->abort
	 *		3. if this is the txn's **FIRST** touch of the tuple, then create new UndoLog (GenerateNewUndoLog())
	 *		   capturing prior values + point to prev undo link head
	 *		   otherwise, update existing one in place (GenerateUpdatedUndoLog())
	 *		4. atomically write new base tuple + new undo link
	*/
	
	/**
	 *	UpdateExecutor executes an update on a table.
	 *	Updated values are always pulled from a child.
	 */
	class UpdateExecutor : public AbstractExecutor {
		friend class UpdatePlanNode;
	
	 public:
		UpdateExecutor(
			ExecutorContext *exec_ctx, 
			const UpdatePlanNode *plan, 
		        std::unique_ptr<AbstractExecutor> &&child_executor
		);
	
		void Init() override;
	
		auto Next(
			std::vector<bustub::Tuple> *tuple_batch, 
			std::vector<bustub::RID> *rid_batch, 
			size_t batch_size
		) -> bool override;
	
		/** @return The output schema for the update */
		auto GetOutputSchema() const -> const Schema & override { 
			return plan_->OutputSchema(); 
		}
	
	 private:
		/** The update plan node to be executed */
		const UpdatePlanNode *plan_;
	
		/** Metadata identifying the table that should be updated */
		const TableInfo *table_info_;
	
		/** The child executor to obtain value from */
		std::unique_ptr<AbstractExecutor> child_executor_;
	};
}  // namespace bustub
