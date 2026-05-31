#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

/*
 *	error code interface
 *
 *	hasDuplicate() return value, fits int8_t, only 5 distinct values:
 *		1	- true, duplicate found
 *		0	- false, no duplicate found
 *		-1	- numsSize out of range
 *		-2	- element in nums[i] out of range
 *		-3	- scratch buffer NULL or smaller than the table needs
 *
 *	invariant violations (probe exhaustion at load factor < 0.5 no pow2 cap)
 *	trap via __built_in_trap() + never returned (i.e. it is a bug in the file and not a caller error)
*/

#if !defined(HD_OK_NO_DUP)
	#define HD_OK_NO_DUP		((int8_t)  0)
#endif
#if !defined(HD_OK_DUP)
	#define HD_OK_DUP		((int8_t)  1)
#endif
#if !defined(HD_ERR_BAD_SIZE)
	#define HD_ERR_BAD_SIZE		((int8_t) -1)
#endif
#if !defined(HD_ERR_BAD_VALUE)
	#define HD_ERR_BAD_VALUE	((int8_t) -2)
#endif
#if !defined(HD_ERR_NO_MEM)
	#define HD_ERR_NO_MEM		((int8_t) -3)
#endif

/*
 *	this should be an impossible gaurd
 *	reserved for conditions that a correct implementation must gaurantee
 *	**NOT** for input validation (i.e. because input validations return error codes)
 *	__built_in_trap() compiles to a faulting instruction (e.g. ud2 on x86)
 *	with no stdlib dependency, so it stays valid in freestanding/embedded
*/
#define INVARIANT(cond) do {\
	if (!(cond)) {\
		__builtin_trap();\
	}\
} while (0)

/*
 *	constraints
*/
#if !defined(NUMS_MAX)
	#define NUMS_MAX		((int32_t)     100000)
#endif
#if !defined(VAL_MIN)
	#define VAL_MIN			((int32_t)-1000000000)
#endif
#if !defined(VAL_MAX)
	#define VAL_MAX			((int32_t) 1000000000)
#endif
#if !defined(SENTINEL_EMPTY)
	#define SENTINEL_EMPTY		INT32_MIN
#endif
#if !defined(CAP_MIN)
	#define CAP_MIN			((size_t)          8)
#endif
#if !defined(CAP_MAX)
	#define CAP_MAX			((size_t)     262144)
#endif


/*
 *	function declarations because we are fucking awasome :(//
*/
int8_t hasDuplicate(
	int32_t *nums, 
	int32_t numsSize,
	int32_t *scratch,
	int32_t scratch_length
);
uint8_t test_harness();

