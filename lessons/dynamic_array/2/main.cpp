/*
 *	here we will try create our own dynamic heap contiguous buffer class
 *	similar to std::vector
*/

#include <iostream>
#include <cstdio>
#include <cstdint>
#include <cstddef>

#include <cassert>
#include <utility>
#include <new>

/*
 *	depth-aware logging
 *	every Vector method bumps g_log_indent on entry, drops on exit.
 *	Vertex prints just call log_indent() and inherit the depth.
 *
 *		standalone Vertex on stack          -> 0 tabs
 *		Vertex inside Vector::PushBack      -> 1 tab
 *		Vertex inside Reallocate from above -> 2 tabs
*/
static int g_log_indent = 0;
static void log_indent () {
	for (int i = 0; i < g_log_indent; i++) putchar('\t');
}

struct Vertex {
	float x;
	float y;
	float z;

	/*
	 *	default ctor
	*/
	Vertex (
	) : x(0.0f), y(0.0f), z(0.0f) {
		log_indent();
		printf("[Vertex{}] default ctor      -- x=%.1f y=%.1f z=%.1f\n",
			this->x, this->y, this->z);
	}

	/*
	 *	parametized ctor
	 *	initialization list to avoid copying
	*/
	Vertex (
		float in_x,
		float in_y,
		float in_z
	) : x(in_x), y(in_y), z(in_z) {
		log_indent();
		printf("[Vertex{}] parametized ctor  -- x=%.1f y=%.1f z=%.1f\n",
			this->x, this->y, this->z);
	}

	/*
	 *	copy ctor
	*/
	Vertex (
		const Vertex& vertex
	) : x(vertex.x), y(vertex.y), z(vertex.z) {
		log_indent();
		printf("[Vertex{}] copy ctor         -- x=%.1f y=%.1f z=%.1f\n",
			this->x, this->y, this->z);
	}

	/*
	 *	move ctor
	*/
	Vertex (
		Vertex&& vertex
	) noexcept : x(vertex.x), y(vertex.y), z(vertex.z) {
		log_indent();
		printf("[Vertex{}] move ctor         -- x=%.1f y=%.1f z=%.1f\n",
			this->x, this->y, this->z);
	}

	/*
	 *	copy asign ctor
	*/
	Vertex& operator = (
		const Vertex& vertex
	) {
		this->x = vertex.x;
		this->y = vertex.y;
		this->z = vertex.z;

		log_indent();
		printf("[Vertex{}] copy assign       -- x=%.1f y=%.1f z=%.1f\n",
			this->x, this->y, this->z);

		return *this;
	}

	/*
	 *	move asign ctor
	*/
	Vertex& operator = (
		Vertex&& vertex
	) noexcept {
		this->x = vertex.x;
		this->y = vertex.y;
		this->z = vertex.z;

		log_indent();
		printf("[Vertex{}] move assign       -- x=%.1f y=%.1f z=%.1f\n",
			this->x, this->y, this->z);

		return *this;
	}

	/*
	 *	dtor
	*/
	~Vertex () {
		log_indent();
		printf("[Vertex{}] dtor              -- x=%.1f y=%.1f z=%.1f\n",
			this->x, this->y, this->z);
	}
};

/*
 *	our Vector implementation
 *	Vector<T, S=0>
 *
 *	S=0	DEFAULT
 *		empty default ctor, no heap touched
 *	S>0	ctor pre-reserves capacity `S` via Reallocate(S)
*/
template<
	typename T,
	std::size_t S = 0
>
class Vector {
	private:
		T* data_ = nullptr;
		std::size_t size_ = 0;
		std::size_t capacity_ = 0;

	public:
		/*
		 *	default ctor
		*/
		Vector () {
			log_indent();
			printf("[Vector{}] - default ctor S=%zu\n", S);
			g_log_indent++;
			if constexpr (S>0) {
				Reallocate(S);
			}
			g_log_indent--;
		}

		~Vector () {
			log_indent();
			printf("[Vector{}] - dtor (size_=%zu, capacity_=%zu)\n",
				size_, capacity_);
			g_log_indent++;
			Clear();
			/*
			 *	raw memory de-allocation
			 *	directly releasing raw block of heap without invoking any dtor
			*/
			::operator delete(data_);
			g_log_indent--;
		}

