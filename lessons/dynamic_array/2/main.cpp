/*
 *	here we will try create our own dynamic heap contiguous buffer class
 *	similar to std::vector
*/

#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cstddef>

struct Vertex {
	float x;
	float y;
	float z;

	/*
	 *	parametized ctor
	 *	initialization list to avoid copying
	*/
	Vertex (
		float in_x,
		float in_y,
		float in_z
	) : x(in_x), y(in_y), z(in_z) {
		printf("parametized ctor at %p, with x=%f, y=%f, z=%f.\n", (const void*)this, this->x, this->y, this->z);
	}

	/*
	 *	copy ctor
	*/
	Vertex (
		const Vertex& vertex
	) : x(vertex.x), y(vertex.y), z(vertex.z) {
		printf(
			"copy ctor: from %p -> to %p, with x=%f, y=%f, z=%f.\n", 
			(const void*)&vertex, (const void*)this, this->x, this->y, this->z
		);
	}
};

int main () {
	std::cout << "Hello World!\n";

	#define BLOCK_0
	#if defined(BLOCK_0)
		/*
		 *	fixed-size stack buffer of 'Vertex' struct
		 *	expected size 12 bytes
		*/
		printf("sizeof(Vertex)=%zu\n", sizeof(Vertex));

		printf("entering scope, declaring `Vertex vertices[3];` on the stack");
		{
			Vertex vertices[3] = {
				Vertex(1, 2, 3),
				Vertex(4, 5, 6),
				Vertex(6, 7, 8)
			};
			printf("\n");

			printf("sizeof(vertices)=%zu\n", sizeof(vertices));
			printf("&vertices=%p\n", (const void*)&vertices);
			printf("&vertices[0]=%p\n", (const void*)&vertices[0]);
			printf("delta from &vertices[0]: %td\n", (const char*)&vertices[0] - (const char*)&vertices[0]);
			printf("&vertices[1]=%p\n", (const void*)&vertices[1]);
			printf("delta from &vertices[1]: %td\n", (const char*)&vertices[1] - (const char*)&vertices[0]);
			printf("&vertices[2]=%p\n", (const void*)&vertices[2]);
			printf("delta from &vertices[2]: %td\n", (const char*)&vertices[2] - (const char*)&vertices[0]);

			printf("\n");

			for (
				std::uint32_t i=0;
				i<3;
				i++
			) {
				printf(
					"vertices[%u] = %.1f, %.1f, %.1f\n", 
					i, vertices[i].x, vertices[i].y, vertices[i].z
				);
			}
			printf("\n");

		printf("scope is about to end\n");
		}
		#endif
}
