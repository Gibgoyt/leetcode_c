/*
 * 2026-05-18T16:29:44.021Z - Ahmed Moti
 *
 * one-pass hash table
 * O(n) time
 * O(n) space
 *
 * algorithm:
 * 	walk nums left->right, at each i ask the hash table:
 * 		- have I already seen (target - nums[i])?
 * 			- yes: return [stored_index, i]
 * 			- no: insert (nums[i] -> i) and continue
 * 		check first + insert after naturally avoids pairing an element with itself
*/

#include <stdio.h>
#include <stdlib.h>

typedef struct HashEntry {
	int key;
	int value;
	struct HashEntry *next;
} HashEntry;

typedef struct {
	HashEntry **buckets;
	int size;
} HashTable;

static int hash(
	int key,
	int size
) {
	return (
		(unsigned int)key % (unsigned int)size
	);
}

static HashTable *ht_create(
	int size
) {
	HashTable *table = malloc(sizeof(*table));
	table->size = size;
	table->buckets = calloc(size, sizeof(HashEntry*));

	return table;
}

static void ht_put(
	HashTable *table,
	int key,
	int value
) {
	// idx, is which bucket to put it in
	int idx = hash(key, table->size);
	for (HashEntry *p=table->buckets[idx]; p != NULL; p=p->next) {
		if (key == p->key) {
			p->value=value;
			return;
		}
	}

	HashEntry *node = malloc(sizeof(*node));
	node->key = key;
	node->value = value;
	node->next = table->buckets[idx];

	table->buckets[idx] = node;
}

static int ht_get(
	HashTable *table,
	int key,
	int *out_value
) {
	// which bucket to check
	int idx = hash(key, table->size);
	for (HashEntry *p=table->buckets[idx]; p != NULL; p=p->next) {
		if (key == p->key) {
			*out_value = p->value;
			return 1;
		}
	}
	return 0;
}

#if defined(DEBUG)
	static void ht_print(
		HashTable *table
	) {
		printf("  HashTable size=%d\n", table->size);
		for (int i=0; i<table->size; i++) {
			printf("    bucket[%d] -> ", i);
			for (HashEntry *p=table->buckets[i]; p != NULL; p=p->next) {
				printf("(key=%d, value=%d) -> ", p->key, p->value);
			}
			printf("NULL\n");
		}
	}
#endif

static void ht_free(
	HashTable *table
) {
	for (int i=0; i<table->size; i++) {
		HashEntry *p = table->buckets[i];
		while (p != NULL) {
			HashEntry *next = p->next;
			free(p);
			p = next;
		}
	}
	free(table->buckets);
	free(table);
}

int *twoSum(
	int *nums,
	int numsSize,
	int target,
	int *returnSize
) {
	int *result = malloc(2*sizeof(int));
	*returnSize = 2;

	HashTable *table = ht_create(2*numsSize + 1);

	#if defined(DEBUG)
		printf("\n[twoSum] nums = [");
		for (int k=0; k<numsSize; k++) {
			printf("%d%s", nums[k], (k == numsSize - 1) ? "" : ", ");
		}
		printf("], target=%d, table_size=%d\n", target, 2*numsSize + 1);
	#endif

	for (int i=0; i<numsSize; i++) {
		int complement = target - nums[i];
		int seen_idx;

		#if defined(DEBUG)
			printf("  i=%d  nums[i]=%d  complement=%d\n", i, nums[i], complement);
		#endif

		// first check if complement was stored by a previous iteration
		if (ht_get(table, complement, &seen_idx)) {
			#if defined(DEBUG)
				printf(
					"    HIT: complement %d found at index %d -> return [%d, %d]\n",
					complement, seen_idx, seen_idx, i
				);
				printf("  Final hash table state:\n");
				ht_print(table);
			#endif
			result[0] = seen_idx;
			result[1] = i;
			goto cleanup;
		}

		#if defined(DEBUG)
			printf(
				"    MISS: insert (key=%d, value=%d) into bucket[%d]\n",
				nums[i], i, (int)((unsigned)nums[i] % (unsigned)table->size)
			);
		#endif
		ht_put(table, nums[i], i);

		#if defined(DEBUG)
			ht_print(table);
		#endif
	}

	free(result);
	result = NULL;
	*returnSize = 0;

	cleanup:
		ht_free(table);
		return result;
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
		printf("  FAIL (no result returned)\n");
		return;
	}

	int a = got[0];
	int b = got[1];
	int sum = nums[a] + nums[b];

	// order-insensitive comparison (problem allows answer in any order)
	int ok =
		(expected_a == a && expected_b == b) ||
		(expected_b == a && expected_a == b);

	printf(
		"  [%d, %d] (values: %d + %d = %d) %s\n",
		a, b, nums[a], nums[b], sum,
		((ok) ? ("OK") : ("FAIL"))
	);

	goto cleanup;

	cleanup:
		free(got);
}

int main (
	void
) {
	int a[] = { 2, 7, 11, 15 };
	run_test("example 1", a, 4, 9,   0, 1);

	int b[] = { 3, 2, 4 };
	run_test("example 2 (skip self)", b, 3, 6,   1, 2);

	int c[] = { 3, 3 };
	run_test("example 3 (duplicate values)", c, 2, 6,   0, 1);

	int d[] = { -1000000000, 1000000000, 5, 7 };
	run_test("large negatives", d, 4, 0,   0, 1);

	int e[] = { 5, 75, 25 };
	run_test("answer at the end", e, 3, 100, 1, 2);

	return 0;
}
