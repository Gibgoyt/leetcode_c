//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// channel.h
//
// Identification: src/include/common/channel.h
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <condition_variable>  // NOLINT
#include <mutex>               // NOLINT
#include <queue>
#include <utility>

namespace bustub {
	/*
	 *	Channel<T> is a thread safe MPMC unbounded FIFO with blocking Get()
	 *
	 *	Design Summary:
	 *		- wraps std::queue<T> behind 1 mutex, 1 condition_variable
	 *		- Put()		- O(1), non-blocking, pushes + notifies all waiters
	 *		- Get()		- O(1) when non-empty, blocks on cv otherwise
	 *		- Bounded()	-
	 *
	 *	why notify_all instead of notify_one?
	 *		notify_all is safe for multi consumer
	 *		the predicate (`!q_.empty()`) prevents spurious wakeups from breaking correctness
	 *		notify_one would be slightly cheaper in the single consumer case (which is what DiskScheduler is)
	 *
	 *	why unique_lock + unlock-before-notify?
	 *		releasing mutex before notify_all lets woken consumer try to acquire lock immediately
	 *		rather than racing still locked consumer
	 *		small optimization, but idiomatic and worth recognizing
	 *
	 *	lifetime:
	 *		- destroying a Channel while threads blocked in Get() results in UB
	 *		- DiskScheduler avoids this by joining its worker (via nullopt sentinel) before any of its members are destroyed
	 *
	 *	movability:
	 *		mutex and cv not movable
	 *		hence, Channel itself is not movable
	 *		hold it by value as a member (just as DiskScheduler does) or by pointer
	*/
	
	/*
	 *	Channels allow for safe sharing of data between threads. This is a multi-producer multi-consumer channel.
	 */
	template <class T>
	class Channel {
	 public:
		Channel() = default;
		~Channel() = default;
	
		/**
		 *	@brief Inserts an element into a shared queue.
		 *
		 *	@param element The element to be inserted.
		 *
		 *	takes the element by value so that move only types (e.g. DiskRequest) can be moved in at the call site
		 *	internally moves into the std::queue
		 *
		 *	notify path:
		 *		1. push under the mutex
		 *		2. release the mutex
		 *		3. notify_all on the cv (any waiting Get() will recheck the predicate)
		 */
		void Put(
			T element
		) {
			std::unique_lock<std::mutex> lk(m_);
			q_.push(std::move(element));
			lk.unlock();
			cv_.notify_all();
		}
	
		/**
		 *	@brief Gets an element from the shared queue. If the queue is empty, blocks until an element is available.
		 *
		 *	the cv.wait predicate `!q.is_empty()` makes Get() robust to spurious wakeups and multi consumer races
		 *		(e.g. another consumer drained the queue between notify and our wake up)
		 *
		 *	returns by value (with move construction if T supports it)
		 */
		auto Get() -> T {
			std::unique_lock<std::mutex> lk(m_);
			cv_.wait(lk, [&]() { return !q_.empty(); });
			T element = std::move(q_.front());
			q_.pop();
			return element;
		}
	
	 private:
		/*
		 *	the single mutex serializing access to q_
		 *	held by Put() briefly
		 *	helf by Get() for the duration of cv.wait
		*/
		std::mutex m_;

		/*
		 *	condition variable signalling "the queue my now be non empty"
		*/

		/*
		 *	underlying FIFO storage
		 *	std::queue is unbounded
		 *	we never cap it
		*/
		std::condition_variable cv_;
		std::queue<T> q_;
	};
}  // namespace bustub
