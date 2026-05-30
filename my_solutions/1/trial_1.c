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
	/*
	 *	my initial attempt would go about having a lookup table, storing the list there
	 *	I can just iterate i, j through nums[], but ofc that is a bit silly
	 *	so we rather have a lookup table, store nums[0] at seen[0], then move to nums[1], if not seen[0] then store nums[0] at seen[0]
	 *	then move to nums[2], check seen[0] and seen[1] if match
	 *	this is still a O(n^2) I think
	 *
	 *	buffer becomes tricky because we are going to have to do a bounds check!
	 *	this is why I need help
	 *
	 *	buffer of every number seen so far
	 *	max size is numsSize in the all unique case
	*/
	int *seen = malloc((size_t)numsSize * sizeof(int));

	int seenCount = 0;
	bool ret;

	for (
		uint32_t i=0; 
		i<numsSize;
		i++
	) {
		// scan only what we have actually stored
		for (
			uint32_t j=0;
			j<seenCount; 
			j++
		) {
			if (nums[i] == seen[j]) {
				ret = true;
				goto cleanup_and_exit;
			}
		}
		// never seen this value, then append if
		seen[seenCount] = nums[i];
		seenCount++;
	}
	// we have reach nums[-1] nicely, therefore no duplicate
	ret = false;
	cleanup_and_exit:
		free(seen);
		return ret;
}

int main () {
	int a[] = {1, 2, 3, 3};
	int b[] = {1, 2, 3, 4};

	printf("[1, 2, 3, 3] -> %d (expected 1)\n", hasDuplicate(a, 4));
	printf("[1, 2, 3, 4] -> %d (expected 0)\n", hasDuplicate(b, 4));

	return 0;
}