		/*
		 *	accessor methods
		*/
		std::size_t Size () const {
			return size_;
		}

		std::size_t Capacity () const {
			return capacity_;
		}
		const T& operator[] (
			std::size_t i
		) const {
			assert(i<size_);
			return data_[i];
		}

		/*
		 *	mutator methods
		*/
		T& operator[] (
			std::size_t i
		) {
			assert(i<size_);
			return data_[i];
		}

		/*
		 *	PushBack(const T&)
		 *	LVALUE of PushBack()
		 *	caller's object stays alive
		 *	copy ctor a new `T` at this heap slot
		 *	expected output for `T=Vertex`
		 *		1 copy ctor called
		*/
		void PushBack (
			const T& value
		) {
			log_indent();
			printf("[Vector{}] push_back(const T&) size_=%zu, capacity_=%zu\n",
				size_, capacity_);
			g_log_indent++;
			GrowIfFull();
			new (&data_[size_]) T(value);   // placement new -- copy ctor at heap slot
			size_++;
			g_log_indent--;
		}

		/*
		 *	PushBack(T&& value)
		 *	RVALUE of PushBack()
		 *	caller passes tmp object (i.e. std::move lvalue)
		 *	move ctor a new `T` at this heap slot
		 *	caller tmp is then destroyed at lifetime end
		 *	expected output for `T=Vertex`
		 *		1 move ctor called
		 *		1 dtor called
		*/
		void PushBack (
			T&& value
		) {
			log_indent();
			printf("[Vector{}] push_back(T&&)      size_=%zu, capacity_=%zu\n",
				size_, capacity_);
			g_log_indent++;
			GrowIfFull();
			new (&data_[size_]) T(std::move(value));   // placement new -- move ctor at heap slot
			size_++;
			g_log_indent--;
		}

		/*
		 *	EmplaceBack -- forward ctor args DIRECTLY to placement-new on heap slot.
		 *	no temp, no copy, no move.
		*/
		template <typename... Args>
		T& EmplaceBack (
			Args&&... args
		) {
			log_indent();
			printf("[Vector{}] emplace_back        size_=%zu, capacity_=%zu\n",
				size_, capacity_);
			g_log_indent++;
			GrowIfFull();
			new (&data_[size_]) T(std::forward<Args>(args)...);
			g_log_indent--;
			return data_[size_++];
		}

		void PopBack () {
			if (size_ > 0) {
				log_indent();
				printf("[Vector{}] pop_back            size_=%zu -> %zu\n",
					size_, size_ - 1);
				g_log_indent++;
				size_--;
				// explicit dtor on the slot that we just dropped
				data_[size_].~T();
				g_log_indent--;
			}
		}

		void Clear () {
			log_indent();
			printf("[Vector{}] clear               size_=%zu -> 0\n", size_);
			g_log_indent++;
			for (
				std::size_t i=0;
				i<size_;
				i++
			) {
				data_[i].~T();
			}
			size_ = 0;
			g_log_indent--;
			// capacity_ goes unchanged, matchin std::vector::clear() (i.e. no shrink)
		}
	private:
		void GrowIfFull () {
			if (
				size_ >= capacity_
			) {
				std::size_t new_capacity = (0 == capacity_) ? (2) : (capacity_ * 2);
				Reallocate(new_capacity);
			}
		}

