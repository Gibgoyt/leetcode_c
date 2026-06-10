//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager_test.cpp
//
// Identification: test/buffer/buffer_pool_manager_test.cpp
//
// Copyright (c) 2015-2025, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <filesystem>

#include "buffer/buffer_pool_manager.h"
#include "gtest/gtest.h"
#include "storage/page/page_guard.h"

namespace bustub {
	/*
	 *	end-to-end tests for BPM
	 *
	 *	unlike page_guard_test.cpp which uses DiskManagerUnlimitedMemory, these tests use the *REAL* DiskManager backed with a temp file
	 *	giving us a true disk round trip for the dirty page eviction
	 *
	 *	test inventory:
	 *	single-threaded correctness
	 *		- VeryBasicTest()	- smoke test
	 *					  write/read/read/delete one page
	 *		- PagePinEasyTest()	- "pool exhausted -> nullopt
	 *					  on 2 frame BPM
	 *					  pin/unpin/refault round trip on dirty bytes
	 *		- PagePinMediumTest()	- longer fill/drop/evict/re-read sequence
	 *						  verifies LRU/ARC kick in correctly
	 * 	concurrency:
	 * 		- PageAccessTest()	- 1 writer thread + main reader thread
	 * 					  single frame BPM
	 * 					  tests Read <-> Write exclusion
	 * 					  writer must *NOT* mutate while reader holds a shread rwlatch_
	 * 		- ContentionTest()	- 4 writer threads
	 * 					  100k rounds each, same page
	 * 					  no assertion
	 * 					  ASAN/TSAN/finishing-without-deadlock is the verdict
	 * 		- DeadlockTest()	- .../project_1/README.md key tripwire
	 * 					  BPM must drop bpm_latch_ before blocking on page's rwlatch_
	 * 					  otherwise a 2nd WritePage() call from the same thread deadlocks
	 * 		- EvictableTest()	- 1 frame BPM with 8 readers + main
	 * 					  1k rounds
	 * 					  stresses SetEvictable() sync under a concurrent pin/unpin
	 * 	DISABLED_ prefix:
	 * 		same as page_guard_test.cpp
	 * 		stops testing (i.e. test suite does not call DISABLED_)
	 * 		take it out as we build tests:
	 * 			1. VeryBasic()	simplest to implement
	 * 			2. PagePinEasy()
	 * 			3. PagePinMedium()
	 * 			4. PageAccess()
	 * 			5. Contentio()
	 * 			6. Deadlock()
	 * 			7. Evictable()	nastiest to implement
	 *
	 * 	real DiskManager??
	 * 		PagePinEasyTest()/PagePinMediumTest() verifies if dirty bytes survive eviction + re-read
	 * 		with memory backed backend these will also survive because nothing was on a real disk
	 * 		using file-backed DiskManager forces BPM's evict-flush pattern path to actually go through DiskScheduler::Schedule() + DiskManager::WritePage()
	*/
	
	/*
	 *	temp file path used by every test in this file
	 *	*NOT* cleaned up between tests - tests caring about a file's contents can remove() at the end
	 *	if tests are interleaved/rerun without a cleanup, then expect spurious failures
	*/
	static std::filesystem::path db_fname("test.bustub");
	
	// The number of frames we give to the buffer pool.
	const size_t FRAMES = 10;
	
	/*
	 *	test helper:
	 *		snprintf-copy 'src' into 'dest' of size BUSTUB_PAGE_SIZE bytes
	 *		BUSTUB_ENSURE() protecting against silent truncation of test data into page
	*/
	void CopyString(
		char *dest, 
		const std::string &src
	) {
		BUSTUB_ENSURE(src.length() + 1 <= BUSTUB_PAGE_SIZE, "CopyString src too long");
		snprintf(dest, BUSTUB_PAGE_SIZE, "%s", src.c_str());
	}
	
