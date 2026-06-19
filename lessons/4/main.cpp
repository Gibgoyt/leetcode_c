/*
 *	bare minimum
 *	default ctor/dtor
 *	see what a c++ object is in memory
 *	where it lives
 *	and what ctor/dtor ran
 *	compared to writing in C by hand
*/

#include <iostream>
#include <cstddef>

class Buffer {
	private:
		int* data_;
		std::size_t size_;

	public:
		/*
		 *	default ctor
		 *	takes no args
		 *	fills 'this' with safe defaults
		*/
		Buffer () : data_(nullptr), size_(0) {
			std::cout << "  [default ctor]"
				<< "  this=" << this
				<< "  data_=" << data_
				<< "  size_=" << size_ 
				<< "\n";
		}

		/*
		 *	dtor
		 *	runs auto when object's scope ends
		*/
		~Buffer () {
			std::cout << "  [dtor]"
				<< "  this=" << this
				<< "  data_=" << data_
				<< "  size_=" << size_ 
				<< "\n";
		}
};

int main () {
	std::cout << "sizeof(Buffer) = " << sizeof(Buffer) << " bytes\n";
	std::cout << "sizeof(int*)   = " << sizeof(int*) << "\n";
	std::cout << "sizeof(size_t) = " << sizeof(std::size_t) << "\n\n";

	std::cout << "before outer block, &main stack frame is around " << &main << "\n";

	#if defined(BLOCK_1)
		std::cout << "\n--- block 1 ---\n";
		{
			Buffer a;
			std::cout << "  &a = " << &a << " (this is where a's 16 bytes live)\n";
		}
	#endif

	#if defined(BLOCK_2)
		std::cout << "\n--- block 2: two objects, dtor order ---\n";
		{
			Buffer x;
			Buffer y;
	
			std::cout << "  &x = " << &x << "\n";
			std::cout << "  &y = " << &y << "\n";
			std::cout << "  (expect dtors in REVERSE order: y first, then x)\n";
		}
	#endif

	#if defined(BLOCK_3)
		{
			Buffer outer;
			{
				Buffer inner;
				std::cout << "  inner block ending now...\n";
			}
			std::cout << "  inner is already dead. outer still alive.\n";
		}
	#endif

	std::cout << "\nend of main\n";
}