		/*
		 *	calls `::operator new` for raw malloc() no ctor
		 *	for i in 0..transfer_count place a new move/copy of old data_[i]
		 *	for i in 0..size_
		 *		explicitly call data_[i].~T() on old data
		 *		even if not in tranfer_count
		 *	`::operator delete` the old data_ so raw bytes returned to kernel and free()d from this process, no dtor
		 *	install the new buffer on the heap + new capacity
		*/
		void Reallocate (
			std::size_t NewCapacity
		) {
			#if defined(REALLOCATE_WITH_COPY) && defined(REALLOCATE_WITH_MOVE)
				#error "Can not Vector::Reallocate() with copy+move"
			#endif
			#if !defined(REALLOCATE_WITH_COPY) && !defined(REALLOCATE_WITH_MOVE)
				#error "Can not Vector::Reallocate() without copy/move"
			#endif
			#if defined(REALLOCATE_WITH_COPY)
				const char* mode = "COPY";
			#elif defined(REALLOCATE_WITH_MOVE)
				const char* mode = "MOVE";
			#endif

			std::size_t transfer_count = (size_ < NewCapacity) ? (size_) : (NewCapacity);

			log_indent();
			printf("[Vector{}] reallocate          cap %zu -> %zu, mode=%s\n",
				capacity_, NewCapacity, mode);

			g_log_indent++;
			T* new_data = (T*)::operator new(NewCapacity * sizeof(T));

			for (std::size_t i=0; i<transfer_count; i++) {
				#if defined(REALLOCATE_WITH_COPY)
					new (&new_data[i]) T(data_[i]);
				#elif defined(REALLOCATE_WITH_MOVE)
					new (&new_data[i]) T(std::move(data_[i]));
				#endif
			}

			for (std::size_t i=0; i<size_; i++) {
				data_[i].~T();
			}

			::operator delete(data_);
			g_log_indent--;

			data_ = new_data;
			capacity_ = NewCapacity;
			size_ = transfer_count;

			log_indent();
			printf("[Vector{}]   installed         size_=%zu, capacity_=%zu\n",
				size_, capacity_);
		}
};

