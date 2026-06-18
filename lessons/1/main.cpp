#include <iostream>
#include <ostream>
#include <vector>
#include <cstdint>

#if defined(PRINT)
	#include <print>
#endif

int main () {
	#if defined(PRINT)
		// TODO!!: fix nvim LSP warning No member named 'println' in namespace 'std'; did you mean 'printf'? (fix available)
		std::println("hello, world");
	#endif
	std::cout << "Hello World" << std::endl;

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

	#if defined(PRINT)
		std::println("name={}, count={}, big={}, length={}", name, count, big, length);
	#endif

	std::cout << "name=" << name << ", count=" << count << ", big=" << big << ", length=" << length << "\n";

	return 0;
}