	/*
	 *	VeryBasicTest()
	 *	smoke test of 4 most used BPM ops
	 *
	 *	invariants:
	 *		1. NewPage() returns a valid pid
	 *		   no assertion, implicit
	 *		   every subsequent BPM op would fail
	 *		2. WritePage() -> GetDataMut() -> mutate -> GetData() on the same guard reads back what was written
	 *		   no fault in needed
	 *		   only buffer consistency
	 *		3. after dropping WritePageGuard{} on the scope, then ReadPage() on the same pid sees the same string
	 *		   catches:
	 *		   	dirty bytes lost on eviction
	 *		   	OR Drop() somehow zeroed the frame
	 *		4. 2nd ReadPage() observes the same bytes
	 *		   i.e. taking ReadPageGuard{} does *NOT* mutate the page
	 * 		5. DeletePage() succeeds (i.e. returns true) when page is unpinned
	 * 		   catches:
	 * 		   	DeletePage() forgot to handle the "not curently held" case
	*/
	TEST(BufferPoolManagerTest, DISABLED_VeryBasicTest) {
		// A very basic test.
	
		auto disk_manager = std::make_shared<DiskManager>(db_fname);
		auto bpm = std::make_shared<BufferPoolManager>(FRAMES, disk_manager.get());
	
		const page_id_t pid = bpm->NewPage();
		const std::string str = "Hello, world!";
	
		// Check `WritePageGuard` basic functionality.
		{
			auto guard = bpm->WritePage(pid);
			CopyString(guard.GetDataMut(), str);
			EXPECT_STREQ(guard.GetData(), str.c_str());
		}
	
		// Check `ReadPageGuard` basic functionality.
		{
			const auto guard = bpm->ReadPage(pid);
			EXPECT_STREQ(guard.GetData(), str.c_str());
		}
	
		// Check `ReadPageGuard` basic functionality (again).
		{
			const auto guard = bpm->ReadPage(pid);
			EXPECT_STREQ(guard.GetData(), str.c_str());
		}
	
		ASSERT_TRUE(bpm->DeletePage(pid));
	}
	
