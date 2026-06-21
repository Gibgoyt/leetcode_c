/*
 *	naive shared ptr
 *	2 ptrs on the stack
 *		SharedPtr<T* ptr_; long* refcount_>
 *	every SharedPtr<T>(new T(...)) does TWO heap allocations:
 *		- `T` itself
 *		- `long` refcount living off to the side
 *	copy ctor bumps refcount_
 *	dtor decrements
 *	hits 0, then delete both
 *	no weak_ptr
 *	no custom deleters
 *	no make_shared
 *	no atomics
 *	no aliasing
*/

#include <iostream>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <utility>

/*
 *  depth-aware logging -- copied verbatim in spirit from dynamic_array/2/main.cpp.
 *  every SharedPtr method that wraps a private helper bumps g_log_indent on entry
 *  and drops it on exit. helpers (release) print at the bumped depth so the
 *  destruction cascade indents readably.
*/
static int g_log_indent = 0;
static void log_indent () {
	for (int i = 0; i < g_log_indent; i++) {
		putchar('\t');
	}
}

struct Tracer {
	int id;

	/*
	 *	default ctor
	 *	id=0 so the field is always defined.
	 *	useful when a user does `new Tracer{}` (value-init) without an id.
	*/
	Tracer (
	) : id(0) {
		log_indent();
		printf("[Tracer{%d}] default ctor\n", id);
	}

	/*
	 *	parametized ctor
	*/
	Tracer (
		int in_id
	) : id(in_id) {
		log_indent();
		printf("[Tracer{%d}] parametized ctor\n", id);
	}

	/*
	 *	copy/lvalue ctor
	*/
	Tracer (
		const Tracer& other
	) : id(other.id) {
		log_indent();
		printf("[Tracer{%d}] copy ctor\n", id);
	}

	/*
	 *	move/rvalue ctor
	*/
	Tracer (
		Tracer&& other
	) noexcept : id(other.id) {
		log_indent();
		printf("[Tracer{%d}] move ctor\n", id);
	}

	/*
	 *	copy asign (non-existent)
	*/
	Tracer& operator = (
		const Tracer& other
	) = delete;

	/*
	 *	move asign ctor (non-existent)
	*/
	Tracer& operator = (
		Tracer&&
	) = delete;

	~Tracer () {
		log_indent();
		printf("[Tracer{%d}] dtor\n", id);
	}
};

