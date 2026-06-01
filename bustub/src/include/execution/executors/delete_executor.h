//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// delete_executor.h
//
// Identification: src/include/execution/executors/delete_executor.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/delete_plan.h"
#include "storage/table/tuple.h"

namespace bustub {
	/*
	 *	DeleteExecutor
	 *	DML executor for delete
	 *
	 *	MVCC delete is a special update
	 *		base tuple replaced by delete marker (i.e. a tombstone) along with txn's temp_ts
	 *		undo log captures prior value such that concurrent reads at an older read_ts can still see the row
	 *
	 *		on commit, delete marker ts is rewritten to commit_ts
	*/
	
	/**
	 *	DeletedExecutor executes a delete on a table.
	 *	Deleted values are always pulled from a child.
	*/
	class DeleteExecutor : public AbstractExecutor {
		public:
			DeleteExecutor(
				ExecutorContext *exec_ctx, 
				const DeletePlanNode *plan,
			        std::unique_ptr<AbstractExecutor> &&child_executor
			);
	
			void Init() override;
	
			auto Next(
				std::vector<bustub::Tuple> *tuple_batch, 
				std::vector<bustub::RID> *rid_batch, 
				size_t batch_size
			) -> bool override;
	
			/** @return The output schema for the delete */
			auto GetOutputSchema() const -> const Schema & override { 
				return plan_->OutputSchema(); 
			};
	
		private:
			/** The delete plan node to be executed */
			const DeletePlanNode *plan_;
	
			/** The child executor from which RIDs for deleted tuples are pulled */
			std::unique_ptr<AbstractExecutor> child_executor_;
	};
}  // namespace bustub