	/*
	 *	PagePinEasyTest()
	 *	small pool (i.e. 2 frame)
	 *	explicit "pool exhausted" tests
	 *
	 *	invariants:
	 *		1.1. CheckedWritePage() on first/second pid succeeds, pin == 1 each
	 *		1.2. with both frames pinned, a third NewPage(), CheckedReadPage(), *AND* CheckedWritePage() must all return std::nullopt
	 *		     catches:
	 *		     	BPM tries to evict a pinned frame
	 *		     	OR ignores "all pinned" case and crashes/hangs
	 *		1.3. after explicit .Drop() on each guard, pin must return to 0
	 *
	 *		2.1. with both frames now free, 2 frames can be loaded
	 *		     i.e. pageid0 and pageid1 are no longer resident, because their frames have gotten re-used
	 *		2.2. GetPinCount() on a non-resident page returns std::nullopt
	 *		     catches:
	 *		     	GetPinCount() returns 0 for "non resident" instead of std::nullopt
	 *		     	tests downstream confuse "unpinned" with "evicted"
	 *
	 *		3.1. re-acquiring pageid0/pageid1 (which is now evicted) succeeds
	 *		     bytes read back are the ones we wrote in block 1
	 *		     catches:
	 *		     	dirty pages were not flushed before eviction in block 2
	 *		     	i.e. this is the dirty flush gate
	 *		3.2. mutating str0updated/str1updated
	 *		     then exiting the scope without an explicit Drop()
	 *		     must still trigger an eviction-time flush
	 *		     i.e. ref gets dropped out of scope, this ~dtor
	 *		
	 *		4.1. after block 3's guard destruct, the updated bytes must be on disk
	 *		     re-reading should observe "page0updated"/"page1updated"
	 *
	 *	cleanup:
	 *		removes db_fname and the log file
	 *		tests that do not cleanup rely on the next test re-creating the file (and the file is not set between tests)
	 *		cleanup sloppy across the file, which is an existing pattern in the codebase, and probably needs to be standardized at some point
	*/
	TEST(
		BufferPoolManagerTest, 
		DISABLED_PagePinEasyTest
	) {
		auto disk_manager = std::make_shared<DiskManager>(db_fname);
		auto bpm = std::make_shared<BufferPoolManager>(2, disk_manager.get());
	
		const page_id_t pageid0 = bpm->NewPage();
		const page_id_t pageid1 = bpm->NewPage();
	
		const std::string str0 = "page0";
		const std::string str1 = "page1";
		const std::string str0updated = "page0updated";
		const std::string str1updated = "page1updated";
	
		/*
		 *	block 1
		 *
		 *	fill BPM
		 *	verify "pool full"
		 *	reject new reqs
		*/
		{
			auto page0_write_opt = bpm->CheckedWritePage(pageid0);
			ASSERT_TRUE(page0_write_opt.has_value());
			auto page0_write = std::move(page0_write_opt.value());  // NOLINT
			CopyString(page0_write.GetDataMut(), str0);
	
			auto page1_write_opt = bpm->CheckedWritePage(pageid1);
			ASSERT_TRUE(page1_write_opt.has_value());
			auto page1_write = std::move(page1_write_opt.value());  // NOLINT
			CopyString(page1_write.GetDataMut(), str1);
	
			ASSERT_EQ(1, bpm->GetPinCount(pageid0));
			ASSERT_EQ(1, bpm->GetPinCount(pageid1));
	
			// 1.2. both frames pinned, 3rd req *MUST* fail with std::nullopt
			const auto temp_page_id1 = bpm->NewPage();
			const auto temp_page1_opt = bpm->CheckedReadPage(temp_page_id1);
			ASSERT_FALSE(temp_page1_opt.has_value());
	
			const auto temp_page_id2 = bpm->NewPage();
			const auto temp_page2_opt = bpm->CheckedWritePage(temp_page_id2);
			ASSERT_FALSE(temp_page2_opt.has_value());
	
			// 1.3. explicit Drop() returns frames to evictable state
			ASSERT_EQ(1, bpm->GetPinCount(pageid0));
			page0_write.Drop();
			ASSERT_EQ(0, bpm->GetPinCount(pageid0));
	
			ASSERT_EQ(1, bpm->GetPinCount(pageid1));
			page1_write.Drop();
			ASSERT_EQ(0, bpm->GetPinCount(pageid1));
		}
	
		/*
		 *	block 2
		 *
		 *	both frames are now free and dirty
		 *	with this, allocate 2 new pages
		 *
		 *	this *MUST* evict pageid0/pageid1 to make room in BP
		 *	dirty bytes must be written to disk at evict
		 *		otherwise, 3.1. fails
		*/
		{
			const auto temp_page_id1 = bpm->NewPage();
			const auto temp_page1_opt = bpm->CheckedReadPage(temp_page_id1);
			ASSERT_TRUE(temp_page1_opt.has_value());
	
			const auto temp_page_id2 = bpm->NewPage();
			const auto temp_page2_opt = bpm->CheckedWritePage(temp_page_id2);
			ASSERT_TRUE(temp_page2_opt.has_value());
	
			ASSERT_FALSE(bpm->GetPinCount(pageid0).has_value());
			ASSERT_FALSE(bpm->GetPinCount(pageid1).has_value());
		}
	
		/*
		 *	block 3
		 *
		 *	refault pageid0/pageid1 (right now on disk only, not on BP)
		 *	verify dirty bytes survived eviction in block 2 and update them
		*/
		{
			auto page0_write_opt = bpm->CheckedWritePage(pageid0);
			ASSERT_TRUE(page0_write_opt.has_value());
			auto page0_write = std::move(page0_write_opt.value());  // NOLINT
			EXPECT_STREQ(page0_write.GetData(), str0.c_str());
			CopyString(page0_write.GetDataMut(), str0updated);
	
			auto page1_write_opt = bpm->CheckedWritePage(pageid1);
			ASSERT_TRUE(page1_write_opt.has_value());
			auto page1_write = std::move(page1_write_opt.value());  // NOLINT
			EXPECT_STREQ(page1_write.GetData(), str1.c_str());
			CopyString(page1_write.GetDataMut(), str1updated);
	
			ASSERT_EQ(1, bpm->GetPinCount(pageid0));
			ASSERT_EQ(1, bpm->GetPinCount(pageid1));
		}
	
		/*
		 *	scope exit dropped both guards
		 *	pages are dirty + unpinned + still resident in BP
		 *	no evictio happened yet because no new reqs fired
		*/
		ASSERT_EQ(0, bpm->GetPinCount(pageid0));
		ASSERT_EQ(0, bpm->GetPinCount(pageid1));
	
		/*
		 *	block 4
		 *
		 *	ReadPage() re-acquires
		 *	no eviction is needed since pages are still resident
		 *	reads *MUST* see updated bytes
		*/
		{
			auto page0_read_opt = bpm->CheckedReadPage(pageid0);
			ASSERT_TRUE(page0_read_opt.has_value());
			const auto page0_read = std::move(page0_read_opt.value());  // NOLINT
			EXPECT_STREQ(page0_read.GetData(), str0updated.c_str());
	
			auto page1_read_opt = bpm->CheckedReadPage(pageid1);
			ASSERT_TRUE(page1_read_opt.has_value());
			const auto page1_read = std::move(page1_read_opt.value());  // NOLINT
			EXPECT_STREQ(page1_read.GetData(), str1updated.c_str());
	
			ASSERT_EQ(1, bpm->GetPinCount(pageid0));
			ASSERT_EQ(1, bpm->GetPinCount(pageid1));
		}
	
		ASSERT_EQ(0, bpm->GetPinCount(pageid0));
		ASSERT_EQ(0, bpm->GetPinCount(pageid1));
	
		remove(db_fname);
		remove(disk_manager->GetLogFileName());
	}
	