template <typename T>
	class SharedPtr {
		private:
			T* ptr_ = nullptr;
			long* refcount_ = nullptr;
		
		public:
			/*
			 *	default ctor
			*/
			SharedPtr () {
				log_indent();
				printf("[SharedPtr] default ctor (empty)\n");
			}

			/*
			 *	parametized ctor
			 *	explicit ctor(T*)
			 *		takes ownership of a raw ptr
			 *		allocate brand new refcount slot on the heap (init to 1)
			 *	
			 *	`explicit` because `SharedPtr` is not implicitly convertible from raw `T*`
			 *	otherwise `void f(SharedPtr<T>)` and `f(new T)` will work silently
			 *	hence, losing track which raw ptrs own which smart ptrs
			*/
			explicit SharedPtr (
				T* raw
			) : ptr_(raw), refcount_(new long(1)) {
				log_indent();
				printf("[SharedPtr] explicit ctor(T*) refcount=1\n");
			}

			/*
			 *	copy ctor
			 *	shares the same object and the same refcount slot
			 *	bump it
			 *	this is the entire point of SharePtr{}, multiple ptr_ pointing to the same heap memory
			*/
			SharedPtr (
				const SharedPtr& other
			) : ptr_(other.ptr_), refcount_(other.refcount_) {
				if (refcount_ != nullptr) {
					++*refcount_;
				}

				log_indent();
				printf("[SharedPtr] copy ctor                 use_count=%ld\n", use_count());
			}

			/*
			 *	copy asign ctor
			 *	3 steps:
			 *		1. self asign guard
			 *		   i.e. same refcount_ slot => same owner group
			 *		2. release()
			 *		   releases current ownership
			 *		   may return ~T() dtor!!
			 *		3. mirror the copy ctor
			 *		   copy ptrs + bump refcount_
			 *
			 *	self-asign guard:
			 *		checks refcount_ ptrs because 2 SharedPtr{}s share refcount slot IFF they are part of same ownership group
			 *		comparing `this == &other` should also work
			 *		but recount_ comparison short-circuits `a = b` when `a` and `b` are 2 SharedPtr{}s that already share same target
			 *		no op + no churn on refcount_
			*/
			SharedPtr& operator = (
				const SharedPtr& other
			) {
				/*
				 *	i do not completely understand this???
				 *	what if two separate SharedPtr{} had the same refcount???
				 *	seems like this only considers if a copy happens within a scope that only has a single SharedPtr{} lifetime???
				*/
				if (this->refcount_ == other.refcount_) {
					return *this;
				}

				log_indent();
				printf("[SharedPtr] copy asign\n");
				g_log_indent++;
				release();

				ptr_ = other.ptr_;
				refcount_ = other.refcount_;

				if (refcount_ != nullptr) {
					++*refcount_;
					log_indent();
					printf("[SharedPtr] ++refcount=%ld\n", *refcount_);
				}

				g_log_indent--;

				return *this;
			}

			/*
			 *	move ctor
			 *	steal both ptrs + null the src
			 *	*NO refcount_ CHANGE*
			 *	total no. of owners unchanged
			 *	one owner relocated from 'other' to 'this'
			 *
			 *	noexcept
			 *		STL containers (e.g. Vector, etc...)
			 *		will only use a move ctor during internal re-alloc *IFF* it is 'noexcept'
			 *		otherwise they fall back to copy ctor
			 *		and a copy ctor costs atomic refcount bump per element
			 *		for SharedPtr{} that is wasteful! hence mark 'noexcept' and STL will use out move ctor
			*/
			SharedPtr (
				SharedPtr&& other
			) noexcept : ptr_(other.ptr_), refcount_(other.refcount_) {
				other.ptr_ = nullptr;
				other.refcount_ = nullptr;

				log_indent();
				printf("[SharedPtr] move ctor                 use_count=%ld\n", use_count());
			}

			/*
			 *	move asign
			 *
			 *	drop current
			 *	steal src
			 *	null src
			 *
			 *	self move guard:
			 *		x = std::move(x); must be safe (i.e. a no op)
			 *		comparing `this == &other` because self move asign we will release ourself then steal the buffers from ourself that we just NULLed
			*/
			SharedPtr& operator = (
				SharedPtr&& other
			) noexcept {
				if (this == &other) {
					return *this;
				}

				log_indent();
				printf("[SharedPtr] move asign\n");
				g_log_indent++;

				release();

				ptr_ = other.ptr_;
				refcount_ = other.refcount_;
				other.ptr_ = nullptr;
				other.refcount_ = nullptr;

				g_log_indent--;

				return *this;
			}

			/*
			 *	dtor
			 *	better than raw refcount convention 'kref' like in  C
			*/
			~SharedPtr () {
				log_indent();
				printf("[SharedPtr] dtor                      use_count=%ld\n", use_count());

				g_log_indent++;
				release();
				g_log_indent--;
			}

			/*
			 *	accessor methods
			*/
			T& operator * () const {
				return *ptr_;
			}

			T* operator -> () const {
				return ptr_;
			}

			T* get () const {
				return ptr_;
			}

			long use_count () const {
				return (refcount_) ? (*refcount_) : (0);
			} 
			
			explicit operator bool () const {
				return ptr_ != nullptr;
			}
		private:
			/*
			 *	release()
			 *	the only function that knows how to drop an owner
			 *	always called under g_log_indent++ from its caller (dtor/op=)
			 *
			 *	invariants:
			 *		- refcount_ = null
			 *		  we own nothing, nothing to do
			 *		- else
			 *		  decrement
			 *		  if we took it to zero, free both managed object *AND* refcount_ slot
			*/
			void release() {
				if (refcount_ == nullptr) {
					return;
				}
				--*refcount_;

				if (0 == *refcount_) {
					log_indent();
					printf("[SharedPtr] --refcount=0  -> delete ptr_; delete refcount_\n");
					g_log_indent++;
					delete ptr_;
					delete refcount_;
					g_log_indent--;
				} else {
					log_indent();
					printf("[SharedPtr] --refcount=%ld\n", *refcount_);
				}
			}
	};

