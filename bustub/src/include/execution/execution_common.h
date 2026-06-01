//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// execution_common.h
//
// Identification: src/include/execution/execution_common.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "binder/bound_order_by.h"
#include "catalog/catalog.h"
#include "catalog/schema.h"
#include "concurrency/transaction.h"
#include "storage/table/tuple.h"

namespace bustub {

	/*
	 *	this header is shared between executors + transaction manager
	 *
	 *	contains 2 unrelated groups of utils:
	 *		1. sort utilities (for project 3)
	 *			TupleComparator
	 *			GenerateSortKey
	 *		2. MVCC utilities (for project 4)
	 *			ReconstructTuple
	 *			CollectUndoLogs
	 *			GenerateNewUndoLog
	 *			GenerateUpdatedUndoLog
	 *			TxnMgrDbg
	 *
	 *	MVCC group is for project 4, and we will slowly implement and tack for project 4, tasks 2 and on
	 *	project 4, task 1 only uses these declarations transitively
	*/
	
	/* 
	 *	The SortKey defines a list of values that sort is based on 
	*/
	using SortKey = std::vector<Value>;

	/* 
	 *	The SortEntry defines a key-value pairs for sorting tuples and corresponding RIDs 
	*/
	using SortEntry = std::pair<SortKey, Tuple>;
	
	/*
	 *	The Tuple Comparator provides a comparison function for SortEntry 
	 * 	strict-weak ordering compatible with std::sort and friends
	*/
	class TupleComparator {
		public:
			explicit TupleComparator(
				std::vector<OrderBy> order_bys
			);
		
			auto operator()(
				const SortEntry &entry_a, 
				const SortEntry &entry_b
			) const -> bool;
	
		private:
			std::vector<OrderBy> order_bys_;
	};
	
	/*
	 *	build a SortKey for a tuple by evaluating each OrderBy expression
	*/
	auto GenerateSortKey(
		const Tuple &tuple, 
		const std::vector<OrderBy> &order_bys, 
		const Schema &schema
	) -> SortKey;
	
	/**
	 * Above are all you need for P3.
	 * You can ignore the remaining part of this file until P4.
	 */

	/*
	 *	MVCC utils for project 4
	*/
	
	/*
	 *	reconstruct a tuple by replaying `undo_logs` over `base_tuple`
	 *	`undo_logs[0]` is applied first (i.e. it is the youngest undo, the one nearest to the base tuple)
	 *	then [1], then [2], etc...
	 *	returns nullopt if a delete marker shows that the tuple did not exist at the resulting version
	*/
	auto ReconstructTuple(
		const Schema *schema, 
		const Tuple &base_tuple, 
		const TupleMeta &base_meta,
	        const std::vector<UndoLog> &undo_logs
	) -> std::optional<Tuple>;
	
	/*
	 *	walk the version chain anchorde at 'undo_link' + gather the undo logs needed for `txn` to see the version it 
	 *	is allowed to read (i.e. read_ts)
	 *	retusn nullopt if the tuple did not yet exist at the txn's read_ts
	*/
	auto CollectUndoLogs( 
		RID rid, 
		const TupleMeta &base_meta, 
		const Tuple &base_tuple, 
		std::optional<UndoLink> undo_link,
	        Transaction *txn, 
		TransactionManager *txn_mgr
	) -> std::optional<std::vector<UndoLog>>;
	
	/*
	 *	build the **FIRST** undo log that a txn writes for a tuple
	 *	'ts' is the tuple's pre-modification timestamp (i.e. the timestamp of the current base version at the heap)
	 *	'prev_version' is the existing head of the chain (i.e. the new log's prev pointer)
	*/
	auto GenerateNewUndoLog (
		const Schema *schema, 
		const Tuple *base_tuple, 
		const Tuple *target_tuple, 
		timestamp_t ts,
	        UndoLink prev_version
	) -> UndoLog;
	
	/*
	 *	merge a 2nd modification by the same txn into its existing undo log
	 *	so each txn contains at most 1 undo log per tuple
	 *	modified fields mask gets ORd
	 *	only newly touched fields written into `tuple_`
	*/
	auto GenerateUpdatedUndoLog(
		const Schema *schema, 
		const Tuple *base_tuple, 
		const Tuple *target_tuple,
		const UndoLog &log
	) -> UndoLog;
	
	/*
	 *	debug dump of a table heap + its version chains
	 *	current implementation only prints a placeholder
	 *	later tasks of project 4 will deal with filling it all in
	*/
	void TxnMgrDbg(
		const std::string &info, 
		TransactionManager *txn_mgr, 
		const TableInfo *table_info,
		TableHeap *table_heap
	);
	
	/*
	*	TODO(P4): Add new functions as needed... You are likely need to define some more functions.
	*
	*	To give you a sense of what can be shared across executors / transaction manager, here are the
	*	list of helper function names that we defined in the reference solution. You should come up with
	*	your own when you go through the process.
	*		-  WalkUndoLogs
	*		-  Modify
	*		-  IsWriteWriteConflict
	*		-  GenerateNullTupleForSchema
	*		-  GetUndoLogSchema
	*
	*	We do not provide the signatures for these functions because it depends on the your implementation
	*	of other parts of the system. You do not need to define the same set of helper functions in
	*	your implementation. Please add your own ones as necessary so that you do not need to write
	*	the same code everywhere.
	*/
	
}  // namespace bustub