	/*
	 *	PagePinMediumTest()
	 *	longer eviction
	 *	persistence sequence on a 10 frame BPM
	 *
	 *	sequence:
	 *		1. write "Hello" to pid0
	 *		   Drop()
	 *		   page is dirty + unpinned
	 *		2. fill BPM with 10 new guards (i.e. FRAMES=10)
	 *		   vector retains them, all pinned
	 *		   pid0 is the LRU candidate
	 *		   eviction inside this step must flush pid0's content to disk
	 *		3. verify that all 10 new pages have pin == 1
	 *		4. with BPM saturated, 10 more CheckedWritePage() calls must each return std::nullopt
	 *		   i.e. pool is exhausted
	 *		5. erase() the front of the vector drops the corresponding guard
	 *		   bringing pin = 0 on those 5 pages (FRAMES/2 = 5)
	 *		6. allocate FRAMES/2 - 1 more pages (i.e. 4 more pages), leaving one frame free
	 *		7. with one frame free, ReadPage(pid0) succeeds
	 *		   faults pid0 back in from disk, verifying "Hello" survived
	 *		   i.e. the big payoff for eviction flush
	 *		8. NewPage() + ReadPage() saturates the BP again
	 *		   CheckedReadPage(pid0) now fails (i.e. pid0 was evicted out and the pool is full)
	 *
	 *	catches:
	 *		anything that breaks this pipline:
	 *			dirty page -> evict flush -> disk -> fault-in -> data preserved
	 *		also catches any eviction policy buugs
	 *			i.e. if replacer evicts the wrong page in step 2
	 *			     pid0 will still be a resident in BP and the tests pass without fault-in
	 *			     so because pid0 is meant to be LRU, (i.e. last of mru_ of ArcReplacer), it is evicted from BP
	 *			     TODO!!: please confirm here that pid0/"HellO" is the oldest access page
	*/
	TEST(
		BufferPoolManagerTest, 
		DISABLED_PagePinMediumTest
	) {
		auto disk_manager = std::make_shared<DiskManager>(db_fname);
		auto bpm = std::make_shared<BufferPoolManager>(FRAMES, disk_manager.get());
	
		// Scenario: The buffer pool is empty. We should be able to create a new page.
		const auto pid0 = bpm->NewPage();
		auto page0 = bpm->WritePage(pid0);
	
		// Scenario: Once we have a page, we should be able to read and write content.
		const std::string hello = "Hello";
		CopyString(page0.GetDataMut(), hello);
		EXPECT_STREQ(page0.GetData(), hello.c_str());
	
		page0.Drop();
	
		/*
		 *	Create a vector of unique pointers to page guards, which prevents the guards from getting destructed.
		 *
		 *	the vector pins everything inside it
		 *	i.e. each guard's lifetime ends when its vector slot ends
		 *	this is the standard pattern for "Hold N pages simueltaneously"
		*/
		std::vector<WritePageGuard> pages;
	
		/*
		 *	Scenario: We should be able to create new pages until we fill up the buffer pool.
		 *
		 *	this loop is the one that triggers the eviction of pid0 (i.e. the LRU candidate)
		 *	if BPM does not flush dirty-pid0 here, then the fault-in read check at the end fails
		*/
		for (size_t i = 0; i < FRAMES; i++) {
			const auto pid = bpm->NewPage();
			auto page = bpm->WritePage(pid);
			pages.push_back(std::move(page));
		}
	
		// Scenario: All of the pin counts should be 1.
		for (const auto &page : pages) {
			const auto pid = page.GetPageId();
			EXPECT_EQ(1, bpm->GetPinCount(pid));
		}
	
		/*
		 *	Scenario: Once the buffer pool is full, we should not be able to create any new pages.
		 *
		 *	every CheckedWritePage() must return std::nullopt here
		*/
		for (size_t i = 0; i < FRAMES; i++) {
			const auto pid = bpm->NewPage();
			const auto fail = bpm->CheckedWritePage(pid);
			ASSERT_FALSE(fail.has_value());
		}
	
		/*
		 *	Scenario: Drop the first 5 pages to unpin them.
		 *
		 *	vector::erase(begin) does:
		 *		destucts first element
		 *		calls ...PageGuard{} dtor
		 *		calls Drop()
		 *		pin goes to 0
		 *	check pin *BEFORE* erase (still 1) and after (now 0)
		*/
		for (size_t i = 0; i < FRAMES / 2; i++) {
			const auto pid = pages[0].GetPageId();
			EXPECT_EQ(1, bpm->GetPinCount(pid));
			pages.erase(pages.begin());
			EXPECT_EQ(0, bpm->GetPinCount(pid));
		}
	
		/* 
		 *	Scenario: All of the pin counts of the pages we haven't dropped yet should still be 1.
		 *
		 *	catches:
		 *		erase() somehow affected sibling gaurds
		 *		e.g. move semantics during vector compaction got the pin handshake wrong
		*/
		for (const auto &page : pages) {
			const auto pid = page.GetPageId();
			EXPECT_EQ(1, bpm->GetPinCount(pid));
		}
	
		/*
		 *	Scenario: After unpinning pages {1, 2, 3, 4, 5}, we should be able to create 4 new pages and bring them into
		 *	memory. Bringing those 4 pages into memory should evict the first 4 pages {1, 2, 3, 4} because of LRU.
		 *
		 *	Note:
		 *		"LRU" is used loosely, since ARC picks effectively the same victims
		*/
		for (size_t i = 0; i < ((FRAMES / 2) - 1); i++) {
			const auto pid = bpm->NewPage();
			auto page = bpm->WritePage(pid);
			pages.push_back(std::move(page));
		}
	
		/*
		 *	Scenario: There should be one frame available, and we should be able to fetch the data we wrote a while ago.
		 *
		 *	this here is the big payoff
		 *	pid0 was evicted in the first fill loop
		 *	its "Hello" must have been flushed
		 *	we now fault it back in
		 *	strcmp() verifies the round trip
		*/
		{
			const auto original_page = bpm->ReadPage(pid0);
			EXPECT_STREQ(original_page.GetData(), hello.c_str());
		}
	
		/*
		 *	Scenario: Once we unpin page 0 and then make a new page, all the buffer pages should now be pinned. Fetching page 0
		 *	again should fail.
		*/
		const auto last_pid = bpm->NewPage();
		const auto last_page = bpm->ReadPage(last_pid);
	
		/*
		 *	all 10 frames are now pinned
		 *		4 leftover from the erase round
		 *		4 leftover from the refill (i.e. FRAMES/2 - 1)
		 *		last_page being the 9th
		 *		original_page scope ended above
		 *		pid0 is unpinned, and was evicted by last_pid being allocated
		 *	CheckedReadPage(pid0) fails because there are no free frames and nothing is evictable
		*/
		const auto fail = bpm->CheckedReadPage(pid0);
		ASSERT_FALSE(fail.has_value());
	
		// Shutdown the disk manager and remove the temporary file we created.
		disk_manager->ShutDown();
		remove(db_fname);
	}
	
