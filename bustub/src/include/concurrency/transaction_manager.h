//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// transaction_manager.h
//
// Identification: src/include/concurrency/transaction_manager.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <shared_mutex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "catalog/schema.h"
#include "common/config.h"
#include "concurrency/transaction.h"
#include "concurrency/watermark.h"
#include "recovery/log_manager.h"
#include "storage/table/table_heap.h"
#include "storage/table/tuple.h"

namespace bustub {
	/*
	 *	this is the global owner of MVCC bookkeeping for BusTub
	 *	
	 *	responsibilities:
	 *		- hand out new txn objects with a correct read timestamp (i.e. Begin)
	 *		- allocate a monotonically increasing commit timestamp at commit time
	 *		  automatically publish as the "latest committed version" so that subsequent 
	 *		  Begin() calls observe it (i.e. Commit)
	 *		- tear down a txn that aborts (i.e. Abort)
	 *		- own the per-tuple undo link map (i.e. `version_info_`) that anchors
	 *		  the head of the version chain for every tuple in every table heap
	 *		- track all live read timestamps via `running_txns_` (i.e. this is a watermark)
	 *		  so that GC knows the oldest version that any reader still needs
	 *
	 *	concurrency model (read locks carefully - Task 1 of project 4 depends on it)
	 *		- txn_map_mutex		- writer lock around `txn_map_` only
	 *		- version_info_mutex	- writer lock around `version_info_` only
	 *					  each PageVersionInfo also having its own latch
	 *		- commit_mutex		- serializes commits (i.e. only 1 txn should be able to choose
	 *					  choose 'commit_ts + publish' critical section at a time,
	 *					  giving commit_ts its total order)
	 *	timestamps (concept)
	 *		- each txn has a `read_ts`, which is just commit_ts of the most recent txn at the moment Begin() ran
	 *		  determining what version it can see
	 *		- each txn gets a `commit_ts` on commit (i.e. last_commit_ts_ + 1)
	 *		- INVALID_TS used as a sentinel for `commit_ts unset` running txns
	 *		  @Transaction::commit_ts_
	*/

	/*
	 *	TransactionManager keeps track of all the transactions running in the system.
	*/
	class TransactionManager {
		public:
			TransactionManager() = default;
			~TransactionManager() = default;
		
			/*
			 *	start a new txn
			 *	Project 4, task 1.1:
			 *		must assign `read_ts_ = last_commit_ts` **BEFORE** registering the txn
			 *		with `running_txns_` 
			 *			(i.e. Watermark::AddTxn enforces read_ts >= watermark_commit_ts_,
			 *			so the `read_ts` must already be set) 
			*/
			auto Begin(
				IsolationLevel isolation_level = IsolationLevel::SNAPSHOT_ISOLATION
			) -> Transaction *;
		
			/*
			 *	commit a txn
			 *	project 4, task 1.2
			 *		under `commit_mutex_`, choose commit_ts = last_commit_ts_ + 1
			 *		stamp it onto txn + publish to last_commit_ts_s,
			 *		then drop the txn's read_ts from the watermark
			*/
			auto Commit(
				Transaction *txn
			) -> bool;
		
			/*
			 *	abort a txn
			 *	already handles the book keeping needed for task 1 (i.e. state flip + water mark removal)
			 *	reverting writes is later task
			*/
			void Abort(
				Transaction *txn
			);
			
			/*
			 *	version chain helpers	@see transaction_manager_impl.cpp
			 *	these let executors read/update the head of tuple's undo link chain atomically with a page latch held
			*/
		
			/*
			 *	automatically set prev_link for `rid`
			 *	optionally guarded by `check`
			*/
			auto UpdateUndoLink(
				RID rid, 
				std::optional<UndoLink> prev_link,
				      std::function<bool(std::optional<UndoLink>)> &&check = nullptr
			) -> bool;
		
			/*
			 *	get the current head undo link for `rid`
			 *	or nullopt if NULL
			*/
			auto GetUndoLink(
				RID rid
			) -> std::optional<UndoLink>;
		
			/*
			 *	resolve an UndoLink -> UndoLog if the source txn still has it
			*/
			auto GetUndoLogOptional(
				UndoLink link
			) -> std::optional<UndoLog>;
		
