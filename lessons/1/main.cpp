#include <iostream>
#include <ostream>
#include <vector>
#include <cstdint>

#if defined(USE_PRINT)
	#include <print>
#endif

int main () {
	#if defined(USE_PRINT)
		// TODO!!: fix nvim LSP warning No member named 'println' in namespace 'std'; did you mean 'printf'? (fix available)
		std::println("hello, world");
	#else
		std::cout << "Hello World" << std::endl;
	#endif

	std::uint32_t count = 42;
	std::uint64_t big = 1'000'000'000ULL;
	std::size_t length = 0;

	/*
	 *	RAII containers
	 *	owns its own memory
	 *	and frees automatically
	*/
	std::string name = "Ahmed";
	std::vector<std::uint32_t> xs = {1, 2, 3, 4, 5};
	length = xs.size();

	#if defined(USE_PRINT)
		std::println("name={}, count={}, big={}, length={}", name, count, big, length);
	#else
		std::cout << "name=" << name << ", count=" << count << ", big=" << big << ", length=" << length << "\n";
	#endif

	return 0;
}
