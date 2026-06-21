/*
 *	naive shared ptr
 *	2 ptrs on the stack
 *		SharedPtr<T* ptr_; long* refcount_>
 *	every SharedPtr<T>(new T(...)) does TWO heap allocations:
 *		- `T` itself
 *		- `long` refcount living off to the side
 *	copy ctor bumps refcount_
 *	dtor decrements
 *	hits 0, then delete both
 *	no weak_ptr
 *	no custom deleters
 *	no make_shared
 *	no atomics
 *	no aliasing
*/

#include <iostream>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <utility>

/*
 *  depth-aware logging -- copied verbatim in spirit from dynamic_array/2/main.cpp.
 *  every SharedPtr method that wraps a private helper bumps g_log_indent on entry
 *  and drops it on exit. helpers (release) print at the bumped depth so the
 *  destruction cascade indents readably.
*/
static int g_log_indent = 0;
static void log_indent () {
	for (int i = 0; i < g_log_indent; i++) putchar('\t');
}

struct Tracer {};

template <typename T>
	class SharedPts {};

int main () {
	#if defined(BLOCK_1)
		printf("Hello World\n");
	#endif
}
