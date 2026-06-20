/*
 *	classes, copy semantics, and move semantics
 *	"how does a class lay out in memory, and what happens when I copy or move one?"
 *
 *	classes vs structs
 *		in C++ they are nearly identical. ONE difference:
 *			struct -> members are 'public' by default
 *			class  -> members are 'private' by default
 *		both can have ctors, dtors, member functions, virtuals, inheritance.
 *		convention: 'struct' for plain data bags, 'class' for things with invariants.
 *
 *	what a class IS in memory
 *		a class is just a struct with rules.
 *		sizeof(T) == sum of non-static data members + padding for alignment.
 *		member functions are NOT stored inside the object — they are normal
 *		functions that secretly take a hidden 'this' pointer as their first arg.
 *		so a class with 10 methods and 1 int member is still 4 bytes.
 *
 *	member access
 *		public:    visible to anyone
 *		private:   visible only to the class itself
 *		protected: visible to the class and its derived classes
 *
 *	the 6 "special member functions" the compiler can synthesize
 *		1. default ctor          T()
 *		2. destructor            ~T()
 *		3. copy ctor             T(const T&)
 *		4. copy assignment       T& operator=(const T&)
 *		5. move ctor             T(T&&)               <- C++11
 *		6. move assignment       T& operator=(T&&)    <- C++11
 *
 *		if your class owns a raw resource (heap, file, socket, CUDA stream),
 *		the compiler's defaults will SHALLOW-copy the handle, which double-frees.
 *		then you must write them yourself: this is the Rule of 5.
 *
 *		Rule of 0: if you don't own raw resources (you compose from vector,
 *		string, unique_ptr), write NONE of these. compiler does the right thing.
 *
 *	lvalue vs rvalue (mini glossary)
 *		lvalue: has a name, has an address.        int x = 5;     x is lvalue
 *		rvalue: temporary, no name, about to die.  5, f(), std::string("hi")
 *
 *		lvalue ref         T&         binds to lvalues only
 *		const lvalue ref   const T&   binds to BOTH (read-only)
 *		rvalue ref         T&&        binds to rvalues only
 *
 *	what does T&& mean as a function parameter?
 *		"I will receive a temporary, and I am allowed to steal its guts
 *		 because nobody will look at it again."
 *
 *	std::move is a CAST, not a verb
 *		std::move(x)  ==  static_cast<T&&>(x)
 *		it does NOT move anything by itself. it RELABELS an lvalue as an
 *		rvalue so overload resolution picks the move ctor/assign.
 *		the real "moving" happens inside the move ctor you write.
 *
 *	moved-from state
 *		after  Buffer b = std::move(a);  the object 'a' is in a
 *		"valid but unspecified" state. you may destroy or reassign it.
 *		do NOT read its value.
 *
 *	the pass-by-value-and-move idiom
 *		void set_name(std::string s) { name_ = std::move(s); }
 *		one function, optimal for both lvalue and rvalue callers.
 *		lvalue caller: pays 1 copy into s, then a cheap move.
 *		rvalue caller: s is built in place (C++17 elision), then a cheap move.
 *
 *	preview: std::forward (next lesson)
 *		same family as std::move, but for template parameters where you
 *		don't know yet if the caller passed an lvalue or an rvalue.
 *		full coverage in the templates lesson.
*/

#include <iostream>
#include <string>
#include <utility>
#include <cstdint>
#include <cstddef>
#include <cstring>

/*
 *	hand-written "owns a heap buffer" class
 *	intentionally uses raw new[]/delete[] so that we can *SEE* the mechanics of copy/move
 *	real code (e.g. std::vector<int>/std::unique_ptr<int[]>) lets the compiler do all 6 for us
*/
class Buffer {
	public:
		// default ctor
		Buffer () : data_(nullptr), size_(0) {
			std::cout << "  [default ctor]\n";
		}

		/*
		 *	parametized ctor
		 *	'explicit' means no implicit conversion of 'size_t' to 'Buffer'
		 *	member init list:
		 *		data_(...)
		 *		size_(n)
		 *		contstructs members directly, instead of default ctor then assign
		*/
		explicit Buffer (
			std::size_t n
		): data_(new int[n]{}), size_(n) {
			std::cout << "  [param ctor, n=" << n << "]\n";
		}

		/*
		 *	copy ctor
		 *	deep copy
		*/
		Buffer (
			const Buffer& other
		) : data_(new int[other.size_]), size_(other.size_) {
			std::memcpy(data_, other.data_, size_ * sizeof(int));
			std::cout << "  [copy ctor, size=" << size_ << "]\n";
		}

		/*
		 *	copy-assign ctor
		 *	free old
		 *	deep copy other
		 *	returns  *this, allows changing a = b = c
		*/
		Buffer& operator = (
			const Buffer& other
		) {
			std::cout << "  [copy assign]\n";
			if (this == &other) {
				// self assign gaurd
				return *this;
			}
			delete[] data_;	// free old buffer
			size_ = other.size_;
			data_ = new int[size_];
			std::memcpy(data_, other.data_, size_ * sizeof(int));
			return *this;
		}

