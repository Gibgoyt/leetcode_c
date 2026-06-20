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
	printf("\t[STATE] %s -- size=%zu, capacity=%zu, data=%p\n",
		label, v.size(), v.capacity(), (const void*)v.data());
}

/*
 *	visual separators for BLOCK_3 output -- makes the timeline readable
 *	===	scope boundary	(major: "doing a whole new demo")
 *	---	step boundary	(minor: "one push_back/emplace_back inside a scope")
*/
#define BLOCK_3_SCOPE_BAR "================================================================================"
#define BLOCK_3_STEP_BAR  "--------------------------------------------------------------------------------"

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
		 *	the output is structured as a TIMELINE:
		 *
		 *		================================ scope header
		 *		[STATE] ...			initial state
		 *
		 *		---------------- step header
		 *		expect: ...			what SHOULD happen
		 *		---------------- step header
		 *		parametized ctor at ...		actual ctor calls
		 *		copy ctor: from ... -> to ...
		 *		[STATE] ...			resulting state
		 *
		 *	addresses help distinguish memory regions at a glance:
		 *		stack	-> high addrs, e.g. 0x7fff...
		 *		heap	-> low addrs,  e.g. 0x55e8...
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
			printf("\n\n");
			printf("%s\n", BLOCK_3_SCOPE_BAR);
			printf(" BLOCK_3 scope 3a -- natural growth, no reserve\n");
			printf("%s\n", BLOCK_3_SCOPE_BAR);
			printf(" std::vector starts with cap=0. each push_back past cap allocates a NEW\n");
			printf(" heap buffer (typically doubling 0->1->2->4->...), copy-ctors every\n");
			printf(" existing element OLD heap -> NEW heap, then copies the new element\n");
			printf(" stack temp -> NEW heap, then frees the OLD buffer.\n");
			printf("\n");

			std::vector<Vertex> vertices;
			print_vector_state("init", vertices);

			printf("\n%s\n", BLOCK_3_STEP_BAR);
			printf(" step 3a.1/4 -- vertices.push_back({1, 2, 3})\n");
			printf(" expect: fresh alloc, cap 0 -> 1; 1 parametized + 1 copy stack -> heap\n");
			printf("%s\n", BLOCK_3_STEP_BAR);
			vertices.push_back({1, 2, 3});
			printf("\n");
			print_vector_state("after push #1", vertices);

			printf("\n%s\n", BLOCK_3_STEP_BAR);
			printf(" step 3a.2/4 -- vertices.push_back({4, 5, 6})\n");
			printf(" expect: RESIZE cap 1 -> 2\n");
			printf("         1 parametized + 1 copy stack temp -> NEW heap (for {4,5,6})\n");
			printf("         1 copy OLD heap -> NEW heap (existing {1,2,3})\n");
			printf("%s\n", BLOCK_3_STEP_BAR);
			vertices.push_back({4, 5, 6});
			printf("\n");
			print_vector_state("after push #2", vertices);

			printf("\n%s\n", BLOCK_3_STEP_BAR);
			printf(" step 3a.3/4 -- vertices.push_back({7, 8, 9})\n");
			printf(" expect: RESIZE cap 2 -> 4\n");
			printf("         1 parametized + 1 copy stack temp -> NEW heap (for {7,8,9})\n");
			printf("         2 copies OLD heap -> NEW heap (existing {1,2,3} and {4,5,6})\n");
			printf("%s\n", BLOCK_3_STEP_BAR);
			vertices.push_back({7, 8, 9});
			printf("\n");
			print_vector_state("after push #3", vertices);

			printf("\n%s\n", BLOCK_3_STEP_BAR);
			printf(" step 3a.4/4 -- vertices.push_back({10, 11, 12})\n");
			printf(" expect: cap=4 has room, NO resize; 1 parametized + 1 copy stack -> heap\n");
			printf("%s\n", BLOCK_3_STEP_BAR);
			vertices.push_back({10, 11, 12});
			printf("\n");
			print_vector_state("after push #4", vertices);
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
			printf("\n\n");
			printf("%s\n", BLOCK_3_SCOPE_BAR);
			printf(" BLOCK_3 scope 3b -- reserve(3), then push_back 4 elements\n");
			printf("%s\n", BLOCK_3_SCOPE_BAR);
			printf(" pre-reserves heap buffer of cap=3. the first 3 pushes fit (1 parametized\n");
			printf(" + 1 copy each, no resize). push #4 EXCEEDS cap so std::vector allocates\n");
			printf(" a NEW heap buffer, copy-ctors all 3 existing elements OLD -> NEW, then\n");
			printf(" copy-ctors the new element stack temp -> NEW heap. 5 ctor calls on #4.\n");
			printf("\n");

			std::vector<Vertex> vertices;
			vertices.reserve(3);
			print_vector_state("after reserve(3)", vertices);

			printf("\n%s\n", BLOCK_3_STEP_BAR);
			printf(" step 3b.1/2 -- push_back({1,2,3}), push_back({4,5,6}), push_back({7,8,9})\n");
			printf(" expect: NO resize; each push = 1 parametized + 1 copy stack -> heap slot\n");
			printf("%s\n", BLOCK_3_STEP_BAR);
			vertices.push_back({1, 2, 3});
			vertices.push_back({4, 5, 6});
			vertices.push_back({7, 8, 9});
			printf("\n");
			print_vector_state("after 3 pushes", vertices);

			printf("\n%s\n", BLOCK_3_STEP_BAR);
			printf(" step 3b.2/2 -- vertices.push_back({10, 11, 12})  <-- EXCEEDS cap=3\n");
			printf(" expect: 1 parametized ctor (stack temp for {10,11,12})\n");
			printf("         3 copy ctors OLD heap -> NEW heap (existing {1,2,3}, {4,5,6}, {7,8,9})\n");
			printf("         1 copy ctor  stack temp -> NEW heap (new {10,11,12})\n");
			printf("%s\n", BLOCK_3_STEP_BAR);
			vertices.push_back({10, 11, 12});
			printf("\n");
			print_vector_state("after push #4 (resized)", vertices);
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
			printf("\n\n");
			printf("%s\n", BLOCK_3_SCOPE_BAR);
			printf(" BLOCK_3 scope 3c -- push_back({...}) vs push_back(Vertex(...))\n");
			printf("%s\n", BLOCK_3_SCOPE_BAR);
			printf(" under -O2 + C++17 (guaranteed copy elision for prvalues), both forms\n");
			printf(" produce IDENTICAL output: 1 parametized + 1 copy stack -> heap slot.\n");
			printf(" to see the un-elided 1+2 behaviour, compile with -fno-elide-constructors.\n");
			printf("\n");

			std::vector<Vertex> vertices;
			vertices.reserve(2);
			print_vector_state("after reserve(2)", vertices);

			printf("\n%s\n", BLOCK_3_STEP_BAR);
			printf(" step 3c.1/2 -- braced form: vertices.push_back({1, 2, 3})\n");
			printf(" expect: 1 parametized + 1 copy stack -> heap\n");
			printf("%s\n", BLOCK_3_STEP_BAR);
			vertices.push_back({1, 2, 3});

			printf("\n%s\n", BLOCK_3_STEP_BAR);
			printf(" step 3c.2/2 -- explicit form: vertices.push_back(Vertex(4, 5, 6))\n");
			printf(" expect: 1 parametized + 1 copy stack -> heap (SAME as braced under -O2)\n");
			printf("%s\n", BLOCK_3_STEP_BAR);
			vertices.push_back(Vertex(4, 5, 6));
			printf("\n");
			print_vector_state("after 2 pushes", vertices);
		}

		/*
		 *	--- scope 3d: emplace_back eliminates the stack temporary ---
		 *	emplace_back forwards its args DIRECTLY to the element's ctor at the
		 *	heap slot. no stack temp, no copy ctor.
		 *	proof: the parametized ctor's 'this' addr (logged above) will fall
		 *	inside [data(), data() + capacity*sizeof(Vertex)) shown by print_vector_state.
		*/
		{
			printf("\n\n");
			printf("%s\n", BLOCK_3_SCOPE_BAR);
			printf(" BLOCK_3 scope 3d -- emplace_back constructs IN-PLACE on the heap\n");
			printf("%s\n", BLOCK_3_SCOPE_BAR);
			printf(" emplace_back forwards its args DIRECTLY to the Vertex ctor at the heap\n");
			printf(" slot. no stack temp, no copy ctor. proof: parametized ctor's 'this'\n");
			printf(" addr will MATCH 'data + N*sizeof(Vertex)' from print_vector_state.\n");
			printf("\n");

			std::vector<Vertex> vertices;
			vertices.reserve(3);
			print_vector_state("after reserve(3)", vertices);

			printf("\n%s\n", BLOCK_3_STEP_BAR);
			printf(" step 3d.1/3 -- vertices.emplace_back(1, 2, 3)\n");
			printf(" expect: 1 parametized ctor with 'this' == data + 0*sizeof(Vertex), ZERO copy ctors\n");
			printf("%s\n", BLOCK_3_STEP_BAR);
			vertices.emplace_back(1, 2, 3);
			printf("\n");
			print_vector_state("after emplace #1", vertices);

			printf("\n%s\n", BLOCK_3_STEP_BAR);
			printf(" step 3d.2/3 -- vertices.emplace_back(4, 5, 6)\n");
			printf(" expect: 1 parametized ctor with 'this' == data + 1*sizeof(Vertex), ZERO copy ctors\n");
			printf("%s\n", BLOCK_3_STEP_BAR);
			vertices.emplace_back(4, 5, 6);
			printf("\n");
			print_vector_state("after emplace #2", vertices);

			printf("\n%s\n", BLOCK_3_STEP_BAR);
			printf(" step 3d.3/3 -- vertices.emplace_back(7, 8, 9)\n");
			printf(" expect: 1 parametized ctor with 'this' == data + 2*sizeof(Vertex), ZERO copy ctors\n");
			printf("%s\n", BLOCK_3_STEP_BAR);
			vertices.emplace_back(7, 8, 9);
			printf("\n");
			print_vector_state("after emplace #3", vertices);

			printf("\n");
			printf(" note: ZERO copy ctors in this scope -- parametized ctor 'this' lies\n");
			printf("       INSIDE the heap buffer (sizeof(Vertex) = 12 bytes apart).\n");
		}

		/*
		 *	--- scope 3e: emplace_back past capacity STILL copies existing elements ---
		 *	emplace_back eliminates the temp->slot copy for the NEW element only.
		 *	reallocation copies are governed by capacity, not by the insertion API.
		 *	when capacity is exceeded, std::vector still has to copy every
		 *	existing element from the old heap buffer to the new one.
		*/
		{
			printf("\n\n");
			printf("%s\n", BLOCK_3_SCOPE_BAR);
			printf(" BLOCK_3 scope 3e -- emplace_back past cap STILL copies existing elements\n");
			printf("%s\n", BLOCK_3_SCOPE_BAR);
			printf(" emplace_back kills the temp->slot copy for the NEW element only.\n");
			printf(" reallocation copies are governed by capacity, NOT the insertion API.\n");
			printf(" once cap is exceeded, existing elements must be copied OLD -> NEW heap.\n");
			printf("\n");

			std::vector<Vertex> vertices;
			vertices.reserve(2);
			print_vector_state("after reserve(2)", vertices);

			printf("\n%s\n", BLOCK_3_STEP_BAR);
			printf(" step 3e.1/2 -- emplace_back(1,2,3), emplace_back(4,5,6)\n");
			printf(" expect: 2 parametized ctors directly on heap slots, ZERO copy ctors\n");
			printf("%s\n", BLOCK_3_STEP_BAR);
			vertices.emplace_back(1, 2, 3);
			vertices.emplace_back(4, 5, 6);
			printf("\n");
			print_vector_state("after 2 emplaces", vertices);

			printf("\n%s\n", BLOCK_3_STEP_BAR);
			printf(" step 3e.2/2 -- vertices.emplace_back(7, 8, 9)  <-- EXCEEDS cap=2\n");
			printf(" expect: 1 parametized ctor with 'this' on the NEW heap slot (no copy for new)\n");
			printf("         2 copy ctors OLD heap -> NEW heap (existing {1,2,3} and {4,5,6})\n");
			printf("%s\n", BLOCK_3_STEP_BAR);
			vertices.emplace_back(7, 8, 9);
			printf("\n");
			print_vector_state("after emplace #3 (resized)", vertices);
		}

		printf("\n\n");
		printf("%s\n", BLOCK_3_SCOPE_BAR);
		printf(" BLOCK_3 end -- recap\n");
		printf("%s\n", BLOCK_3_SCOPE_BAR);
		printf(" 1. std::vector lives on the stack but its element buffer is on the heap.\n");
		printf(" 2. push_back(T_temp): 1 parametized ctor (stack temp) + 1 copy ctor (stack -> heap).\n");
		printf(" 3. emplace_back(args...): 1 parametized ctor DIRECTLY on heap slot, ZERO copies.\n");
		printf(" 4. exceeding capacity allocates a new heap buffer and copies ALL existing\n");
		printf("    elements OLD heap -> NEW heap, regardless of push_back vs emplace_back.\n");
		printf(" 5. reserve(N) up front avoids the reallocation copy storm if you know the size.\n");
		printf("\n");
	#endif
	return 0;
}
