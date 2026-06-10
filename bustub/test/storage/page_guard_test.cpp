//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// page_guard_test.cpp
//
// Identification: test/storage/page_guard_test.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cstdio>

#include "buffer/buffer_pool_manager.h"
#include "storage/disk/disk_manager_memory.h"
#include "storage/page/page_guard.h"

#include "gtest/gtest.h"

namespace bustub {
	/*
	 *	contract tests for ReadPageGuard()/WritePageGuard()
	 *
	 *	these tests gate concurrency invariants laid out in .../project_1/README.md
	 *	(i.e. "§Concurrency" section)
	 *
	 *	if ...PageGuard{} is implemented properly (i.e. page_guard.{h,cpp}), then BPM's pin/SetEvictable() handshake is right, and both tests pass
	 *	if any of the standard bug classes outlined @see page_guard.cpp at the top, 3 bugs outlined at .../project_1/README.md
	 *		these tests will catch any of those bugs
	 *
	 *	testing
	 *		- DropTest() 	- tests Drop() idempotency
	 *				  dtor cleanup
	 *				  latch release
	 *				  RAII at scope exit
	 *				  dirty-flush-on-eviction roundtrip
	 *		- MoveTest()	- move ctor + move-assign for both ...PageGuard{} typez
	 *				  includes self-move and moves to/from default-constructed guards
	 *				  i.e. the default constructed guards are invalid
	 *
	 *	DISABLED_ prefix???
	 *		GTest() skips any test prefixed with DISABLED_
	 *		current code is shipped with them disabled so that the BPM stub (currently throws UNIMPLEMENTED()) does not make every CI turn red
	 *		to enable, strip 'DISABLED_' prefix once enough of the BPM/...PageGuard{} to make these tests meaningful
	 *		standard order would follow:
	 *			1. implement BPM::GetPinCount()
	 *			   i.e. the cheapest path to read the pin_count_
	 *			2. implement BPM::NewPage()/CheckedReadPage()/CheckedWritePage() to mint ...PageGaurd{}s
	 *			3. implement ...PageGuard{}' ctor/dtor/Drop()
	 *			4. re-enable DropTest()
	 *			5. implement move-ctor/move-assign
	 *			6. re-enable MoveTest()
	 *
	 *	DiskManagerUnlimitedMemory
	 *		this is RAM disk manager (i.e. from project 1, task 2)
	 *		avoids touching filesystem (I/O entirely to RAM)
	 *		the only test backend appropriate for ...PageGuard{} tests because we want predictable + near 0 latency I/O
	 *			so that these tests can exercise latching and not disk timing
	 *		
	 *	Standard BPM size for tests is num_frames_ = 10
	 *	tests that want to stress eviction allocate more than 10 over the lifetime so that pool has to recycle frames
	*/
	
	/*
	 *	BPM set capacity to BP at num_frames_ = 10 
	 *	picked small enough to exercise eviction
	 *	i.e. "fill the BPM" scenario in  DropTest() below deliberately allocates exactly 10 pages, then even more to force eviction of first pool
	*/
	const size_t FRAMES = 10;
	
