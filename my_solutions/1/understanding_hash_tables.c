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
			out_value = p->value;
			return 1;
		}
		return 0;
	}
}

void ht_print (
	HashTable *table
) {
	printf("HashTable size: %d\n", table->size);
	for (int i=0; i<table->size; i++) {
		printf("\tbucket[%d] -> ", i);
		for (HashEntry *p=table->buckets[i]; NULL != p; p->next) {
			printf("(%d=%d) ->", p->key, p->value);
		}
		printf("NULL\n");
	}
}

// free
void ht_free () {} 