	/*
	 *	PageAccessTest()
	 *	Read <-> Write mutual exclusion underneath a writer thread
	 *
	 *	Setup:
	 *		1-frame BPM, no eviction possible when pinned
	 *		50 rounds
	 *		50 writes to the same pid, sleeping 5ms between each
	 *		main thread:
	 *			50 reads of the same pid
	 *			for each read:
	 *				- sleep 10ms (i.e. letting the writer race for the latch)
	 *				- take ReadPageGuard{} (shared rwlatch_)
	 *				- snapshot the bytes
	 *				- sleep 10ms (because writer should block on rwlatch_.lock())
	 *				- verify bytes unchanged
	 *	what does this gate?
	 *		if WritePageGuard{} ctor takes shared instead of exclusive
	 *		with shared, writer can race into the frame and mutate during main thread's 10ms sleep
	 *		final STREQ catches is
	 *	
	 *	what if it does not verify?
	 *		does not check if the writer ever ran, because the writer will always run
	 *		50 rounds, 5ms each = 250ms of writer activity
	 *		just checks that the writer must *NEVER* run concurrently with the reader
	 *
	 *	what can flake?
	 *		tight timings:
	 *			if machine is loaded, then the 10ms sleep may not gaurantee that the writer has tried to enter
	*/
	TEST(
		BufferPoolManagerTest, 
		DISABLED_PageAccessTest
	) {
		const size_t rounds = 50;
	
		auto disk_manager = std::make_shared<DiskManager>(db_fname);
		auto bpm = std::make_shared<BufferPoolManager>(1, disk_manager.get());
	
		const auto pid = bpm->NewPage();
		char buf[BUSTUB_PAGE_SIZE];
	
		auto thread = std::thread([&]() {
			// The writer can keep writing to the same page.
			for (size_t i = 0; i < rounds; i++) {
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
				auto guard = bpm->WritePage(pid);
				CopyString(guard.GetDataMut(), std::to_string(i));
			}
		});
	
		for (size_t i = 0; i < rounds; i++) {
			// Wait for a bit before taking the latch, allowing the writer to write some stuff.
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
	
			/*
			 *	While we are reading, nobody should be able to modify the data.
			 *
			 *	taking a ReadPageGuard{} acquires rwlatch_.lock_shared()
			 *	writer's next WritePage() call must block on rwlatch_.lock() (i.e. WritePage() needs an exclusive lock)
			*/
			const auto guard = bpm->ReadPage(pid);
	
			// Save the data we observe.
			memcpy(buf, guard.GetData(), BUSTUB_PAGE_SIZE);
	
			/*
			 *	Sleep for a bit. If latching is working properly, nothing should be writing to the page.
			 *	10ms is enough for the writer's 5ms sleep loop to come around twice if we were not blocking it
			 *	if our guard's shared lock fails to block, we would see different bytes after the sleep
			*/
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
	
			// Check that the data is unmodified.
			EXPECT_STREQ(guard.GetData(), buf);
		}
	
		thread.join();
	}
	