		/*
		 *	move assign ctor
		 *	steal ptr
		 *	null the source
		*/
		Buffer& operator = (
			Buffer&& other
		) noexcept {
			std::cout << "  [move assign]\n";
			if (this == &other) {
				return *this;
			}
			delete[] data_;
			data_ = other.data_;
			size_ = other.size_;
			other.data_ = nullptr;
			other.size_ = 0;
			
			return *this;
		}

		/*
		 *	move ctor
		 *	steal the ptr, null the source
		 *	'noexcept' is critical
		 *		std::vector will only use your move ctor when reallocating IF it is noexcept
		 *		otherwise it copies
		*/
		Buffer (
			Buffer &&other
		) noexcept : data_(other.data_), size_(other.size_) {
			other.data_ = nullptr;
			other.size_ = 0;
			std::cout << "  [move ctor]\n";
		}

		/*
		 *	dtor
		 *	delete heap buffer
		 *	delete[] on nullptr is no op/safe
		*/
		~Buffer () {
			std::cout << "  [dtor, size=" << size_ << "]\n";
			delete[] data_;
		}

		// accessors
		std::size_t size () const {
			return size_;
		}

		int& operator[] (
			std::size_t i
		) {
			return data_[i];
		}

		const int& operator[] (
			std::size_t i
		) const {
			return data_[i];
		}

	private:
		int* data_ = nullptr;
		std::size_t size_ = 0;
};

/*
 *	pass by value + std::move idiom
 *		lvalue caller:
 *			copy ctor builds s, and then
 *			move ctor builds local
 *		rvalue caller:
 *			s is ctor inplace (C++17 mandatory), and then
 *			move ctor builds local
 *			zero copies
 *
*/
void set_and_print (
	Buffer s
) {
	Buffer local = std::move(s);
	std::cout << "  set_and_print: local.size=" << local.size() << "\n";
}

int main () {
	std::cout << "sizeof(Buffer) = " << sizeof(Buffer) << "\n";
	std::cout << "  (sizeof(int*) + sizeof(size_t), plus any padding)\n\n";

	std::cout << "--- 1. default ctor ---\n";
	{
		Buffer a;
		//	@see main.cpp:87-90
	}
	std::cout << "\n";

	std::cout << "--- 2. param ctor + operator[] ---\n";
	{
		Buffer a(4);
		for (std::size_t i=0; i<a.size(); ++i) {
			a[i] = static_cast<int>(i * i);
		}
		std::cout << "  a[3] = " << a[3] << "\n";
	}
	std::cout << "\n";

	std::cout << "--- 3. copy ctor (deep copy) ---\n";
	{
		Buffer a(3);
		a[0] = 7; 
		a[1] = 8; 
		a[2] = 9;

		Buffer b = a;
		b[0] = 99;
		std::cout << "  a[0]=" << a[0] << " b[0]=" << b[0] << "\n";
		std::cout << "  (independent buffers — deep copy worked)\n";
	}
	std::cout << "\n";

	std::cout << "--- 4. copy assignment ---\n";
	{
		Buffer a(2);
		Buffer b(5);

		b = a;		// copy assign: frees b's 5, deep-copies a


		std::cout << "  b.size=" << b.size() << "\n";
	}
	std::cout << "\n";

	std::cout << "--- 5. move ctor (via std::move) ---\n";
	{
		Buffer a(3);
		a[0] = 1; 
		a[1] = 2; 
		a[2] = 3;

		Buffer b = std::move(a);	// move ctor, steals a's ptr

		std::cout << "  b.size=" << b.size() << "\n";
		std::cout << "  a.size=" << a.size() << "\n";
		std::cout << "  (a is moved-from — empty)\n" << "\n";
	}
	std::cout << "\n";

	std::cout << "--- 6. move assignment ---\n";
	{
		Buffer a(4);
		Buffer b;

		b = std::move(a);
		std::cout << "  b.size=" << b.size() << "\n";
		std::cout << "  a.size=" << a.size() << "\n";
	}

	std::cout << "\n";

	std::cout << "--- 7. pass-by-value + std::move idiom ---\n";
	{
		Buffer a(2);
		std::cout << "  passing lvalue (expect copy ctor + move ctor):\n";
		set_and_print(a);
		std::cout << "  passing rvalue (expect param ctor + move ctor, no copy):\n";
		set_and_print(Buffer(2));
	}
	std::cout << "\n";

	std::cout << "--- 8. self-assignment guard ---\n";
	{
		Buffer a(3);
		a = a;                        // copy assign returns early
		a = std::move(a);             // move assign returns early
		std::cout << "  a.size=" << a.size() << "\n";
		std::cout << " (unchanged, no double-free)\n";
	}
	std::cout << "\n";
}
