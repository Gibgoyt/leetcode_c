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
		printf("parametized ctor at %p, with x=%f, y=%f, z=%f.\n", (const void*)this, this->x, this->y, this->z);
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
		printf("copy ctor: from %p -> to %p, with x=%f, y=%f, z=%f.\n", (const void*)&vertex, (const void*)this, this->x, this->y, this->z);
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

/*
 *	dumps size, capacity, and underlying heap buffer ptr for a std::vector<Vertex>
 *	used to make heap reallocation on push_back/emplace_back visible:
 *		when 'data' changes between calls, std::vector has alloc'd a new heap buffer
 *		and copy-ctor'd every existing element from old -> new
*/
void print_vector_state (
	const char* label,
	const std::vector<Vertex>& v
) {
	printf("  [%s] size=%zu, capacity=%zu, data=%p\n",
		label, v.size(), v.capacity(), (const void*)v.data());
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

			int n = 2;
			printf("reserving capacity=%d for 'vertices' vector\n", n);
			vertices.reserve(2);

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
	#if defined(BLOCK_3)
		/*
		 *	BLOCK_3 -- end-to-end std::vector walkthrough
		 *
		 *	goal: make EVERY copy ctor and heap (re)allocation visible.
		 *	with the enhanced copy ctor logging 'from %p -> to %p' the heap-to-heap
		 *	moves on resize become obvious -- you can correlate them against the
		 *	'data=%p' field printed by print_vector_state.
		 *
		 *	build:	./build.sh --block BLOCK_3
		*/

		/*
		 *	--- scope 3a: natural growth, no reserve ---
		 *	std::vector starts with capacity=0.
		 *	each push_back that exceeds capacity allocates a NEW heap buffer
		 *	(typically doubling: 0->1->2->4->...), copy-ctors every existing element
		 *	from old buffer -> new buffer, then copies the new element into place,
		 *	then frees the old buffer.
		*/
		{
			printf("\n=== BLOCK_3 scope 3a: natural growth, no reserve ===\n");
			std::vector<Vertex> vertices;
			print_vector_state("init", vertices);

			printf("\n-- push_back #1: {1,2,3} (fresh alloc, expect cap 0->1) --\n");
			vertices.push_back({1, 2, 3});
			print_vector_state("after #1", vertices);

			printf("\n-- push_back #2: {4,5,6} (RESIZE 1->2: heap-to-heap copy of {1,2,3}, then stack-to-heap copy of {4,5,6}) --\n");
			vertices.push_back({4, 5, 6});
			print_vector_state("after #2", vertices);

			printf("\n-- push_back #3: {7,8,9} (RESIZE 2->4: heap-to-heap copies of {1,2,3} AND {4,5,6}, then stack-to-heap copy of {7,8,9}) --\n");
			vertices.push_back({7, 8, 9});
			print_vector_state("after #3", vertices);

			printf("\n-- push_back #4: {10,11,12} (cap=4 has room, NO resize, only 1 stack-to-heap copy) --\n");
			vertices.push_back({10, 11, 12});
			print_vector_state("after #4", vertices);
		}

		/*
		 *	--- scope 3b: reserve(3), then exceed it ---
		 *	the user's question: 'reserve capacity of 3 and then show everything
		 *	gets a copy when we add a 4th element'.
		 *	pushes #1-#3 do ONE copy each (parametized ctor stack temp, then
		 *	copy ctor stack -> heap slot). push #4 triggers reallocation:
		 *	the 3 EXISTING elements get copy-ctor'd heap -> heap, then the new
		 *	element gets copy-ctor'd stack -> new heap. 5 ctor calls total for #4.
		*/
		{
			printf("\n=== BLOCK_3 scope 3b: reserve(3), then push_back 4 elements ===\n");
			std::vector<Vertex> vertices;
			vertices.reserve(3);
			print_vector_state("after reserve(3)", vertices);

			printf("\n-- push_back #1, #2, #3: NO resize, each = 1 parametized + 1 copy (stack -> heap slot) --\n");
			vertices.push_back({1, 2, 3});
			vertices.push_back({4, 5, 6});
			vertices.push_back({7, 8, 9});
			print_vector_state("after 3 push_backs", vertices);

			printf("\n-- push_back #4: {10,11,12} EXCEEDS capacity, expect:\n");
			printf("     1 parametized ctor (stack temp for {10,11,12})\n");
			printf("     3 copy ctors: OLD heap buffer -> NEW heap buffer (existing {1,2,3}, {4,5,6}, {7,8,9})\n");
			printf("     1 copy ctor: stack temp -> NEW heap buffer (the new {10,11,12})\n");
			vertices.push_back({10, 11, 12});
			print_vector_state("after #4 (resized)", vertices);
		}

		/*
		 *	--- scope 3c: push_back({...}) vs push_back(Vertex(...)) ---
		 *	the earlier observation that 'push_back(Vertex(1,2,3))' calls copy ctor
		 *	TWICE was made without copy elision. under -O2 + C++17 onwards
		 *	(guaranteed copy elision for prvalues), Vertex(4,5,6) is constructed
		 *	directly into the push_back argument slot -- so both forms produce
		 *	the SAME output: 1 parametized + 1 copy (stack -> heap slot).
		 *	to reproduce the un-elided 1+2 behaviour, compile with
		 *	g++ ... -fno-elide-constructors (will NOT match this lesson's build.sh).
		*/
		{
			printf("\n=== BLOCK_3 scope 3c: push_back({...}) vs push_back(Vertex(...)) ===\n");
			std::vector<Vertex> vertices;
			vertices.reserve(2);
			print_vector_state("after reserve(2)", vertices);

			printf("\n-- braced form: vertices.push_back({1,2,3}) --\n");
			vertices.push_back({1, 2, 3});

			printf("\n-- explicit form: vertices.push_back(Vertex(4,5,6)) --\n");
			vertices.push_back(Vertex(4, 5, 6));

			printf("\n(both forms should produce 1 parametized + 1 copy under -O2 thanks to copy elision)\n");
			print_vector_state("after 2 push_backs", vertices);
		}

		/*
		 *	--- scope 3d: emplace_back eliminates the stack temporary ---
		 *	emplace_back forwards its args DIRECTLY to the element's ctor at the
		 *	heap slot. no stack temp, no copy ctor.
		 *	proof: the parametized ctor's 'this' addr (logged above) will fall
		 *	inside [data(), data() + capacity*sizeof(Vertex)) shown by print_vector_state.
		*/
		{
			printf("\n=== BLOCK_3 scope 3d: emplace_back constructs IN-PLACE on the heap ===\n");
			std::vector<Vertex> vertices;
			vertices.reserve(3);
			print_vector_state("after reserve(3)", vertices);

			printf("\n-- emplace_back(1,2,3): parametized ctor's 'this' should == data + 0*sizeof(Vertex) --\n");
			vertices.emplace_back(1, 2, 3);
			print_vector_state("after emplace #1", vertices);

			printf("\n-- emplace_back(4,5,6): parametized ctor's 'this' should == data + 1*sizeof(Vertex) --\n");
			vertices.emplace_back(4, 5, 6);
			print_vector_state("after emplace #2", vertices);

			printf("\n-- emplace_back(7,8,9): parametized ctor's 'this' should == data + 2*sizeof(Vertex) --\n");
			vertices.emplace_back(7, 8, 9);
			print_vector_state("after emplace #3", vertices);

			printf("\n(ZERO copy ctor calls in this scope -- parametized ctor's 'this' addr lies INSIDE the heap buffer)\n");
		}

		/*
		 *	--- scope 3e: emplace_back past capacity STILL copies existing elements ---
		 *	emplace_back eliminates the temp->slot copy for the NEW element only.
		 *	reallocation copies are governed by capacity, not by the insertion API.
		 *	when capacity is exceeded, std::vector still has to copy every
		 *	existing element from the old heap buffer to the new one.
		*/
		{
			printf("\n=== BLOCK_3 scope 3e: emplace_back past capacity STILL copies existing elements ===\n");
			std::vector<Vertex> vertices;
			vertices.reserve(2);
			print_vector_state("after reserve(2)", vertices);

			printf("\n-- emplace_back(1,2,3) + emplace_back(4,5,6): ZERO copies, both in-place on heap --\n");
			vertices.emplace_back(1, 2, 3);
			vertices.emplace_back(4, 5, 6);
			print_vector_state("after 2 emplace_backs (cap=2)", vertices);

			printf("\n-- emplace_back(7,8,9) EXCEEDS cap=2, expect:\n");
			printf("     2 copy ctors: existing {1,2,3} and {4,5,6} get copied OLD heap -> NEW heap\n");
			printf("     1 parametized ctor: {7,8,9} constructed DIRECTLY on the new heap slot (no copy for the new element)\n");
			vertices.emplace_back(7, 8, 9);
			print_vector_state("after emplace #3 (resized)", vertices);
		}
	#endif
	return 0;
}