/*
 *	Node -- the cycle subject for BLOCK_4.
 *	auto-incrementing static `s_next_id` so every `new Node{}` gets a unique
 *	sequential id without the caller passing one. id is initialized FIRST,
 *	then the SharedPtr<Node> `next` member runs its default ctor (which logs
 *	"[SharedPtr] default ctor (empty)"), then this struct's body prints
 *	"[Node{N}] default ctor". so the inner member ctor appears BEFORE the
 *	outer Node ctor line in the transcript -- that is the truth of C++
 *	construction order (members first, body second), do not be surprised.
*/
struct Node {
	static int s_next_id;
	int id;
	SharedPtr<Node> next;

	Node (
	) : id(s_next_id++) {
		log_indent();
		printf("[Node{%d}] default ctor\n", id);
	}

	Node (
		const Node& other
	) : id(s_next_id++), next(other.next) {
		log_indent();
		printf("[Node{%d}] copy ctor (from Node{%d})\n", id, other.id);
	}

	Node (
		Node&& other
	) noexcept : id(s_next_id++), next(std::move(other.next)) {
		log_indent();
		printf("[Node{%d}] move ctor (from Node{%d})\n", id, other.id);
	}

	Node& operator = (const Node&) = delete;
	Node& operator = (Node&&)      = delete;

	~Node () {
		log_indent();
		printf("[Node{%d}] dtor\n", id);
	}
};
int Node::s_next_id = 1;

int main () {
	#if defined(BLOCK_0)
		printf("Hello World\n");
	#endif

	#if defined(BLOCK_1)
		printf("\n==== BLOCK_1: SharedPtr<int> sanity ====\n\n");
		{
			SharedPtr<int> a(new int(42));
			{
				SharedPtr<int> b = a;
				{
					SharedPtr<int> c = b;
					printf("*a=%d *b=%d *c=%d\n", *a, *b, *c);
					printf("use_count: a=%ld b=%ld c=%ld\n",
						a.use_count(), b.use_count(), c.use_count());
				}
				printf("after c dies: a.use_count=%ld\n", a.use_count());
			}
			printf("after b dies: a.use_count=%ld\n", a.use_count());
		}
		printf("after a dies: (the heap int has been deleted)\n");
	#endif
	#if defined(BLOCK_2)
		printf("\n==== BLOCK_2: SharedPtr<Tracer> visibility ====\n\n");
		{
			SharedPtr<Tracer> a(new Tracer(1));
			SharedPtr<Tracer> b = a;
			SharedPtr<Tracer> c(new Tracer(2));
			b = c;
			SharedPtr<Tracer> d = std::move(c);
		}
	#endif
	#if defined(BLOCK_3)
		printf("\n==== BLOCK_3: double control block (run under -fsanitize=address) ====\n\n");
		printf("--- handing the SAME raw Tracer* to TWO SharedPtrs ---\n\n");
		{
			Tracer* raw = new Tracer(99);
			SharedPtr<Tracer> a(raw);
			SharedPtr<Tracer> b(raw);
		}
	#endif
	#if defined(BLOCK_4)
		printf("\n==== BLOCK_4: cycle leak (run under -fsanitize=address) ====\n\n");
		{
			SharedPtr<Node> a(new Node{});
			SharedPtr<Node> b(new Node{});
			a->next = b;
			b->next = a;
		}
	#endif
}