	/*
	 *	ContentionTest()
	 *	4 writers, same page, 100k rounds for each writer
	 *	no assertions
	 *
	 *	criteria for passing:
	 *		1. test completes
	 *		   i.e. no deadlock
	 *		2. no ASAN/TSAN/UBSAN reports during the run
	 *		3. no crashes from torn writers or use-after-free
	 *
	 *	what this test gates:
	 *		- pin_count_ races
	 *		  4 threads are constantly acquiring/releasing on the same frame
	 *		  if pin_count_ is not atomic, or SetEvictable() handshake is not bpm_latch_ protected, then this test would produce TSAN warnings
	 *		  	or underflows pin_count_ (i.e. size_t wraparound -> apparently stuck pinned frame -> later tests fail mysteriously)
	 *		- rwlatch_ races
	 *		  4 threads taking exclusive on the same shared mutex
	 *		  standard std::shared_mutex handles this
	 *		  this test mostly ensures that BPM ctor wires latch properly
	 *
	 *	this test does *NOT* verify for the following:
	 *		correctness of the final byte content
	 *		writers each write its loop counter
	 *		final state is whatever the last writer wrote
	 *		the point is not who wins, the point is that nothing crashes/deadlocks while racing
	 *
	 *	why 100k rounds?
	 *		to catch races that have small per-iteration windows
	 *		1k or 10k often passes even with broken code
	 *		100k tends to surface any flakes in the system within a single test run
	*/
	TEST(
		BufferPoolManagerTest, 
		DISABLED_ContentionTest
	) {
		auto disk_manager = std::make_shared<DiskManager>(db_fname);
		auto bpm = std::make_shared<BufferPoolManager>(FRAMES, disk_manager.get());
	
		const size_t rounds = 100000;
	
		const auto pid = bpm->NewPage();
	
		auto thread1 = std::thread([&]() {
			for (size_t i = 0; i < rounds; i++) {
				auto guard = bpm->WritePage(pid);
				CopyString(guard.GetDataMut(), std::to_string(i));
			}
		});
	
		auto thread2 = std::thread([&]() {
			for (size_t i = 0; i < rounds; i++) {
				auto guard = bpm->WritePage(pid);
				CopyString(guard.GetDataMut(), std::to_string(i));
			}
		});
	
		auto thread3 = std::thread([&]() {
			for (size_t i = 0; i < rounds; i++) {
				auto guard = bpm->WritePage(pid);
				CopyString(guard.GetDataMut(), std::to_string(i));
			}
		});
	
		auto thread4 = std::thread([&]() {
			for (size_t i = 0; i < rounds; i++) {
				auto guard = bpm->WritePage(pid);
				CopyString(guard.GetDataMut(), std::to_string(i));
			}
		});
	
		thread3.join();
		thread2.join();
		thread4.join();
		thread1.join();
	}
	
