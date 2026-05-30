#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

/*
 *	function declarations because we are fucking awasome :(//
*/
bool hasDuplicate(
	int *nums,
	int numsSize
);
uint8_t test_harness();

bool hasDuplicate(
	int *nums, 
	int numsSize
) {
	bool ret;
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

	size_t cap = (size_t)numsSize * 2;

	int *keys = malloc(cap * sizeof(int));
	bool *used = calloc(cap, sizeof(bool));

	for (
		uint32_t i=0; 
		i<numsSize;
		i++
	) {
		// knuth multiplicative hash, reduced mod cap
		size_t base = (size_t)(
			(uint32_t)nums[i] * 2654435769u
		) % cap;
		size_t h = base;
		bool slot_resolved = false;

		/*
		 *	linear probe, walk +1 until we find either our key or an empty slot
		 *	loop has a concrete induction variable 'p' and a concrete upper bound 'cap'
		 *	bound is not a timer or retry-limit, if p ever reaches cap, then the invariant has been broken
		 *	break on either termination condition, post-loop code never has to re-evaluate which case won
		 *	(i.e. this would require re-read of the slot)
		*/
		for (
			size_t p=0;
			p<cap;
			p++
		) {
			h = (base + p) % cap;
			if (
				!used[h] ||
				nums[i] == keys[h]
			) {
				slot_resolved = true;
				break;
			}
		}

		if (!slot_resolved) {
			/*
			 *	load factor < 0.5 would gaurantee that we resolved a slot
			 *	reaching here means the hash table invariant is corrupted (i.e. logical bug, not runtime condition)
			*/
			abort();
		}

		if (used[h]) {
			// loop exits on keys[h] == nums[i] (i.e. meaning duplicate found)
			ret = true;
			goto out_free_used;
		}

		// loop exits because slot was empty, add to hashtable
		keys[h] = nums[i];
		used[h] = true;
	}
	// we have reach nums[-1] nicely, therefore no duplicate
	ret = false;
	out_free_used:
		free(used);
	out_free_keys:
		free(keys);
	out:
		return ret;
}

int main () {
	int a[] = {1, 2, 3, 3};
	int b[] = {1, 2, 3, 4};

	printf("[1, 2, 3, 3] -> %d (expected 1)\n", hasDuplicate(a, 4));
	printf("[1, 2, 3, 4] -> %d (expected 0)\n", hasDuplicate(b, 4));

	return 0;
}
