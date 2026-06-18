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
	return 0;
}