			/*
			 *	resolve UndoLink just as GetUndoLogOptional()
			 *	but throws/asserts if the log is gone
			*/
			auto GetUndoLog(
				UndoLink link
			) -> UndoLog;
		
			/*
			 *	@brief Get the lowest read timestamp in the system. 
			 *	used to determine the safe horizon for GC
			*/
			auto GetWatermark() -> timestamp_t { 
				return running_txns_.GetWatermark(); 
			}
		
			/*
			 *	stop the world GC
			 *	runs only when no executors are touching heaps
			*/
			void GarbageCollection();
		
			/*
			 *	internal state
			 *	these fields are intentionally public in BusTub so that the executor code + tests can poke at them
			 *	keep the locking contracts in mind whenever touching them
			*/
			
			/*
			 *	protects txn map 
			*/
			std::shared_mutex txn_map_mutex_;

			/*
			 *	All transactions, running or committed 
			 *	the key is the internal txn_id (which lives above TXN_START_ID, distinct from commit timestamps)
			*/
			std::unordered_map<txn_id_t, std::shared_ptr<Transaction>> txn_map_;
		
			/*
			 *	per page slot -> head of undo chain map
			 *	each table heap page has one of these
			 *	`slot_offset_t` maps to the latest UndoLink for that tuple slot
			 *	UndoLog itself stored inside the writing txn
			*/
			struct PageVersionInfo {
				/** protects the map */
				std::shared_mutex mutex_;
				/* 
				 *	Stores previous version info for all slots. Note: DO NOT use `[x]` to access it because
				 *	it will create new elements even if it does not exist. Use `find` instead.
				 */
				std::unordered_map<slot_offset_t, UndoLink> prev_link_;
			};
		
			/** protects version info */
			std::shared_mutex version_info_mutex_;
			/*
			 *	Stores the previous version of each tuple in the table heap. 
			 *	Do not directly access this field. Use the helper
			 *	functions in `transaction_manager_impl.cpp`. 
			*/
			std::unordered_map<page_id_t, std::shared_ptr<PageVersionInfo>> version_info_;
		
			/*
			 *	Stores all the read_ts of running txns so as to facilitate garbage collection. 
			*/
			Watermark running_txns_{0};
		
			/*
			 *	Only one txn is allowed to commit at a time 
			 *	held across the entire commit critical section so that the global commit_ts allocation is serial
			*/
			std::mutex commit_mutex_;
			/*
			 *	The last committed timestamp.
			 *	new Begin() reads this for read_ts
			 *	Commit() bumps it by 1 (under commit_mutex_) to allocate the next commit_ts
			 *	starts at 0, the first commit will publish 1
			*/
			std::atomic<timestamp_t> last_commit_ts_{0};
		
			/** Catalog */
			Catalog *catalog_;
		
			/*
			 *	source of fresh internal txn ids
			 *	begins at TXN_START_ID (a constant far far beyond a plausible timestamp)
			 *	so that a raw ID can be visually distinguished from a real commit timestamp
			*/
			std::atomic<txn_id_t> next_txn_id_{TXN_START_ID};
		
		private:
			/*
			 *	hook for serializable verfication at commit time
			*/
			auto VerifyTxn(
				Transaction *txn
			) -> bool;
	};

	/*
	 *	free functions
	 *	page-latch-aware helpers that read or write the pair atomically (i.e. either tuple, or UndoLink pair)
	 *	@see transaction_managner_impl.cpp
	*/
	
	/**
	 *	@brief Update the tuple and its undo link in the table heap atomically.
	*/
	auto UpdateTupleAndUndoLink(
		TransactionManager *txn_mgr, 
		RID rid, 
		std::optional<UndoLink> undo_link, 
		TableHeap *table_heap, 
		Transaction *txn,
		const TupleMeta &meta, 
		const Tuple &tuple,
		std::function<bool(
			const TupleMeta &meta, 
			const Tuple &tuple, 
			RID rid, 
			std::optional<UndoLink>
		)> &&check = nullptr
	) -> bool;
	
	/**
	 *	@brief Get the tuple and its undo link in the table heap atomically.
	 */
	auto GetTupleAndUndoLink(
		TransactionManager *txn_mgr, 
		TableHeap *table_heap, 
		RID rid
	) -> std::tuple<TupleMeta, Tuple, std::optional<UndoLink>>;
	
}  // namespace bustub
