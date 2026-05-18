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

int main () {}
