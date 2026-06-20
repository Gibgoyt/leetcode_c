#include <iostream>
#include <ostream>
#include <string>
#include <cstdint>
#include <vector>

struct Vertex {
	float x;
	float y;
	float z;

	Vertex (
		float x,
		float y,
		float z
	) : x(x), y(y), z(z) {
		printf("parametized ctor called, with x=%f, y=%f, z=%f.\n", x, y, z);
	}
};

std::ostream& operator<<(
	std::ostream& stream,
	const Vertex& vertex
) {
	stream << vertex.x << ", " << vertex.y << ", " << vertex.z;
	return stream;
}

void print_vertices (
	const std::vector<Vertex>& vertices
) {
	for (
		std::uint32_t i=0;
		i<vertices.size();
		i++
	) {
		printf("vertices[i]=%f, %f, %f\n", vertices[i].x, vertices[i].y, vertices[i].z);
	}
}

int main () {
	#if defined(BLOCK_0)
		{
			float x;
			printf("sizeof(float x)=%d\n", sizeof(x));
		}
	#endif
	#if defined(BLOCK_1)
		{
			std::vector<Vertex> vertices;
			vertices.push_back({1, 2, 3});
			print_vertices(vertices);
		}
	#endif
	return 0;
}
