#include <iostream>
#include <ostream>
#include <string>

struct Vertex {
	float x;
	float y;
	float z;
};

std::ostream& operator<<(
	std::ostream& stream,
	const Vertex& vertex
) {
	stream << vertex.x << ", " << vertex.y << ", " << vertex.z;
	return stream;
}

int main () {
	std::cin.get();
	return 0;
}
