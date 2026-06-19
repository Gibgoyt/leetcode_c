# Walkthrough

## Entire Program Stdout

```text
sizeof(Buffer) = 16
  (sizeof(int*) + sizeof(size_t), plus any padding)

--- 1. default ctor ---
  [default ctor]
  [dtor, size=0]

--- 2. param ctor + operator[] ---
  [param ctor, n=4]
  a[3] = 9
  [dtor, size=4]

--- 3. copy ctor (deep copy) ---
  [param ctor, n=3]
  [copy ctor, size=3]
  a[0]=7 b[0]=99
  (independent buffers — deep copy worked)
  [dtor, size=3]
  [dtor, size=3]

--- 4. copy assignment ---
  [param ctor, n=2]
  [param ctor, n=5]
  [copy assign]
  b.size=2
  [dtor, size=2]
  [dtor, size=2]

--- 5. move ctor (via std::move) ---
  [param ctor, n=3]
  [move ctor]
  b.size=3
  a.size=0
  (a is moved-from — empty)

  [dtor, size=3]
  [dtor, size=0]

--- 6. move assignment ---
  [param ctor, n=4]
  [default ctor]
  [move assign]
  b.size=4
  a.size=0
  [dtor, size=4]
  [dtor, size=0]

--- 7. pass-by-value + std::move idiom ---
  [param ctor, n=2]
  passing lvalue (expect copy ctor + move ctor):
  [copy ctor, size=2]
  [move ctor]
  set_and_print: local.size=2
  [dtor, size=2]
  [dtor, size=0]
  passing rvalue (expect param ctor + move ctor, no copy):
  [param ctor, n=2]
  [move ctor]
  set_and_print: local.size=2
  [dtor, size=2]
  [dtor, size=0]
  [dtor, size=2]

--- 8. self-assignment guard ---
  [param ctor, n=3]
  [copy assign]
  [move assign]
  a.size=3
 (unchanged, no double-free)
  [dtor, size=3]

```

## Default ctor

Inside main(), we have this

```c++
std::cout << "--- 1. default ctor ---\n";
{
    Buffer a;
    //      @see main.cpp:87-90
}
std::cout << "\n";
```

this calls the default ctor

```c++
// default ctor
Buffer () : data_(nullptr), size_(0) {
    std::cout << "  [default ctor]\n";
}
```

hence the stdout 

```text
[default ctor]
```

at scope close (i.e. `}`), the dtor is called since the reference to a is dropped
I think this is why dtor is called??
Not sure what the entire struct of `Buffer` looks like and if it actually C++ ref (i.e. `&`) that gets dropped calling dtor or what

```c++
/*
 *	dtor
 *	delete heap buffer
 *	delete[] on nullptr is no op/safe
*/
~Buffer () {
	std::cout << "  [dtor, size=" << size_ << "]\n";
	delete[] data_;
}
```

hence the stdout
```text
[dtor, size=0]
```

## Param Ctor

In main() we have

```c++
std::cout << "--- 2. param ctor + operator[] ---\n";
{
	Buffer a(4);
	for (std::size_t i=0; i<a.size(); ++i) {
		a[i] = static_cast<int>(i * i);
	}
	std::cout << "  a[3] = " << a[3] << "\n";
}
std::cout << "\n";
```

where we call this ctor:

```c++
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
```

hence giving us the stdout

```text
[param ctor, n=4]
```

after ctor, we then map through the entire Buffer{} object

```c++
for (std::size_t i=0; i<a.size(); ++i) {
	a[i] = static_cast<int>(i * i);
}
std::cout << "  a[3] = " << a[3] << "\n";
```

placing into a[i] its square (e.g. a[0] = 0, a[1]=1, a[2]=4, etc...), and hence the stdout:
```text
a[3] = 9
```

and then at `{` in main, the ref to `a`is dropped, and its dtor is called
```c++
/*
 *	dtor
 *	delete heap buffer
 *	delete[] on nullptr is no op/safe
*/
~Buffer () {
	std::cout << "  [dtor, size=" << size_ << "]\n";
	delete[] data_;
}
```

and hence the stdout
```text
[dtor, size=4]
```

so to summarize, the parametized Buffer{} ctor opened a buffer of size 4,
and then we mapped through it, until its end `a.size()`, palcing into each `a[i]` the value `i²`

## Copy Ctor

This forms a deep copy, for all elements inside the original `a` object, create the `b` object
```c++
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
```

giving a stdout
```text
--- 3. copy ctor (deep copy) ---
  [param ctor, n=3]
  [copy ctor, size=3]
  a[0]=7 b[0]=99
  (independent buffers — deep copy worked)
  [dtor, size=3]
  [dtor, size=3]
```

```c++
Buffer a(3);
```
calls this ctor:

```c++
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
```

while the next piece of code `Buffer b = a` somehow calls this