int8_t hasDuplicate(
	int32_t *nums, 
	int32_t numsSize,
	int32_t *scratch,
	int32_t scratch_length
) {
	int8_t ret;

	/*
	 *	constraint
	 *
	 *	numsSize in [0, NUMS_MAX]
	 *	negative is forbidden by Question.md;
	 *	NUMS_MAX would blow past CAP_MAX and corrupt table size invariant
	*/
	if (
		numsSize < 0 ||
		numsSize > NUMS_MAX
	) {
		ret = HD_ERR_BAD_SIZE;
		goto out;
	}

	/*
	 *	empty array trivially has no duplicates, and we never read scratch
	 *	so the caller may legitimately pass (NULL, 0) here
	*/
	if (0 == numsSize) {
		ret = HD_OK_NO_DUP;
		goto out;
	}

	/*
	 *	smalles pow2 cap with cap >= 2 * numsSize and cap >= CAP_MIN
	 *	pow2 lets us mask with (cap - 1) instead of '%' which Knuth multiplicative hashing needs to spread bits cleanly
	 *	load factor still < 0.5
	*/
	size_t cap = CAP_MIN;
	// TODO!!: never ever use a while loop! Please always use a for loop with a definite end, no counter/timer workaround! A definite end!!
	for (;cap < (size_t)numsSize * 2;) {
		cap <<= 1;
	}
	INVARIANT(cap <= CAP_MAX);
	INVARIANT(0 == (cap & (cap - 1)));

	/*
	 *	replace linear 'j' scan with open-addressed hash table
	 *
	 *	layout:
	 * 		keys[cap]	- stores actual int at its hashed slot
	 * 		used[cap]	- parallel flag tells "is this slot occupied" (i.e. empty different from value = "0")
	 *
	 * 	sizing:
	 * 		cap		- numsSize * 2, load factor < 0.5, keeping probe chains short
	 *
	 *	hash:
	 *		knuth multiplicative	- (uint32_t)*2654435769u, then % cap
	 *		cast to unsigned to avoid undefined behavior
	 *
	 *	collisions:
	 *		linear probe - on conflict walk +1 at a time, wrapping at cap, simple + cache-friendly
	 *
	 *	edge cases:
	 *		numsSize == 0, no duplicates possible, return false before mallocing
	 *		malloc(0) is implementation undefined; therefore skipping is cleaner
	*/
	if (0 == numsSize) {
		ret = false;
		goto out;
	}

	/*
	 *	validate caller supplied scratch
	 *	only returning -3
	 *	when NO_HEAP then the static buffer is sized to CAP_MAX, hence this can not fire
	 *	when heap main() catches malloc failure before calling hasDuplicate() (i.e. this is a defensive double check)
	*/
	if (
		NULL == scratch ||
		scratch_length < cap 
	) {
		ret = HD_ERR_NO_MEM;
		goto out;
	}

	/*
	 *	mark every slot empty, memset() can not help
	 *	SENTINEL_EMPTY 0x80 has no all-bytes-equal patter
	*/
	for (size_t i=0; i<cap; i++) {
		scratch[i] = SENTINEL_EMPTY;
	}

	for (int32_t i=0; i<numsSize; i++) {
		/*
		 *	nums[i] in [VAL_MIN, VAL_MAX] checked inline to keep total work O(n)
		 *	a bad value is detected at the position it occurs
		*/
		if (
			nums[i] < VAL_MIN ||
			nums[i] > VAL_MAX
		) {
			ret = HD_ERR_BAD_VALUE;
			goto out;
		}

		// knuth multiplicative hash, masked to cap
		size_t base = (size_t)(
			(uint32_t)nums[i] * 2654435769u
		) & (cap - 1);

		bool placed = false;

		/*
		 *	linear probe, with 2 terminating cases:
		 *		empty slot: 	insert, set placed, break
		 *		equal slot:	duplicate, set ret, jump out
		 *
		 *	loop bound is cap; reaching it means load factor invariant is corrupted
		 *	(i.e. INVARIANT(placed) traps)
		*/
		for (
			size_t p=0;
			p<cap;
			p++
		) {
			size_t h = (base + p) & (cap - 1);
			if (SENTINEL_EMPTY == scratch[h]) {
				scratch[h] = nums[i];
				placed = true;
				break;
			}
			if (nums[i] == scratch[h]) {
				ret = HD_OK_DUP;
				goto out;
			}
		}

		INVARIANT(placed);
	}

	ret = HD_OK_NO_DUP;

	out:
		return ret;
}

int main () {
	int32_t a[] = {1, 2, 3, 3};
	int32_t b[] = {1, 2, 3, 4};
	int32_t bad_val[] = {0, 1, 2000000000};
	int32_t tiny[2] = {0, 0};

	#if defined(NO_HEAP)
		static int32_t scratch_buf[CAP_MAX];
		int32_t *scratch = scratch_buf;
		size_t scratch_length = CAP_MAX;
	#else
		size_t scratch_length = CAP_MAX;
		int32_t *scratch = malloc(scratch_length * sizeof(int32_t));
		if (NULL == scratch) {
			fprintf(stderr, "Error: malloc() failed for scratch buffer\n");
			exit(1);
		}
	#endif

	printf("[1, 2, 3, 3] -> %d (expected 1)\n", hasDuplicate(a, 4, scratch, scratch_length));
	printf("[1, 2, 3, 4] -> %d (expected 0)\n", hasDuplicate(b, 4, scratch, scratch_length));
	printf("empty -> %d (expected  0)\n", (int)hasDuplicate(NULL, 0, scratch, scratch_length));
	printf("bad value 2e9 -> %d (expected  0)\n", (int)hasDuplicate(bad_val, 3, scratch, scratch_length));
	printf("bad size -1 -> %d (expected -1)\n", (int)hasDuplicate(a, -1, scratch, scratch_length));
	printf("NULL scratch -> %d (expected -3)\n", (int)hasDuplicate(a, 4, NULL, 0));
	printf("scratch too small -> %d (expected -3)\n", (int)hasDuplicate(a, 4, tiny, 2));

	#if !defined(NO_HEAP)
		free(scratch);
	#endif

	return 0;
}