	/*
	 *	DropTest(), exercises the full Drop() + dtor lifecycle of both ...PageGuard{} types
	 *
	 *	invariants that this tests gate:
	 *		1. mint a WritePageGuard{} -> pin_count_ = 1
	 *		   catches:
	 *		   	ctor forgot to include pin or did it twice
	 *		2. guard.Drop() -> pin_count_ = 0
	 *		   catches:
	 *		   	Drop() forgot to decrment pin
	 *		   	or held lock + decremented but did not sync SetEvictable()
	 *		   	or got the ordering wrong
	 *		3. Drop() called a 2nd time -> still pin_count_ == 0
	 *		   catches:
	 *		   	Drop() is not idempotent
	 *		   	i.e. does not check is_valid_ at the top
	 *		   	could underflow pin_count_ (i.e. size_t wrap around to ~2^64)
	 *		4. ~...PageGuard{} at the end of the scope, after an explicit Drop() -> no UB
	 *		   catches:
	 *		   	dtor does not short-circuit on is_valid_ = false
	 *		   	double frees the latch
	 *		5. ReadPageGuard{}/WritePageGuard{} on different pages -> both pinnned, both droppable independently
	 *		   catches:
	 *		   	pin/unpin keyed on something shared across pages
	 *		   	e.g. a static, or accidentally on the BPM instead of on the frame
	 *		6. inner scope re-acquires WritePage on pids that were previously held
	 *		   i.e. "WILL HANG IF LATCHES ARE NOT UNLOCKED"
	 *		   this is a critical DropTest() hang test
	 *		   catches:
	 *		   	Drop() forgot to release rwlatch_
	 *		   	WritePage() call below blocks on rwlatch_.lock() forever
	 *		7. fill BPM with frames guards in a vector
	 *		   when vector destructs, every ...PageGuard{} dtor must Drop() cleanly
	 *		   after the scope, every page's pin_count_ = 0
	 *		   catches:
	 *		   	dtor not calling Drop()
	 *		   	or guards in a container that was moved/copied improperly
	 *		   	i.e. we deleted copy, so push_back() into vector<...PageGuard{}>
	 *		   	requires move ctor to exist and work properly
	 *		8. mutate a page (i.e. CopyString "data")
	 *		   drop the guard
	 *		   fill + evict BPM
	 *		   re-ReadPage() on mutable_page_id
	 *		   expect to see "data"
	 *		   catches:
	 *		   	dirty page was not flushed when its frame was evicted from the BP
	 *		   	BPM's eviction  needs to watch is_dirty_ and schedule write *before* Reset() on fid
	 *		   	...PageGuard{}::GetDataMut() has to have set is_dirty_ in the first place
	 * 
	 * Note:
	 * 	scope (i.e. {...}) each deliberately "let RAII fire here"
	 * 	such that we "}" (i.e. scope finished), ~...PageGuard{} runs
	 * 	do not refactor scopes
	*/
	TEST(
		PageGuardTest, 
		DISABLED_DropTest
	) {
		auto disk_manager = std::make_shared<DiskManagerUnlimitedMemory>();
		auto bpm = std::make_shared<BufferPoolManager>(FRAMES, disk_manager.get());
	
		{
			const auto pid0 = bpm->NewPage();
			auto page0 = bpm->WritePage(pid0);
	
			// The page should be pinned.
			ASSERT_EQ(1, bpm->GetPinCount(pid0));
	
			// A drop should unpin the page.
			page0.Drop();
			ASSERT_EQ(0, bpm->GetPinCount(pid0));
	
			// Another drop should have no effect.
			page0.Drop();
			ASSERT_EQ(0, bpm->GetPinCount(pid0));
		}  // Destructor should be called. Useless but should not cause issues.
	
		const auto pid1 = bpm->NewPage();
		const auto pid2 = bpm->NewPage();
	
		{
			auto read_guarded_page = bpm->ReadPage(pid1);
			auto write_guarded_page = bpm->WritePage(pid2);
	
			ASSERT_EQ(1, bpm->GetPinCount(pid1));
			ASSERT_EQ(1, bpm->GetPinCount(pid2));
	
			// Dropping should unpin the pages.
			read_guarded_page.Drop();
			write_guarded_page.Drop();
			ASSERT_EQ(0, bpm->GetPinCount(pid1));
			ASSERT_EQ(0, bpm->GetPinCount(pid2));
	
			// Another drop should have no effect.
			read_guarded_page.Drop();
			write_guarded_page.Drop();
			ASSERT_EQ(0, bpm->GetPinCount(pid1));
			ASSERT_EQ(0, bpm->GetPinCount(pid2));
		}  // Destructor should be called. Useless but should not cause issues.
	
		/*
		 *	This will hang if the latches were not unlocked correctly in the destructors.
		 *	invariant 6. 
		 *	if any prior Drop() failed to release rwlatch_ 
		 *	then WritePage() will block forever on its rwlatch_.lock()
		 *	GTest() per test timeout (or my damned patience) will catch it
		*/
		{
			const auto write_test1 = bpm->WritePage(pid1);
			const auto write_test2 = bpm->WritePage(pid2);
		}
	
		std::vector<page_id_t> page_ids;
		{
			/*
			 *	Fill up the BPM.
			 *
			 *	invariant 7.
			 *	vector storage triggers move-ctor on every WritePageGuard{} as it grows
			 *	fault move-ctor (e.g.  forgot to invalidate "that") will show up here as double Drop() on scope exit
			*/

			std::vector<WritePageGuard> guards;
			for (size_t i = 0; i < FRAMES; i++) {
				const auto new_pid = bpm->NewPage();
				guards.push_back(bpm->WritePage(new_pid));
				ASSERT_EQ(1, bpm->GetPinCount(new_pid));
				page_ids.push_back(new_pid);
			}
		}  // This drops all of the guards.
	
		for (size_t i = 0; i < FRAMES; i++) {
			ASSERT_EQ(0, bpm->GetPinCount(page_ids[i]));
		}
	
		// Get a new write page and edit it. We will retrieve it later
		const auto mutable_page_id = bpm->NewPage();
		auto mutable_guard = bpm->WritePage(mutable_page_id);
		strcpy(mutable_guard.GetDataMut(), "data");  // NOLINT
		mutable_guard.Drop();
	
		{
			/*
			 *	Fill up the BPM again.
			 *
			 *	this block of allocation forces ArcReplacer to evict mutable_page_id
			 *	i.e. it is the LRU candidate
			 *
			 *	*IF* BPM eviction path does *NOT* flush dirty pages, data is lost and assertion below fails
			*/
			std::vector<WritePageGuard> guards;
			for (size_t i = 0; i < FRAMES; i++) {
				auto new_pid = bpm->NewPage();
				guards.push_back(bpm->WritePage(new_pid));
				ASSERT_EQ(1, bpm->GetPinCount(new_pid));
			}
		}
	
		/*
		 *	Fetching the flushed page should result in seeing the changed value.
		 *
		 *	re-faults mutable_page_id back in from disk
		 *	test for invariant 8.
		 *		if we see "data" then it worked
		 *		if we see either zero's, or fresh init'd frame sentinel through a strcmp() then it failed
		*/
		auto immutable_guard = bpm->ReadPage(mutable_page_id);
		ASSERT_EQ(0, std::strcmp("data", immutable_guard.GetData()));
	
		/*
		 *	Shutdown the disk manager and remove the temporary file we created.
		 *	
		 *	DiskManagerUnlimitedMemory has nothing on disk to clean up
		 *	ShutDown() just drains in flight reqs
		*/
		disk_manager->ShutDown();
	}
	
