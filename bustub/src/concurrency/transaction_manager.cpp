//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// transaction_manager.cpp
//
// Identification: src/concurrency/transaction_manager.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/transaction_manager.h"

#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#include "catalog/catalog.h"
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/config.h"
#include "common/exception.h"
#include "common/macros.h"
#include "concurrency/transaction.h"
#include "execution/execution_common.h"
#include "storage/table/table_heap.h"
#include "storage/table/tuple.h"
#include "type/type_id.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

	/*
	 *	Begin(), Commit(), Abort()
	 *	the 3 lifecycle entry points, all 3 need 2 invariants alive:
	 *		1. last_commit_ts_ must be the largest commit_ts of any COMMITTED txn
	 *		2. running_txns_ (i.e. the watermark) must reflect exactly the set of read_ts values
	 *		   of all RUNNING or TAINTER txns
	 *		   AddTxn/RemoveTxn must be paired exactly once per txn lifecycle
	*/

	/*
	 *	Begins a new transaction.
	 *	@param isolation_level an optional isolation level of the transaction.
	 *	@return an initialized transaction
	 *
	 *	lock order:
	 *		txn_map_mutex_ (i.e. the writer)
	 *		do NOT touch commit_mutex here
	 *		may race with a concurrent Commit(), which is fine, we just need to see some monotonic prefix 
	 *		of commits via last_commit_ts_
	 */
	auto TransactionManager::Begin(
		IsolationLevel isolation_level
	) -> Transaction * {
		std::unique_lock<std::shared_mutex> l(txn_map_mutex_);
		auto txn_id = next_txn_id_++;
		auto txn = std::make_unique<Transaction>(txn_id, isolation_level);
		auto *txn_ref = txn.get();
		txn_map_.insert(std::make_pair(txn_id, std::move(txn)));
	
		/*
		 *	project 4, task 1.1
		 *		assign txn_ref->read_ts = last_commit_ts.load() **BEFORE**
		 *		running_txns.AddTxn() call
		 *		Watermark::AddTxn enforces read_ts >= commit_ts_, so the read_ts must be set by then
		*/
	
		running_txns_.AddTxn(txn_ref->read_ts_);
		return txn_ref;
	}
	
	/*
	 *	@brief Verify if a txn satisfies serializability. 
	 *	We will not test this function and you can change / remove it as you want. 
	*/
	auto TransactionManager::VerifyTxn(Transaction *txn) -> bool { return true; }
	
	/**
	 *	Commits a transaction.
	 *	@param txn the transaction to commit, the txn will be managed by the txn manager so no need to delete it by yourself
	 *
	 *	lock order:
	 *		commit_mutex_ (i.e. the global commit lock) -> txn_map_mutex_
	 *		the entire commit-ts allocation + state flip happens under commit_mutex_
	 *		giving commit timestamps their total order across the system
	*/
	auto TransactionManager::Commit(
		Transaction *txn
	) -> bool {
		std::unique_lock<std::mutex> commit_lck(commit_mutex_);
	
		/*
		 *	project 4, task 1.2
		 *		acquire a commit ts
		 *		candidate commit_ts = last_commit_ts_.load() + 1
		 *		we are under commit_mutex_ so no other commits can race for the same ID
		*/
	
		if (txn->state_ != TransactionState::RUNNING) {
			throw Exception("txn not in running state");
		}
	
		if (txn->GetIsolationLevel() == IsolationLevel::SERIALIZABLE) {
			/*
			 *	SERIALIZABLE ONLY!
			 *		verify no read-write conflicts before txn commit
			 *	
			 *	on failure, drop commit lock so Abort() can take other locks
			*/
			if (!VerifyTxn(txn)) {
				commit_lck.unlock();
				Abort(txn);
				return false;
			}
		}
	
		/*
		 *	project 4, task 1.2
		 *		implement the commit logic
		 *
		 *	later tasks:
		 *		stamp every undo log + table-heap write made by this txn with freshly allocated commit_ts
		 *		so that future readers see them as permanent versions, instaed of "tentative writes by txn_id"
		*/
	
		std::unique_lock<std::shared_mutex> lck(txn_map_mutex_);
	
		/*
		 *	project 4, task 1.2
		 *		set commit timestamp + update the last committed timestamp here
		 *
		 *		txn->commit_ts_ = <new_commit_ts>			 
		 *		last_commit_ts-.store(<new_commit_ts>);	// publish for future Begin()s
		 *
		 *	order matters relative to UpdateCommitTs below
		 *	we must give the watermark a valid commit_ts (not INVALID_TS)
		*/

		txn->state_ = TransactionState::COMMITTED;
		running_txns_.UpdateCommitTs(txn->commit_ts_);	// watermark floor moves forward
		running_txns_.RemoveTxn(txn->read_ts_);		// this txn is no longer "reading"
	
		return true;
	}
	
	/**
	 *	Aborts a transaction
	 *	@param		- txn the transaction to abort, 
	 *			  the txn will be managed by the txn manager so no need to delete it by yourself
	 *
	 *	for task 1, we only need the bookkeeping below (i.e. state flip + watermark removal)
	 *	rolling back any writes that the aborted txn already made is a later task
	*/
	void TransactionManager::Abort(
		Transaction *txn
	) {
		if (
			txn->state_ != TransactionState::RUNNING && 
			txn->state_ != TransactionState::TAINTED
		) {
			throw Exception("txn not in running / tainted state");
		}
	
		// TODO(P4): Implement the abort logic!
	
		std::unique_lock<std::shared_mutex> lck(txn_map_mutex_);
		txn->state_ = TransactionState::ABORTED;
		running_txns_.RemoveTxn(txn->read_ts_);
	}
	
	/*
	 *	@brief Stop-the-world garbage collection. 
	 *	Will be called only when all transactions are not accessing the table heap. 
	*/
	void TransactionManager::GarbageCollection() { 
		UNIMPLEMENTED("not implemented"); 
	}
}  // namespace bustub
