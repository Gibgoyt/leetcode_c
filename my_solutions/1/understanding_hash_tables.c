/*
 *	2026-05-18T09:44:28.905Z Ahmed Moti
 *
 *	Understanding hash tables, stage C of TwoSum (i.e. leetcode question 1)
*/

#include <stdio.h>
#include <stdlib.h>

/*
 * 1 entry table, 'next' links to next item
*/
typedef struct HashEntry {
	int key;
	int value;
	struct HashEntry *next;
} HashEntry;

// table owns array of all pointers and size of the array
typedef struct {
	HashEntry **buckets;
	int size;
} HashTable;

/*
 *	@brief		-	squashes any int into [0,size)
 *
*/
static int hash (
	int key,
	int size
) {
	return (int)(
		(unsigned int)key % (unsigned int)size
	);
}

HashTable *ht_create (
		int size
) {
	HashTable *table = malloc(sizeof(*table));
	table->size = size;

	// calloc zeroing mem such that every bucket starts at 0
	table->buckets = calloc(size, sizeof(HashEntry*));

	return table;
}

// insert (key,value), if 'key' already exists then overwrite its value
void ht_put (
	HashTable *table,
	int key,
	int value
) {
	int idx = hash(key, table->size);
	printf("ht_put() into table %p, key: %d, value: %d, idx: %d\n", table, key, value, idx);

	// walk through chain at this bucket (is key already here??)
	for (HashEntry *p=table->buckets[idx]; NULL != p; p=p->next) {
		if (key == p->key) {
			p->value = value;
			return;
		}
	}

	// if not present then prepend node to chain head
	HashEntry *node = malloc(sizeof(*node));
	node->key = key;
	node->value = value;
	node->next = table->buckets[idx];
	table->buckets[idx] = node;
}

// lookup 'key' @return 1 on success and 0 on not found
int ht_get (
	HashTable *table,
	int key,
	int *out_value
) {
	int idx = hash(key, table->size);
	for (HashEntry *p=table->buckets[idx]; NULL != p; p=p->next) {
		if (key == p->key) {
			*out_value = p->value;
			return 1;
		}
	}
	return 0;
}

void ht_print (
	HashTable *table
) {
	printf("HashTable size: %d\n", table->size);
	for (int i=0; i<table->size; i++) {
		printf("\tbucket[%d] -> ", i);
		for (HashEntry *p=table->buckets[i]; NULL != p; p=p->next) {
			printf("(%d=%d) ->", p->key, p->value);
		}
		printf("NULL\n");
	}
}

// free
void ht_free (
	HashTable *table
) {
	// walk every chain and free every node, then free bucket array, then table struct itself
	for (int i=0; i<table->size; i++) {
		HashEntry *p = table->buckets[i];
		while (NULL !=p) {
			HashEntry *next = p->next;
			free(p);
			p = next;
		}
	}
	free(table->buckets);
	free(table);
} 

int main (
	void
) {
	// deliberately small table
	HashTable *table = ht_create(10);

	ht_put(table, 3, 100);
	ht_put(table, 10, 200);
	ht_put(table, 17, 300);
	ht_put(table, 24, 400);
	ht_put(table, 4, 500);
	ht_put(table, 5, 600);
	ht_put(table, 6, 700);
	ht_put(table, 1, 700);
	ht_put(table, 2, 700);
	ht_put(table, 12, 700);

	printf("After 10 inserts\n");
	ht_print(table);

	printf("\n\n\n");
	printf("Overwriting 3->100 key to 105\n");
	ht_put(table, 3, 105);

	printf("\n\n\n");
	int v;
	printf("now doing lookups\n");
	if (ht_get(table, 10, &v)) {
		printf("key 10 -> %d\n", v);
	} else {
		printf("key 10 miss");
	}

	if (ht_get(table, 17, &v)) {
		printf("key 17 -> %d\n", v);
	} else {
		printf("key 17 miss\n");
	}

	if (ht_get(table, 9, &v)) {
		printf("key 9 -> %d\n", v);
	} else {
		printf("key 9 miss\n");
	}

	ht_print(table);
	ht_free(table);
	return 0;
}
