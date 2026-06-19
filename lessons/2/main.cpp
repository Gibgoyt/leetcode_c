/*
 *	values, references, and ptr passing
 *	"should I pass this by value, by const-ref, by ref, or by pointer?"
 *
 *	3 kinds of things
 *		```
 *		int x = 42;	// value, owns the bits, lives where declared
 *		int& r = x;	// reference, alias for x, must bind on init/ctor, can *NOT* be null, can *NOT* rebind
 *		int* p = &x;	// pointer, address of x, can be null, can be re-assgined, can also dangle
 *		```
 *
 *		- value 
 *		  has lifetime
 *		  has location (stack, heap, static)
 *		- reference
 *		  syntax sugar for ptr that compiler guarantees will never break
 *		  must *ALWAYS* point to a real thing, no null reference
 *		- pointer
 *		  int which happens to be address
 *		  compiler gives us no help, we control whether it points to something live
 *
 *	stack vs. heap
 *
 *		```auto* heap = new int(2);``` is similar to ```int *heap = (int*)malloc(sizeof(int*));```
 *		'new' is like 'malloc()'
 *
 *	'const' means immutability
 *	idk what 'constexpr' is
 *
 *	function signatures
 *		1. pass by value
 *			```
 *			void f (T x)
 *			```
 *			one copy
 *		2. const reference
 *			```
 *			void f (const T& x)
 *			```
 *			zero copy, read only
 *		3. mutable reference
 *			```
 *			void F (T& x)
 *			```
 *			zero copy, read/write
 *		4. pointer
 *			```
 *			void F (T* x) 
 *			```
 *			zero copy, can be null
*/

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

/*
 *	function headers/declarations for fun
*/

void by_value (
	std::string s
);
void by_const_ref (
	const std::string& s
);
void by_ref (
	std::string& s
);
void by_ptr (
	std::string* s
);

/*
 *	function by value
 *	one copy, hence bad for big things
*/
void by_value (
	std::string s
) {
	s += "!";
}

/*
 *	by 'const' ref
 *	zero copy, read-only
*/
void by_const_ref (
	const std::string& s
) {
	std::cout << s.size() << "\n";
}

/*
 *	by mutable ref
 *	zero copy, read/writre
*/
void by_ref (
	std::string& s
) {
	s += "!";
}

/*
 *	by ptr
 *	may be null, just like C here
*/
void by_ptr (
	std::string* s
) {
	if (s) {
		*s += "!";
	}
}

int main () {
	std::string name = "Ahmed";
	by_const_ref(name);
	by_ref(name);
	std::cout << "after by_ref(): " << name << "\n";
	by_value(name);
	std::cout << "after by_value(): " << name << "\n";
	by_ptr(&name);
	by_ptr(nullptr);
	std::cout << "after by_ptr(): " << name << "\n";
	
	// const reading drill
	const int   a = 1;
	const int*  p = &a;        // *p read-only, p reassignable
	int         x = 9;
	int* const  q = &x;        // *q writable, q frozen
	// p = &x;                 // OK
	// *p = 5;                 // ERROR
	// q = &a;                 // ERROR
	*q = 5;                    // OK
	std::cout << "x=" << x << "\n";

	// auto deduction drill
	std::vector<std::string> v = {"alpha", "beta", "gamma"};
	auto        copy  = v[0];	// COPY
	auto&       alias = v[1];	// ALIAS — mutate-able
	const auto& view  = v[2];	// READ-ONLY ALIAS

	alias += "_changed";		// v[1] is now "beta_changed"
	std::cout << v[1] << "\n";

	for (const auto& s : v) {
		std::cout << s << " ";
	}
	std::cout << "\n";

	return 0;
}
