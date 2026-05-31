#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/*
 *	error semantics
 *	NeetCode.IO requires function signature bool isAnagram(char*, char*)
 *	so there is no error code channel
 *	every non-anagram outcome (e.g. length mismatch, OOR char, NULL input) all will collapse to false
*/

#define INVARIANT(cond) do { if (!(cond)) { __builtin_trap(); } } while (0)

/*
 *	constraints
 *	STR_MAX
 *	ALPHABET_SIZE
 *	CAP
 *	SENTINEL_EMPTY
*/
#if !defined(STR_MAX)
	#define STR_MAX			((size_t)  50000)
#endif
#if !defined(ALPHABET_SIZE)
	#define ALPHABET_SIZE		((size_t) 26)
#endif
#if !defined(CAP)
	#define CAP			((size_t) 64)
#endif
#if !defined(SENTINEL_EMPTY)
	#define SENTINEL_EMPTY		INT32_MIN
#endif

_Static_assert(
	0 == (CAP & (CAP -1)),
	"Cap must be pow2"
);
_Static_assert(
	CAP >= 2 *ALPHABET_SIZE,
	"cap must hold the alphabet at load factor < 0.5"
);

/*
 *	one open-addressed slot
 *
 *	key the char (lifted to int32_t) or SENTINEL_EMPTY if unoccupied
 *	freq signed net count: 	increment for each occurence in s
 *				decrement for each occurence in t
 *	anagram <=> every occupied slot at freq == 0
 *
 *	keeping key + freq adjacent in 1 struct gives the linear probe a good cache locality compared to 2 parallel arrays
*/

typedef struct {
	int32_t key;
	int32_t freq;
} HashEntry;

bool isAnagram(
	const char* s, 
	const char* t
);

static void test_harness (
	const char *name,
	const char *s,
	const char *t,
	bool expected
);

bool isAnagram(
	const char* s, 
	const char* t
) {
	bool ret = true;

	/*
	 *	NULL inputs are outside the problem's input domain
	 *	collapse to false instead of dereferencing
	*/
	if (
		NULL == s ||
		NULL == t
	) {
		ret = false;
		goto out;
	}

	size_t s_len = strlen(s);
	size_t t_len = strlen(t);

	/*
	 *	length mismatch can not be an anagram
	 *	doing this upfront lets main() walk both strings under a single index without per-iteration bounds checking
	*/
	if (s_len != t_len) {
		ret = false;
		goto out;
	}

	/*
	 *	oversized length violates the stated constraints (i.e. at Question.md)
	 *	refuse, rather than walk unbounded memory
	*/
	if (s_len > STR_MAX) {
		ret = false;
		goto out;
	}

	/*
	 *	stack-allocated open-address hash table
	 *	CAP is a compile time constant, so for loop has a definite end
	 *
	 *	0 set the entire table
	*/
	HashEntry table[CAP];
	for (
		size_t i=0; 
		i<CAP; 
		i++
	) {
		table[i].key = SENTINEL_EMPTY;
		table[i].freq = 0;
	}

	/*
	 *	fused pass
	 *	for each i in [0, s_len), bump table[s[i]]++ and table[t[i]]--
	 *	after the pass, after the pass every slot must net to 0 after the pass :))) (i.e. proving an anagram)
	 *
	 *	loop bound = s_len == t_len (i.e. has a definite end :)))
	*/
	for (
		size_t i=0;
		i<s_len;
		i++
	) {
		unsigned char c_s = (unsigned char) s[i];
		unsigned char c_t = (unsigned char) t[i];

		/*
		 *	range check as per Question.md (i.e. lowercase eng/latin letters)
		 *	OOR means char is not valid, so can not be anagram
		*/
		if (
			c_s < 'a' ||
			c_s > 'z' || 
			c_t < 'a' ||
			c_t > 'z'
		) {
			ret = false;
			goto out;
		}

		/*
		 *	Knuth multiplicative hash
		 *	masked to CAP
		 *	cast to uint32_t making the multiply well defined (signed overflow will be undefined behavior)
		*/
		size_t base_s = (size_t)(
			(uint32_t) c_s * 2654435769u
		) & (CAP - 1);
		size_t base_t = (size_t)(
			(uint32_t) c_t * 2654435769u
		) & (CAP - 1);
		
		/*
		 *	insert-or-bump s[i]:
		 *		linear probe
		 *		definite bound CAP
		 *
		 *	terminating cases:
		 *		empty slot: claim it + freq = 1
		 *		matching key: freq ++
		 *
		 *	probe exhaustion would mean that the load factor invariant broke -> trap
		 *	(i.e. a bug in the file and not in the input)
		*/
		bool placed_s = false;
		for (
			size_t p=0;
			p<CAP;
			p++
		) {
			size_t h = (base_s + p) & (CAP - 1);
			if (SENTINEL_EMPTY == table[h].key) {
				table[h].key = (int32_t) c_s;
				table[h].freq = 1;
				placed_s = true;
				break;
			}

			if ((int32_t) c_s == table[h].key) {
				table[h].freq++;
				placed_s = true;
				break;
			}
		}
		INVARIANT(placed_s);

		/*
		 *	insert or dump for t[i]
		 *	same probe shape
		 *	if t[i] has not been seen yet then we still claim a slot with freq--
		 *	the final sweep will catch any imbalances
		*/
		bool placed_t = false;
		for (
			size_t p=0;
			p<CAP;
			p++
		) {
			size_t h = (base_t + p) & (CAP - 1);
			if (SENTINEL_EMPTY == table[h].key) {
				table[h].key = (int32_t) c_t;
				table[h].freq = -1;
				placed_t = true;
				break;
			}

			if ((int32_t) c_t == table[h].key) {
				table[h].freq--;
				placed_t = true;
				break;
			}
		}
		INVARIANT(placed_t);
	}

	/*
	 *	verify if every slot nets to 0
	 *	empty slots were init to freq=0
	 *	a single sweep over all CAP slots covers both occupied + unoccupied
	 *	loop bound CAP (i.e. definite end)
	*/

	ret = true;
	for (
		size_t i=0;
		i<CAP;
		i++
	) {
		if (0 != table[i].freq) {
			ret = false;
			break;
		}
	}

	out:
		return ret;
}

static void test_harness (
	const char *name,
	const char *s,
	const char *t,
	bool expected
) {
	printf("Test: %s. Got: %d. Expected: %d\n", name, isAnagram(s, t), expected);
}

int main () {
	test_harness("Example 1", "racecar", "carrace", 1);
	test_harness("Example 2", "jar", "jam", 0);

	return 0;
}