	/*
	 *	DeadlockTest()
	 *	.../project_1/README.md essential for "§Concurrency" section
	 *
	 *	scenario (lock ordering is very important)
	 *		main:	WritePage(pid0)
	 *			acquires rwlatch_(pid0's frame_id) exclusive
	 *		main:	spawn child
	 *		child:	WritePage(pid0)
	 *			blocks on rwlatch_(pid0's frame_id)
	 *		main:	sleep 1s
	 *			simulates useful work with a sleep
	 *		main:	WritePage(pid1)
	 *			this is the gate DeadlockTest() tests for!!
	 *		main:	guard0.Drop()
	 *		child:	eventually unlock, gets pid0
	 *		child:	join
	 *
	 *	if BPM holds bpm_latch_ *FOR THE DURATION* of WritePage(pid1) call
	 *		specifically while ...PageGuard{} ctor calls rwlatch_.lock() on pid1's frame_id
	 *		then we are fine, because pid1's frame is unlatched
	 *	if BPM holds bpm_latch_ while waiting for child's WritePage(pid0) to complete
	 *		(it does not in DeadlockTest() here)
	 *		then the child is blocked on rwlatch_, and not on bpm_latch_
	 *		this test passes tho ....
	 *	but if BPM acquires bpm_latch_ in the main's WritePage(pid1) *AND* child also takes bpm_latch_
	 *		while blocked on rwlatch_ **THEN A DEADLOCK!!**
	 *
	 *	more common bug:
	 *		BPM::CheckedWritePage() takes bpm_latch_
	 *		then tries to take rwlatch_ on the frame
	 *		but the child is holding rwlatch_ on the frame
	 *		then wait...
	 *		because the child does not hold pid0's rwlatch_, it is blocked, and waiting for it
	 *
	 *	the actual deadlock pattern:
	 *		1. child takes bpm_latch_
	 *		   walks the page_table_
	 *		   releases bpm_latch_
	 *		   (this is good so far if we obey the lock-ordering rule)
	 *		2. child calls rwlatch_(pid0).lock()
	 *		   BLOCKS because main holds rwlatch_ on pid0
	 *		3. main thread does its 1s sleep, then calls WritePage(pid1)
	 *		4. main thread takes bpm_latch_
	 *		   walks the page_table_ for pid1
	 *		5. if main *DOES NOT* drop bpm_latch_ *BEFORE* rwlatch_(pid1).lock
	 *		   then that is *STILL FINE* because pid1's rwlatch_ is free
	 *		   hence NO DEADLOCK
	 *		6. but if main holds bpm_latch_ across the rwlatch_ acquisition
	 *		   *AND* the child thread is waiting on bpm_latch_ for something else
	 *		   DEADLOCK
	 *
	 *	in practice, this DeadlockTest() catches BPM's that do:
	 *		```
	 *		std::scoped_lock l(*bpm_latch_);
	 *		```
	 *		hold this latch for the entire function
	 *		do everything, including ```rwlatch_.lock()``` and ```future.get()```
	 *		without releasing the latch on bpm_latch_ *BEFORE* rwlatch_ is acquired
	*/
	TEST(
		BufferPoolManagerTest, 
		DISABLED_DeadlockTest
	) {
		auto disk_manager = std::make_shared<DiskManager>(db_fname);
		auto bpm = std::make_shared<BufferPoolManager>(FRAMES, disk_manager.get());
	
		const auto pid0 = bpm->NewPage();
		const auto pid1 = bpm->NewPage();
	
		// main thread holds rwlatch_(pid0) exclusively
		auto guard0 = bpm->WritePage(pid0);
	
		// A crude way of synchronizing threads, but works for this small case.
		std::atomic<bool> start = false;
	
		auto child = std::thread([&]() {
			// Acknowledge that we can begin the test.
			start.store(true);
	
			/*
			 *	Attempt to write to page 0.
			 *
			 *	child blocks inside this WritePage(pid0)
			 *		- takes bpm_latch_
			 *		  looks up pid0 in page_table_
			 *		  sees that it is resident
			 *		- bumps pin_count_
			 *		  calls SetEvictable(false)
			 *		- releases bpm_latch_
			 *		  per lock ordering rules
			 *		- constructs WritePageGuard{}
			 *		  ctor calls rwlatch_(pid0).lock()
			 *		- block here until main Drop() on guard0
			*/
			const auto guard0 = bpm->WritePage(pid0);
		});
	
		// Wait for the other thread to begin before we start the test.
		while (!start.load()) {
		}
	
		/*
		 *	Make the other thread wait for a bit.
		 *	This mimics the main thread doing some work while holding the write latch on page 0.
		*/
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	
		/*
		 *	If your latching mechanism is incorrect, the next line of code will deadlock.
		 *	Think about what might happen if you hold a certain "all-encompassing" latch for too long...
		 *
		 *	The trap:
		 *		if WritePage(pid1) below acquires bpm_latch_ and the child thread also needs bpm_latch_
		 *		to make progress
		 *			e.g. for SetEvictable() handshake once it gets rwlatch_
		 *		then we deadlock
		 *		the fix is to maintain the same lock order rule repeated in every doc in this project
		 *		drop bpm_latch_ *BEFORE* blocking (i.e. stop serializing on BPM)
		*/
	
		// While holding page 0, take the latch on page 1.
		const auto guard1 = bpm->WritePage(pid1);
	
		/*
		 *	Let the child thread have the page 0 since we're done with it.
		 *	Drop() rwlatch_(pid0)
		 *	child's WritePage() call unblocks
		 *	child finishes
		*/
		guard0.Drop();
	
		child.join();
	}
	