```c+++
/*
 *	copy ctor
 *	deep copy
*/
Buffer (
	const Buffer& other
) : data_(new int[other.size_]), size_(other.size_) {
	for (std::size_t i=0; i<size_; ++i) {
		data_[i] = other.data_[i];
	}
	std::cout << "  [copy ctor, size=" << size_ << "]\n";
}
```

although here I am very confused, would we not have to do this to call this ctor:
```c++
Buffer a(4);
Buffer b(*a);
```

or does g++ see `b = a` and fill in, `*a` is a ptr to `Buffer` object
please properly explain what the fuck is going on here

## Copy-Assign Ctor

```c++
std::cout << "--- 4. copy assignment ---\n";
{
	Buffer a(2);
	Buffer b(5);

	b = a;		// copy assign: frees b's 5, deep-copies a

	std::cout << "  b.size=" << b.size() << "\n";
}
std::cout << "\n";
```

this entire function gives this ctor:
```text
--- 4. copy assignment ---
  [param ctor, n=2]
  [param ctor, n=5]
  [copy assign]
  b.size=2
  [dtor, size=2]
  [dtor, size=2]
```

both assignments call Buffer{} parametized ctor
but then at 
```c++
b = a;
```

this Buffer{} code is called:
```c++
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
	for (std::size_t i=0; i<size_; ++i) {
		data_[i] = other.data_[i];
	}
	return *this;
}

by its function
```

what the heck is `Buffer& operator = () {}`??? please explain this properly

## Move Assign

inside main() we have this
```c++
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
```

giving us this stdout:

```text
--- 5. move ctor (via std::move) ---
  [param ctor, n=3]
  [move ctor]
  b.size=3
  a.size=0
  (a is moved-from — empty)

  [dtor, size=3]
  [dtor, size=0]
```

Buffer{} move assign ctor

```c++
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
```

this makes sense as g++ sees
```c++
Buffer dst = std::move(src);
```

but how the fuck does the function signature make c++ do this:
```c++
class Buffer {
    Buffer& operator = (
        Buffer &&dst
    ); 
}
```

make `std::move` know to call this type of ctor??

## Move Assign

inside main() we have
```c++
	std::cout << "--- 6. move assignment ---\n";
	{
		Buffer a(4);
		Buffer b;

		b = std::move(a);
		std::cout << "  b.size=" << b.size() << "\n";
		std::cout << "  a.size=" << a.size() << "\n";
	}

	std::cout << "\n";
```

giving us this stdout
```text
--- 6. move assignment ---
  [param ctor, n=4]
  [default ctor]
  [move assign]
  b.size=4
  a.size=0
  [dtor, size=4]
  [dtor, size=0]
```

move assign means both `src` and `dst` are already existing,
we are just moving `src` to `dst`, and freeing `src`

here is the move assign ctor for Buffer{}

```c++
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
```
but how the fuck does `std::move` know when to call this
and one further! how the fuck does `std::move` know move assign or pure move ctor
does it use move assign if `Buffer dst` is created with `std::move`???

## Copy by value
```c++
	std::cout << "--- 7. pass-by-value + std::move idiom ---\n";
	{
		Buffer a(2);
		std::cout << "  passing lvalue (expect copy ctor + move ctor):\n";
		set_and_print(a);
		std::cout << "  passing rvalue (expect param ctor + move ctor, no copy):\n";
		set_and_print(Buffer(2));
	}
	std::cout << "\n";
```
giving stdout
```text
--- 7. pass-by-value + std::move idiom ---
  [param ctor, n=2]
  passing lvalue (expect copy ctor + move ctor):
  [copy ctor, size=2]
  [move ctor]
  set_and_print: local.size=2
  [dtor, size=2]
  [dtor, size=0]
  passing rvalue (expect param ctor + move ctor, no copy):
  [param ctor, n=2]
  [move ctor]
  set_and_print: local.size=2
  [dtor, size=2]
  [dtor, size=0]
  [dtor, size=2]
```

`Buffer a(2);` calls paramatized ctor
`set_and_print(a);` somehow outputs:
```text
  [copy ctor, size=2]
  [move ctor]
  set_and_print: local.size=2
  [dtor, size=2]
  [dtor, size=0]

```

coming from here
```c++
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
```

which appears to call
```c++
		Buffer (
			const Buffer& other
		) : data_(new int[other.size_]), size_(other.size_) {
			for (std::size_t i=0; i<size_; ++i) {
				data_[i] = other.data_[i];
			}
			std::cout << "  [copy ctor, size=" << size_ << "]\n";
		}
```
and then call
```c++
		Buffer (
			Buffer &&other
		) noexcept : data_(other.data_), size_(other.size_) {
			other.data_ = nullptr;
			other.size_ = 0;
			std::cout << "  [move ctor]\n";
		}
```
what the fuck?? why the fuck does it call copy ctor then move ctor???
and the appears to destruct both of them?? because exiting the function appears to drop the ref???
therefore ~Buffer{}???
and calling `set_and_print(Buffer(2));` stdouts
```text
  [param ctor, n=2]
  [move ctor]
  set_and_print: local.size=2
```
why the fuck does it call param ctor before move ctor??