	/*
	 *	MoveTest()
	 *
	 *	exercises every single legal move pattern on ReadPageGuard{}/WritePageGuard{}
	 *
	 *	invariants that MoveTest() gates:
	 *		1. self-move
	 *		   ```
	 *		   guard = std::move(guard_r);
	 *		   ```
	 *		   where 'guard_r' is a ref to guard
	 *		   leaves pin_count_ unchanges
	 *		   catches
	 *			move-assign *WITHOUT* a check `&that == this; return this;`
	 *			without the guard, Drop() ourselves first
	 *			then move from cleared fields
	 *			pin_count_ underflows + unlokc rwlatch_ (leaving it locked causes UB)
	 *		2. move-assign valid -> valid
	 *		   dst held pidA = 1
	 *		   src held pidB = 1
	 *		   after `dst = std::move(src)` 
	 *		   then pidA pin = 0, dst must Drop() before adopting
	 *		   and pidB pin = 1, src resource is now live and in 'dst'
	 *		   catches:
	 *		   	move-assign forgot to Drop() itself first
	 *		   	i.e. pidA leaks pin
	 *		   	or did not invalidate src, so ~src Drop()s pinB a 2nd time
	 *		3. move-construct from valid
	 *		   src held pidB pin = 1
	 *		   after `auto dst(std::move(src))`
	 *		   pidB pin still = 1 (i.e. transferred, not released)
	 *		   catches:
	 *		   	move ctor either forgot to copy the fields (i.e. pidB pin drops to pidB = 0, when ~src runs)
	 *		   	or forgot to invalidate src (i.e. ~src Drop()s pidB and pin goes to 0 prematurely)
	 *		4. after all moves on pidX
	 *		   inner scope acquires WritePage(pidX)
	 *		   will hang if a latch on pidX was *NOT* properly released during the move chain
	 *		   e.g. move-assign released rwlatch_ on src before moving its ownership over
	 *
	 *		   catches:
	 *		   	same Bug 2 class as DropTest() invariant 6.
	 *		   	i.e. "inner scope re-acquires WritePage on pids that were previously held"
	 *		   	but, specifically along the move path
	 *		5. move-ctor with invalid 'that'
	 *		   i.e. default-ctor guard
	 *		   ```
	 *		   auto x{std::move(default_guard)}
	 *		   ```
	 *		   this must *NOT* crash
	 *		   *MUST* produce another invalid guard
	 *		   catches:
	 *		   	move-ctor unconditionally Drop() 'that' 
	 *		   	or reads from that.frame_ (i.e. a null shared_ptr) without the is_valid_ guard
	 *		6. move-assign with invalid 'that' 
	 *		   ```
	 *		   valid_guard = std::move(invalid_guard)
	 *		   ```
	 *		   Drop()s the valid guard's held page
	 *		   (i.e. pidX's pin = 0)
	 *		   leaving dst invalid
	 *		   verified implicitly + the test does not check pin, just that it does not crash or hang
	 *		   catches:
	 *		   	move-assign assumes that.is_valid_ == true
	 *
	 * symmetry
	 * 	every pattern is tested for ReadPageGuard{} (i.e. guards 1, 2, and 3)
	 * 	and WritePageGuard{} (i.e. guard 4, and 5)
	 * 	asymmetric bugs:
	 * 		read's move-assign is correct
	 * 		copy-pased write move-assign incorrectly
	 * 		this will get caught
	*/
	TEST(
		PageGuardTest, 
		DISABLED_MoveTest
	) {
		auto disk_manager = std::make_shared<DiskManagerUnlimitedMemory>();
		auto bpm = std::make_shared<BufferPoolManager>(FRAMES, disk_manager.get());
	
		const auto pid0 = bpm->NewPage();
		const auto pid1 = bpm->NewPage();
		const auto pid2 = bpm->NewPage();
		const auto pid3 = bpm->NewPage();
		const auto pid4 = bpm->NewPage();
		const auto pid5 = bpm->NewPage();
	
		auto guard0 = bpm->ReadPage(pid0);
		auto guard1 = bpm->ReadPage(pid1);
		ASSERT_EQ(1, bpm->GetPinCount(pid0));
		ASSERT_EQ(1, bpm->GetPinCount(pid1));
	
		/*
		 *	This shouldn't change pin counts...
		 *
		 *	invariant 1.
		 *	self-move guard test
		*/
		auto &guard0_r = guard0;
		guard0 = std::move(guard0_r);
		ASSERT_EQ(1, bpm->GetPinCount(pid0));
	
		/*
		 *	Invalidate the old guard0 by move assignment.
		 *
		 *	invariant 2.
		 *	read move-assign
		 *
		 *	guard0 (pid0) <- guard1 (pid1)
		 *	expected after:
		 *		pid0 pin = 0 (i.e. because guard0 Drop() fires)
		 *		pid1 pin = 1 (i.e. moved to guard1)
		 *		guard1 is now invalid
		*/
		guard0 = std::move(guard1);
		ASSERT_EQ(0, bpm->GetPinCount(pid0));
		ASSERT_EQ(1, bpm->GetPinCount(pid1));
	
		/*
		 *	Invalidate the old guard0 by move construction.
		 *
		 *	invariant 3.
		 *	read move-ctor
		 *
		 *	guard0a <- guard0
		 *	expected after:
		 *		pid1 pin still = 1 (i.e. transferred, not released)
		 *		pid0 pin still 0
		*/
		auto guard0a(std::move(guard0));
		ASSERT_EQ(0, bpm->GetPinCount(pid0));
		ASSERT_EQ(1, bpm->GetPinCount(pid1));
	
		auto guard2 = bpm->ReadPage(pid2);
		auto guard3 = bpm->ReadPage(pid3);
		ASSERT_EQ(1, bpm->GetPinCount(pid2));
		ASSERT_EQ(1, bpm->GetPinCount(pid3));
	
		// This shouldn't change pin counts...
		auto &guard2_r = guard2;
		guard2 = std::move(guard2_r);
		ASSERT_EQ(1, bpm->GetPinCount(pid2));
	
		// Invalidate the old guard3 by move assignment.
		guard2 = std::move(guard3);
		ASSERT_EQ(0, bpm->GetPinCount(pid2));
		ASSERT_EQ(1, bpm->GetPinCount(pid3));
	
		// Invalidate the old guard2 by move construction.
		auto guard2a(std::move(guard2));
		ASSERT_EQ(0, bpm->GetPinCount(pid2));
		ASSERT_EQ(1, bpm->GetPinCount(pid3));
	
		/*
		 *	This will hang if page 2 was not unlatched correctly.
		 *
		 *	invariant 4.
		 *	pid2 must have its shared rwlatch_ released somewhere during the move chain above
		 *	specifically when guard2 = std::move(guard3) ran and implicitly Drop() old pid2 ownership
		 *
		 *	WritePage() tries to take exclusive lock of pid2's frame
		 *	but if it still a shared lock, then hang
		*/
		{ const auto temp_guard2 = bpm->WritePage(pid2); }
	
		auto guard4 = bpm->WritePage(pid4);
		auto guard5 = bpm->WritePage(pid5);
		ASSERT_EQ(1, bpm->GetPinCount(pid4));
		ASSERT_EQ(1, bpm->GetPinCount(pid5));
	
		// This shouldn't change pin counts...
		auto &guard4_r = guard4;
		guard4 = std::move(guard4_r);
		ASSERT_EQ(1, bpm->GetPinCount(pid4));
	
		// Invalidate the old guard5 by move assignment.
		guard4 = std::move(guard5);
		ASSERT_EQ(0, bpm->GetPinCount(pid4));
		ASSERT_EQ(1, bpm->GetPinCount(pid5));
	
		// Invalidate the old guard4 by move construction.
		auto guard4a(std::move(guard4));
		ASSERT_EQ(0, bpm->GetPinCount(pid4));
		ASSERT_EQ(1, bpm->GetPinCount(pid5));
	
		/*
		 *	This will hang if page 4 was not unlatched correctly.
		 *
		 *	invariant 4.
		 *	write-guard invariant
		 *
		 *	Note:
		 *		ReadPage() here not WritePage()
		 *		i.e. leftover shared/exclusive lock will still block Read
		*/
		{ const auto temp_guard4 = bpm->ReadPage(pid4); }
	
		// moves involving INVALID/default-constructe guards

		/*
		 *	Test move constructor with invalid that
		 *
		 *	invalidread0/invalidwrite0 has is_valid_ = false
		 *	every owning field is null shared_ptr
		 *	a correct move-ctor copies the fields verbatim
		 *		sets this->is_valid_ = that.is_valid_ (i.e. this->is_valid_ = false)
		 *		sets that.is_valid_ = false (i.e. a no-op)
		 *		never touches null frame_/replacer_/etc...
		 *	~invalidread1/invalidwrite1 *MUST* short circuit to Drop() on the same flag
		*/
		{
			ReadPageGuard invalidread0;
			const auto invalidread1{std::move(invalidread0)};
			WritePageGuard invalidwrite0;
			const auto invalidwrite1{std::move(invalidwrite0)};
		}
	
		/*
		 *	Test move assignment with invalid that
		 *
		 *	```read = std::move(invalidread)```, we *MUST* do the following:
		 *		1. Drop() the currently valid 'read'
		 *		   i.e. pid pin goes to 0
		 *		2. move from the invalid source
		 *		   i.e. a no-op since all fields are null/invalid
		 *		3. leave 'read' itself as invalid
		 *		   is_valid_ = false
		 *	
		 *	the test does *NOT* assert on pin here, but the operation must not crash
		 *	which it would free if move-assign blindly dereferenced sources null frame_
		*/
		{
			const auto pid = bpm->NewPage();
			auto read = bpm->ReadPage(pid);
			ReadPageGuard invalidread;
			read = std::move(invalidread);
			auto write = bpm->WritePage(pid);
			WritePageGuard invalidwrite;
			write = std::move(invalidwrite);
		}
	
		// Shutdown the disk manager and remove the temporary file we created.
		disk_manager->ShutDown();
	}
	
}  // namespace bustub