int main () {
	#if defined(BLOCK_0)
		/*
		 *	fixed-size stack buffer of 'Vertex' struct
		 *	expected size 12 bytes
		*/
		printf("sizeof(Vertex)=%zu\n", sizeof(Vertex));

		printf("entering scope, declaring `Vertex vertices[3];` on the stack");
		{
			Vertex vertices[3] = {
				Vertex(1.0f, 2.0f, 3.0f),
				Vertex(4.0f, 5.0f, 6.0f),
				Vertex(6.0f, 7.0f, 8.0f)
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
	#elif defined(BLOCK_1)
		/*
		 *	TEST 1 -- Vector<int>  (S=0, primitive type)
		 *	  shows: empty Vector touches no heap. growth strategy 0 -> 2 -> 4 -> 8.
		 *	  int has no ctor printfs, so we watch Size/Capacity instead.
		*/
		printf("\n==== TEST 1/3: Vector<int> (S=0, primitive) ====\n\n");
		{
			Vector<int> v;
			printf("after default ctor: Size=%zu Capacity=%zu  (expect 0, 0 -- no heap)\n\n",
				v.Size(), v.Capacity());

			printf("--- 5 PushBacks: expect growth 0 -> 2 -> 4 -> 8 ---\n");
			v.PushBack(10);  printf("after PushBack(10): Size=%zu Capacity=%zu\n", v.Size(), v.Capacity());
			v.PushBack(20);  printf("after PushBack(20): Size=%zu Capacity=%zu\n", v.Size(), v.Capacity());
			v.PushBack(30);  printf("after PushBack(30): Size=%zu Capacity=%zu\n", v.Size(), v.Capacity());
			v.PushBack(40);  printf("after PushBack(40): Size=%zu Capacity=%zu\n", v.Size(), v.Capacity());
			v.PushBack(50);  printf("after PushBack(50): Size=%zu Capacity=%zu\n", v.Size(), v.Capacity());

			printf("\n--- readback via operator[] ---\n");
			for (std::size_t i = 0; i < v.Size(); i++) {
				printf("v[%zu] = %d\n", i, v[i]);
			}

			printf("\n--- PopBack x2 (size drops, capacity unchanged) ---\n");
			v.PopBack();  printf("after PopBack: Size=%zu Capacity=%zu\n", v.Size(), v.Capacity());
			v.PopBack();  printf("after PopBack: Size=%zu Capacity=%zu\n", v.Size(), v.Capacity());

			printf("\n--- Clear (size = 0, capacity unchanged -- matches std::vector::clear) ---\n");
			v.Clear();  printf("after Clear: Size=%zu Capacity=%zu\n", v.Size(), v.Capacity());

			printf("\nscope ending -- ~Vector fires\n");
		}

		/*
		 *	TEST 2 -- Vector<Vertex>  (S=0, class with all special-member printfs)
		 *	  shows: every ctor / move / copy / dtor path through the Vector API.
		*/
		printf("\n\n==== TEST 2/3: Vector<Vertex> (S=0, class with printfs) ====\n\n");
		{
			Vector<Vertex> v;
			printf("after default ctor: Size=%zu Capacity=%zu  (expect 0, 0 -- ZERO Vertex ctors)\n",
				v.Size(), v.Capacity());

			printf("\n--- PushBack lvalue (copy form) ---\n");
			printf("step 1: Vertex a(1,2,3) -- expect 1 parametized ctor on STACK\n");
			Vertex a(1.0f, 2.0f, 3.0f);
			printf("step 2: v.PushBack(a) -- expect Reallocate(0->2) + 1 copy ctor (a -> heap slot)\n");
			v.PushBack(a);
			printf("after: Size=%zu Capacity=%zu\n", v.Size(), v.Capacity());

			printf("\n--- PushBack rvalue (move form) ---\n");
			printf("v.PushBack(Vertex(4,5,6)) -- expect:\n");
			printf("  1 parametized ctor (stack TEMP)\n");
			printf("  1 move ctor (TEMP -> heap slot)\n");
			printf("  1 dtor (TEMP at end-of-statement)\n");
			v.PushBack(Vertex(4.0f, 5.0f, 6.0f));
			printf("after: Size=%zu Capacity=%zu\n", v.Size(), v.Capacity());

			printf("\n--- EmplaceBack (in-place construction, triggers realloc) ---\n");
			printf("v.EmplaceBack(7,8,9) -- expect:\n");
			printf("  Reallocate(2->4): 2 move ctors (existing slots -> new heap) + 2 dtors (old slots)\n");
			printf("  1 parametized ctor DIRECTLY on new heap slot [2]  (NO temp, NO copy/move)\n");
			v.EmplaceBack(7.0f, 8.0f, 9.0f);
			printf("after: Size=%zu Capacity=%zu\n", v.Size(), v.Capacity());

			printf("\n--- readback via operator[] ---\n");
			for (std::size_t i = 0; i < v.Size(); i++) {
				printf("v[%zu] = (%.1f, %.1f, %.1f)\n", i, v[i].x, v[i].y, v[i].z);
			}

			printf("\n--- PopBack (1 dtor for popped slot) ---\n");
			v.PopBack();
			printf("after: Size=%zu Capacity=%zu\n", v.Size(), v.Capacity());

			printf("\n--- Clear (dtor for each remaining live slot) ---\n");
			v.Clear();
			printf("after: Size=%zu Capacity=%zu\n", v.Size(), v.Capacity());

			printf("\nscope ending -- expect dtor for 'a' (stack), then ~Vector (Clear no-op + ::operator delete)\n");
		}

		/*
		 *	TEST 3 -- Vector<Vertex, 4>  (S=4, pre-reserved heap)
		 *	  shows: S>0 pre-allocates raw bytes at ctor (NO Vertex ctors fire).
		 *	         4 EmplaceBacks fit without realloc. 5th triggers growth to 8.
		*/
		printf("\n\n==== TEST 3/3: Vector<Vertex, 4> (S=4, pre-reserved) ====\n\n");
		{
			Vector<Vertex, 4> v;
			printf("after default ctor: Size=%zu Capacity=%zu  (expect 0, 4 -- heap alloc but ZERO Vertex ctors)\n",
				v.Size(), v.Capacity());

			printf("\n--- 4 EmplaceBacks within capacity ---\n");
			printf("expect: 4 parametized ctors DIRECTLY on heap slots, NO realloc\n");
			v.EmplaceBack(1.0f, 2.0f, 3.0f);
			v.EmplaceBack(4.0f, 5.0f, 6.0f);
			v.EmplaceBack(7.0f, 8.0f, 9.0f);
			v.EmplaceBack(10.0f, 11.0f, 12.0f);
			printf("after: Size=%zu Capacity=%zu  (expect 4, 4 -- no realloc happened)\n",
				v.Size(), v.Capacity());

			printf("\n--- 5th EmplaceBack EXCEEDS cap=4 (triggers realloc 4->8) ---\n");
			printf("expect:\n");
			printf("  Reallocate(4->8): 4 move ctors + 4 dtors\n");
			printf("  1 parametized ctor DIRECTLY on new heap slot [4]\n");
			v.EmplaceBack(13.0f, 14.0f, 15.0f);
			printf("after: Size=%zu Capacity=%zu  (expect 5, 8)\n",
				v.Size(), v.Capacity());

			printf("\nscope ending -- ~Vector fires (Clear: 5 dtors, then ::operator delete)\n");
		}
	#endif
}
