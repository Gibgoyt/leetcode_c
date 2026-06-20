#include <iostream>
#include <ostream>
#include <string>
#include <cstdint>
#include <vector>

struct Vertex {
	float x;
	float y;
	float z;

	/*
	 *	parametized ctor for the 'Vertex' struct
	*/
	Vertex (
		float in_x,
		float in_y,
		float in_z
	) {
		// c-style approach, explicit, not weird c++ syntax
		this->x = in_x;
		this->y = in_y;
		this->z = in_z;
		printf("parametized ctor called, with x=%f, y=%f, z=%f.\n", this->x, this->y, this->z);
	}

	/*
	 *	copy ctor for 'Vertex' object
	*/
	Vertex (
		const Vertex& vertex
	) {
		this->x = vertex.x;
		this->y = vertex.y;
		this->z = vertex.z;
		printf("copy ctor called, with x=%f, y=%f, z=%f.\n", this->x, this->y, this->z);
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
	printf("before iterating over 'vertices'\n");
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
		printf("right before scope start\n");
		{
			printf("right after scope start\n\n");

			printf("before init std::vector\n");
			std::vector<Vertex> vertices;

			printf("before push_back()\n");
			vertices.push_back({1, 2, 3});

			printf("before print_vertices()\n");
			print_vertices(vertices);

			printf("right before second push_back()\n");
			vertices.push_back(Vertex(4, 5, 6));

			printf("right before scope end\n\n");
		}
		printf("right after scope end\n");
	#endif
	#if defined(BLOCK_2)
		Vertex test = Vertex(1, 2, 3);
		Vertex* test2 = new Vertex(test);
	#endif
	return 0;
}