	/*
	 *	EvictableTest()
	 *	1k rounds of 1 frame BPM with 8 reader threads + main
	 *
	 *	per round:
	 *		winner_pid = NewPage()
	 *		loser_pid = NewPage()
	 *		spawn 8 threads, all waiting on 'signal' cv
	 *		main thread:
	 *			- even rounds
	 *			  ReadPage(winner_pid)
	 *			  shared lokc
	 *			- odd round
	 *			  WritePage(winner_pid)
	 *			  exclusive
	 *			- set original = true
	 *			  cv.notify_all
	 *			- Drop() guard
	 *			  i.e. allowing the readers to proceed
	 *		readers (once woken)
	 *			- ReadPage(winner_pid)
	 *			  must succeed, since winner is being kept in the only frame
	 *			- CheckedReadPage(loser_pid)
	 *			  must fail
	 *			  winner is pinned by the calling reader
	 *			  i.e. no eviction is possible
	 *
	 *	what EvictableTest() gates:
	 *		- SetEvictable() handshake:
	 *		  if main Drop() flips SetEvictable(true), but a reader has already incremented pin_count_
	 *		  then that reader's outstanding ReadPage() guard means that the frame is *NOT* evictable*
	 *		  CheckedReadPage(loser_pid) must see "all frames pinned", returning std::nullopt
	 *		  if SetEvictable() is out of sync with pin_count_, then this fails non-deterministically across 1k rounds
	 *		- read after write transition:
	 *		  odd rounds release a write/exclusive lock, while readers take shared locks
	 *		  even rounds release a read/shared lock, and readers take shared locks
	 *		  catches a wrong unlock_shared() vs. unlock() call in Drop()
	 *
	 *	what EvictableTest() does *NOT* verify
	 *		bytes (i.e. what bytes are written/read)
	 *		this test is purely pin count and evictability
	 *
	 *	why 1k rounds, with 8 threads?
	 *		rare race condition in pin_count_/SetEvictable() sync surface with high contention
	 *		a single-iteration test will mostly pass, even on broken code
	*/
	TEST(
		BufferPoolManagerTest, 
		DISABLED_EvictableTest
	) {
		// Test if the evictable status of a frame is always correct.
		const size_t rounds = 1000;
		const size_t num_readers = 8;
	
		auto disk_manager = std::make_shared<DiskManager>(db_fname);
		/*
		 *	Only allocate one frame of memory to the buffer pool manager.
		 *	1 frame = winner_pid resident in the only frame
		 *	loser_pid is the "would be" evictee that must never get a chance to evict the winner
		*/
		auto bpm = std::make_shared<BufferPoolManager>(1, disk_manager.get());
	
		for (size_t i = 0; i < rounds; i++) {
			std::mutex mutex;
			std::condition_variable cv;
	
			// This signal tells the readers that they can start reading after the main thread has already taken the read latch.
			bool signal = false;
	
			// This page will be loaded into the only available frame.
			const auto winner_pid = bpm->NewPage();
			// We will attempt to load this page into the occupied frame, and it should fail every time.
			const auto loser_pid = bpm->NewPage();
	
			std::vector<std::thread> readers;
			for (size_t j = 0; j < num_readers; j++) {
				readers.emplace_back([&]() {
					std::unique_lock<std::mutex> lock(mutex);
	
					// Wait until the main thread has taken a read latch on the page.
					while (!signal) {
					  cv.wait(lock);
					}
	
					/*
					 *	Read the page in shared mode.
					 *	bump pin_count_ on winner's frame
					*/
					const auto read_guard = bpm->ReadPage(winner_pid);
	
					/*
					 *	Since the only frame is pinned, no thread should be able to bring in a new page.
					 *	The gate:
					 *		must return std::nullopt
					 *		because winner's pin > 0 making the only frame non-evictable
					*/
					ASSERT_FALSE(bpm->CheckedReadPage(loser_pid).has_value());
				});
			}
	
			std::unique_lock<std::mutex> lock(mutex);
	
			if (i % 2 == 0) {
				// Take the read latch on the page and pin it.
				auto read_guard = bpm->ReadPage(winner_pid);
	
				// Wake up all of the readers.
				signal = true;
				cv.notify_all();
				lock.unlock();
	
				/*
				 *	Allow other threads to read.
				 *	Drop() releases *SHARED* rwlatch_
				 *	decreas pins
				 *	readers can now take its own shared latch
				 *	(no exclusion between shared holders)
				*/
				read_guard.Drop();
			} else {
				/*
				 *	Take the read latch on the page and pin it.
				 *	TODO!!: wtf is this comment, this is a write path???
				*/
				auto write_guard = bpm->WritePage(winner_pid);
	
				// Wake up all of the readers.
				signal = true;
				cv.notify_all();
				lock.unlock();
	
				/*
				 *	Allow other threads to read.
				 *	Drop() releases EXCLUSIVE rwlatch_ + decrease pins
				 *	catches Drop() that calls unlock_shared() on an exclusively held shared mutex (i.e. results in UB)
				*/
				write_guard.Drop();
			}
	
			for (size_t i = 0; i < num_readers; i++) {
				readers[i].join();
			}
		}
	}
	
}  // namespace bustub
