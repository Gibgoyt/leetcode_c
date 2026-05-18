/*
 * 	2026/05/18 Ahmed Mot
 *
 * 	Stage A: brute force nested loop, O(n^2) time complexity, mem complexity ???
 *
 * 	int* twoSum(int* nums, int numsSize, int target, int* returnSize);
 *
 * 	@param *nums		-	input array
 * 	@param numsSize		-	input arr length
 * 	@param target		-	the sum we want
 * 	@param returnSize	-	OUT parameter - we must write 2 here so that the caller knows returned element is 2 elements long
 *
 * 	@return		-	malloc'd int arr length 2 holding two indices
 * 			-	caller's responsibility to free()
 * 				not static arr or stack array
*/

#include <stdio.h>
#include <stdlib.h>

int *twoSum(
	int *nums,
	int numsSize,
	int target,
	int *returnSize
) {
	// allocate 2-int arr on heap
	int *result = malloc(2 * sizeof(int));

	// promise the caller that the result is length 2
	*returnSize = 2;

	/*
	 * O(n^2) nested loop
	 * j starts at i+1
	 * 	- never pair an element with itself
	 * 	- never re-check a pair in the opposite order
	*/
	for (int i=0; i<numsSize; i++) {
		for (int j=i+1; j<numsSize; j++) {
			if ( target == (nums[i] + nums[j]) ) {
				result[0] = i;
				result[1] = j;
				return result;
			}
		}
	}

	// input should gaurantee that a solution exists, so this should be unreachable on a valid input, handle defensively anyways
	goto not_found;
	
	// not really necessary, as process ends on main() exit(0) and kernel frees, but handle defensively
	not_found:
		free(result);
		*returnSize = 0;
		return NULL;
}

static void run_test (
	const char *name,
	int *nums,
	int numsSize,
	int target,
	int expected_a,
	int expected_b
) {
	int returnSize = 0;
	int *got = twoSum(nums, numsSize, target, &returnSize);

	printf("[%s] target=%d\n", name, target);

	if (
		NULL == got || 
		2 != returnSize
	) {
		printf("FAIL (no result returned)\n");
		return;
	}

	int a = got[0];
	int b = got[1];
	int sum = nums[a] + nums[b];

	// order insensitive comparison (answer can be in any order)
	int ok = 
		(expected_a == a && expected_b == b) || 
		(expected_b == a && expected_a == b);

	printf("[%d, %d] (values: %d + %d = %d) %s\n", a, b, nums[a], nums[b], sum, ((ok) ? ("OK"):("FAIL")));

	// proper gotos
	goto cleanup;

	// defensive cleanup of heap allocated memory
	cleanup:
		free(got);
}

int main (
	void
) {
	int a[] = {2, 7, 11, 15};
	run_test("example 1", a, 4, 9, 0, 1);
	
	int b[] = { 3, 2, 4 };
	run_test("example 2 (skip self)", b, 3, 6, 1, 2);

	int c[] = { 3, 3 };
	run_test("example 3 (duplicate values)", c, 2, 6, 0, 1);

	int d[] = { -1000000000, 1000000000, 5, 7 };
	run_test("large negatives", d, 4, 0, 0, 1);

	int e[] = { 5, 75, 25 };
	run_test("answer at the end", e, 3, 100, 1, 2);

	return 0;
}
