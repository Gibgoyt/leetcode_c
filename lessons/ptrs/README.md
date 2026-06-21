# `shared_ptr` from first principles — 7-lesson ladder

> A C programmer's guide to `std::shared_ptr` by rebuilding it from scratch.
> This README is the spec: read it, then implement one lesson at a time.

---

## Who this is for

You are a C programmer. You write `malloc` / `free` in your sleep. You may
have written reference-counted resources by hand (think `kref` in the Linux
kernel, or refcounted DOM nodes in some C library). You know what a vtable is
because you've hand-rolled one with function pointers in a `struct`.

What `shared_ptr` is to you: a 16-byte struct on the stack that automates the
refcount bookkeeping you already do by convention, with a vtable to handle
custom destruction and a one-allocation optimization (`make_shared`) that
fuses the object and the bookkeeping into a single cache-friendly chunk.

That's it. Everything in `std::shared_ptr` is a small variation on that core.
This document drags every variation into the open, one lesson at a time, so
you can build them yourself and stop treating `shared_ptr` as magic.

---

## How to use this document

1. **Read sections 1–4** top-to-bottom. That's the mental model. No code.
2. **Pick the lesson you're on** (start at lesson 1). Read its section.
3. **Implement it.** The README gives you a detailed skeleton plus the
   expected printf transcripts for each `BLOCK_N`. Type the code yourself;
   do not copy-paste. The skeletons have `// TODO:` markers in the spots
   you should think through.
4. **Diff your output** against the expected transcripts. They are exact.
5. **Read "What's still broken"** at the end of the lesson, then move on.

A fresh Claude session can sit down with this README, ask you "which lesson
are you on?", read that section, and coach you through it without re-deriving
anything. That is the design goal.

---

## Pre-flight checklist (do this once before lesson 1)

The lesson source uses the libstdc++ shared_ptr code as a reference. You need
the raw source files, not GitHub's HTML render. Fetch them:

```bash
cd lessons/ptrs/1
wget https://raw.githubusercontent.com/gcc-mirror/gcc/refs/heads/master/libstdc%2B%2B-v3/include/bits/shared_ptr.h        -O shared_ptr.hpp
wget https://raw.githubusercontent.com/gcc-mirror/gcc/refs/heads/master/libstdc%2B%2B-v3/include/bits/shared_ptr_base.h   -O shared_ptr_base.hpp
wget https://raw.githubusercontent.com/gcc-mirror/gcc/refs/heads/master/libstdc%2B%2B-v3/include/bits/shared_ptr_atomic.h -O shared_ptr_atomic.hpp
```

Verify they're source, not HTML:

```bash
head -1 shared_ptr.hpp        # should print "// shared_ptr and weak_ptr implementation -*- C++ -*-"
head -1 shared_ptr_base.hpp   # should print "// shared_ptr atomic access -*- C++ -*-" or similar
```

If you see `<!DOCTYPE html>` you grabbed `github.com/blob/...` instead of
`raw.githubusercontent.com/...`. Try again.

The reference files are read-only context for Claude — you are not supposed
to copy them. They exist so Claude can point you at concrete line numbers
when you ask "but how does GNU do this?"

---

## House-style conventions (inherited from `dynamic_array/2`)

Every lesson directory looks like:

```
lessons/ptrs/N/
  main.cpp     # one file, multiple BLOCK_N sections gated by -D flags
  build.sh     # ./build.sh --flags BLOCK_0 [,FLAG2,...]
```

The conventions you must keep:

- **`BLOCK_N` `-D` flags.** One `main.cpp`, multiple `#if defined(BLOCK_N)`
  sections. Build one at a time. Lets you run a single experiment without
  recompiling the universe.
- **`Tracer` struct.** A struct whose every special member function prints
  what it is. Like the `Vertex` in `dynamic_array/2/main.cpp`. Lets you
  *see* every ctor, copy, move, dtor — interleaved with `SharedPtr`'s own
  logging. This is the entire point of the visual style.
- **`g_log_indent` depth tracking.** `static int g_log_indent;` plus a
  `log_indent()` helper. Methods bump on entry, drop on exit. Nested
  operations indent. The destruction cascade becomes readable.
- **`build.sh` `--flags X,Y,Z`.** Comma-separated, one arg. Expands to
  `-DX -DY -DZ`. Copy the script verbatim from `dynamic_array/2/build.sh`.
- **C++17.** Per `PLAN.md` and `lessons/Makefile`. Not c++23. Even though
  `dynamic_array/2/build.sh` uses c++23 locally, the lesson series target
  is c++17. `if constexpr`, `std::is_convertible_v`, `std::aligned_storage`
  are all available; `std::concepts` and `std::expected` are not.
- **`-fsanitize=address,undefined`.** Per the lessons Makefile. Several
  BLOCKs are designed to **trigger** ASan / UBSan deliberately so you can
  see what the bug looks like.

---

# Layer A — The mental model

## 1. TL;DR

A `shared_ptr<T>` is **two pointers on the stack**:

```
shared_ptr<T> {                  16 bytes on a 64-bit system.
    T*               ptr_;       Always.
    control_block*   ctrl_;      Regardless of T.
};
```

The control block lives **on the heap**:

```
control_block {
    long  strong_;               # of shared_ptrs pointing here
    long  weak_;                 # of weak_ptrs + (1 if strong_ > 0)
    virtual dispose();           kills the OBJECT  (when strong_ -> 0)
    virtual destroy();           kills the BLOCK   (when weak_   -> 0)
    virtual ~ctor();
};
```

Two refcounts because there are **two independent lifetimes**:

- The **managed object** dies when `strong_` reaches 0.
- The **control block itself** dies when `weak_` reaches 0.

That ordering matters: the object dies first; the control block can
outlive it because `weak_ptr` needs somewhere to ask "is the object still
alive?" without keeping it alive. The control block's only job in that
window is to truthfully answer "no, `strong_ == 0`, give up."

The **vtable** (`dispose` / `destroy`) is what lets one `shared_ptr<T>`
template instantiation work with:

- `new T` → `dispose` does `delete ptr_`
- `fopen(...)` with `fclose` deleter → `dispose` does `deleter_(ptr_)`
- `make_shared<T>(args...)` → `dispose` does `ptr_->~T()`, `destroy` does
  `::operator delete(this)` on the combined block

Three derived classes, one base, polymorphism handles the rest. Type
erasure via vtable. You've written this pattern in C with function
pointers; here the compiler writes the vtable for you.

`make_shared<T>(args...)` is the one optimization worth remembering:
**one heap allocation** holds both the control block and an in-place
`T` slot. Saves an allocation, halves the pointer-chasing.

Everything else in `<memory>` is bookkeeping around those facts.

---

## 2. Why `shared_ptr` exists

You already know `unique_ptr`. It's a `T*` wrapped in a class whose
destructor calls `delete`. One owner, transfer via `std::move`, scope
exit cleans up. This covers ~80% of dynamic ownership in real code.

`shared_ptr` is for the other 20%: cases where the lifetime of an
object can't be predicted at compile time because **multiple
independent parties hold references**, and you don't know which one
will release last.

Concrete cases:

- A worker thread holds a job descriptor; the main thread also holds
  it (to cancel). Whichever thread is last to drop its reference
  destroys the descriptor.
- A graph node has back-edges. Topological ordering of destruction
  isn't possible.
- A C library callback registration: you registered a function pointer
  with a `void*` userdata; the library will call you back at some
  arbitrary later time. The userdata must outlive any pending call.

The C reflex: `malloc` plus a `refcount` field plus `atomic_inc` /
`atomic_dec`, with the convention "every owner increments on take,
decrements on release." This works. The Linux kernel does it (`kref`).
And every shipping C codebase has the same bugs:

- Forgot to `put` on an error path → leak.
- `put` after `get` is on a different code path → double-free.
- Someone took a reference without `get`-ing → use-after-free.
- The `put` order between two refcounted objects causes a cycle leak;
  no one notices until production OOMs.

The whole point of `shared_ptr` is **the destructor never lies**. RAII
moves the `put` from "every author of every call site" to "the type
system." That's the trade.

### When NOT to use `shared_ptr`

`shared_ptr` has cost:

- Two pointers per instance (vs one for `unique_ptr` or a raw pointer).
- A heap allocation for the control block (unless `make_shared`).
- An atomic increment/decrement per copy/destroy (lesson 5).
- An indirect call through the vtable on destruction.

For hot paths, that overhead matters. The right ranking of alternatives:

1. **Stack value `T x;`** — no allocation, no refcount. Use it when
   the lifetime is scope-bound. The default.
2. **`unique_ptr<T>`** — one heap allocation, no refcount, transfer
   via `std::move`. Use it when the lifetime is dynamic but
   single-owner.
3. **`shared_ptr<T>`** — when multiple owners with independent
   lifetimes is genuinely the model. Not as a default.
4. **Raw owning pointer** — never (outside of one-line implementation
   details inside a class that wraps it).

`shared_ptr` is rarely the right answer in hot paths. (`PLAN.md`
lesson 8.) It is often the right answer in the cold setup/teardown
paths of a system.

---

## 3. The data structure in detail

### 3.1 Layout

```
   STACK                              HEAP
   -----                              ----

   shared_ptr<T> sp1;
   +-------------------+
   | ptr_   = 0xAAAA   |--------------------+
   | ctrl_  = 0xBBBB   |---------+          |
   +-------------------+         |          |
                                 v          v
   shared_ptr<T> sp2 = sp1;   +----------+ +----+
   +-------------------+      |  ctrl    | | T  |
   | ptr_   = 0xAAAA   |----->| strong=2 | |    |
   | ctrl_  = 0xBBBB   |--+   | weak  =1 | |    |
   +-------------------+  |   | vtable   | +----+
                          +-->|  dispose |
                              |  destroy |
                              +----------+
```

Two stack objects, one control block, one managed object. `sp1` and
`sp2` share the **same** `ctrl_`. Every copy bumps `strong_`. Every
destruction decrements it. When `strong_` hits 0, the control block's
`dispose()` runs and the managed object dies. The control block
itself sticks around until `weak_` hits 0.

### 3.2 Why two pointers, not one

You could imagine a design where `shared_ptr<T>` is **one** pointer:
`ctrl_` only, and the managed `T*` lives inside the control block.
You'd save 8 bytes per `shared_ptr`. Why don't we?

Because the **aliasing constructor** (lesson 7) needs `ptr_` and the
control block's managed pointer to be **different**. Like this:

```cpp
shared_ptr<Outer> outer = make_shared<Outer>();
shared_ptr<Inner> inner(outer, &outer->inner);
//                              ^^^^^^^^^^^^^ alias pointer
// inner.get() == &outer->inner
// inner's refcount keeps the WHOLE Outer alive
```

`inner` and `outer` share the same control block (same refcount), but
`inner.ptr_` points at `&outer->inner` while the control block still
"manages" the whole `Outer`. One pointer in the `shared_ptr` couldn't
express that. So we pay 8 bytes for the feature.

Also: `operator*` and `operator->` need to deref `ptr_` without
chasing through the control block. Caching `ptr_` on the stack is a
performance win for the common case.

### 3.3 The two refcounts and the +1 invariant

Why two refcounts?

- `strong_` = number of `shared_ptr` instances. Each owns the
  **object's lifetime**. When `strong_` reaches 0, dispose the
  object.
- `weak_` = number of `weak_ptr` instances PLUS one if `strong_ > 0`.
  Owns the **control block's lifetime**. When `weak_` reaches 0,
  destroy the control block.

The "PLUS one if `strong_ > 0`" is **the invariant** that makes the
whole thing work. The "strong group" collectively holds exactly one
weak reference. State machine:

```
   initial construct:
       strong_ = 1
       weak_   = 1                <-- the +1, "owned by the strong group"

   copy a shared_ptr:
       strong_++

   copy a weak_ptr:
       weak_++

   destroy a shared_ptr (not the last):
       strong_--

   destroy a shared_ptr (the LAST one, strong_ goes 1 -> 0):
       strong_--                  -> dispose() the object
       weak_--                    -> consume the +1
       (if weak_ now == 0: also destroy() the control block)

   destroy a weak_ptr:
       weak_--
       (if weak_ now == 0: destroy() the control block)
```

Without the +1, you'd have to special-case "is the control block
still needed?" every time `strong_` changes — and you'd race against
`weak_ptr::lock()` which expects to be able to check `strong_ > 0`
on a still-valid control block. The +1 collapses both questions
into "is `weak_ > 0`?".

### 3.4 Destruction timeline

A worked example. Start with three `shared_ptr`s and two `weak_ptr`s
to the same object:

```
   t=0    strong_=3   weak_=3        (3 + 1 for the strong group + er, wait)
```

Hold on. Let's redo this. `weak_` = (number of weak_ptrs) + (1 if
strong_ > 0). So three weak_ptrs would give... no, we have two
weak_ptrs:

```
   t=0    strong_=3   weak_=3        weak_ = 2 (weak_ptrs) + 1 (strong group)
```

Now sequence the destructions:

```
   t=1    sp3 destroyed:
          strong_ 3 -> 2
          weak_   unchanged (3)
   
   t=2    wp1 destroyed:
          strong_ unchanged (2)
          weak_   3 -> 2
   
   t=3    sp1 destroyed:
          strong_ 2 -> 1
          weak_   unchanged (2)
   
   t=4    sp2 destroyed (THE LAST STRONG):
          strong_ 1 -> 0
          ---> dispose()  <- the OBJECT dies HERE
          weak_   2 -> 1   (consume the +1)
   
   t=5    wp2 destroyed:
          weak_   1 -> 0
          ---> destroy()  <- the CONTROL BLOCK dies HERE
```

Two distinct moments: t=4 (object dies) and t=5 (control block
freed). Anything checking `wp2.expired()` between t=4 and t=5 sees
`true` and returns an empty `shared_ptr` from `.lock()` — that's the
whole reason the control block has to outlive the object.

### 3.5 The vtable

`dispose()` and `destroy()` are virtual. Why?

Because one `shared_ptr<T>` instantiation has to handle:

```
   new T(args)                  -> dispose:  delete ptr_
                                   destroy:  delete this
   
   raw pointer + deleter        -> dispose:  deleter_(ptr_)
                                   destroy:  delete this   (or alloc.destroy + dealloc)
   
   make_shared<T>(args)         -> dispose:  ptr_->~T()
                                   destroy:  ::operator delete(this)
                                             (frees one combined block)
   
   allocate_shared<T,Alloc>     -> dispose:  ptr_->~T()
                                   destroy:  alloc.deallocate(this)
```

All four share the same `shared_ptr<T>` type. The only difference is
**how to kill things**. That information is captured at construction
time and stored in the vtable of the control block subclass that was
chosen. The `shared_ptr<T>` instance itself doesn't know which subclass
it's pointing at; it just calls `ctrl_->dispose()` and lets the vtable
sort it out.

You've written this in C as:

```c
struct ctrl_vtable {
    void (*dispose)(struct ctrl*);
    void (*destroy)(struct ctrl*);
};
struct ctrl {
    long strong, weak;
    const struct ctrl_vtable *vt;
    /* ... per-subtype fields ... */
};
```

Same thing. The C++ version is shorter because the compiler emits the
vtable for you.

---

## 4. The five invariants

If at any point your implementation is misbehaving, walk these five.
Whichever one you've broken is the bug.

1. **Object alive ⟺ `strong_ > 0`.** No exceptions. If `strong_` is
   0, do not deref `ptr_`. If you've kept a raw `T*` somewhere and
   dereferenced it after `strong_` hit 0, that's a use-after-free.

2. **Control block alive ⟺ `weak_ > 0`.** Once `weak_` hits 0, the
   control block is `delete`d. Don't read `strong_` or `weak_` after
   that.

3. **The strong group holds exactly one weak reference.** When
   `strong_` transitions `0 -> N`, also `weak_++` (in practice this
   only happens at first construction, where we initialize both to
   1). When `strong_` transitions `N -> 0`, also `weak_--`. Forget
   this and weak_ptr breaks.

4. **Refcount mutations are atomic** (lesson 5). Two threads
   destroying the last two `shared_ptr` copies must not both see
   `strong_ == 1` before decrementing.

5. **`weak_ptr::lock()` is a CAS loop, not `++strong_`.** Because
   `strong_` may already be 0 by the time we observe it. We must
   atomically check "is `strong_` still > 0?" AND increment in one
   step.

These five facts are the entire correctness argument for
`shared_ptr`. Memorize them.

---

# Layer B — The lesson ladder

## 5. Lessons overview

| # | Dir | Topic | What it adds |
|---|---|---|---|
| 1 | `ptrs/1/` | Naïve refcount | `SharedPtr<T>` = `{T*, long*}`. Two allocations. Copy bumps, dtor decrements. |
| 2 | `ptrs/2/` | `ControlBlock` + `WeakPtr` | Extract refcount into a struct, add second refcount, build `WeakPtr` w/ `lock()`. |
| 3 | `ptrs/3/` | Type erasure + deleters | Promote `ControlBlock` to virtual base. Subclasses for `new`/`delete`, custom deleter. |
| 4 | `ptrs/4/` | `make_shared` | New subclass with in-place T slot. One allocation. Forwarded ctor args. |
| 5 | `ptrs/5/` | Atomics | `std::atomic<long>` refcounts. Memory orders. CAS for `lock()`. |
| 6 | `ptrs/6/` | `enable_shared_from_this` | Mixin so a `T` can hand out `SharedPtr<T>` to itself without a second control block. |
| 7 | `ptrs/7/` | Aliasing constructor | `SharedPtr<U>` pointing at a sub-object of a managed `T`. Why `ptr_` and managed pointer can differ. |

Each lesson is a complete, runnable `main.cpp`. Each builds on the
last but stands alone (i.e. copy the previous lesson's code as
starting point, then evolve).

---

## 6. Lesson 1 — Naïve refcount

### 6.1 Where we are

Nothing exists yet. We have not yet built any smart pointer. We're
going to build the simplest thing that could possibly work: a
template class that owns a `T*` and a `long*` refcount, both
heap-allocated. Copy ctor bumps the refcount. Dtor decrements. When
the count hits zero, both pointers get `delete`d.

This is the strawman. It will be broken in three known ways at the
end. The next six lessons fix them one at a time.

### 6.2 Data structure

```
   stack:                              heap:
   +-------------------+
   | SharedPtr<T>      |               +-----+      +---+
   | ptr_      = ...   |-------------->|  T  |      |   |
   | refcount_ = ...   |---------------|     |   +->| 2 |  long
   +-------------------+               +-----+   |  +---+
                                                 |
   SharedPtr<T> b = a;                           |
   +-------------------+                         |
   | ptr_      = ...   |---------------+         |
   | refcount_ = ...   |-----+         |         |
   +-------------------+     |         +---------+
                             +-------------------+
```

Two heap allocations per managed object. Both `SharedPtr` instances
point at the same `T` and the same `long` refcount.

### 6.3 Operations

```
default ctor:               ptr_ = nullptr, refcount_ = nullptr
explicit ctor(T* raw):      ptr_ = raw, refcount_ = new long(1)
copy ctor(const&):          copy both pointers; if (refcount_) ++*refcount_
copy assign(const&):        guard self-assign; release(); copy ptrs; ++*refcount_
move ctor(&&):              steal both pointers; null out source
move assign(&&):            guard self-move; release(); steal; null source
dtor:                       release()

release() (private):
    if (refcount_ && --*refcount_ == 0) {
        delete ptr_;
        delete refcount_;
    }

accessors:
    T& operator*()  const { return *ptr_; }
    T* operator->() const { return ptr_; }
    T* get()        const { return ptr_; }
    long use_count() const { return refcount_ ? *refcount_ : 0; }
    explicit operator bool() const { return ptr_ != nullptr; }
```

### 6.4 Code skeleton

```cpp
/*
 *  lessons/ptrs/1/main.cpp
 *
 *  the naïve shared pointer
 *  - two heap allocations per managed object: one for T, one for long
 *  - copy ctor bumps refcount, dtor decrements, hits zero -> delete both
 *  - no weak_ptr, no custom deleters, no make_shared, no atomics
 *
 *  three BLOCKs:
 *    BLOCK_0   T=int       sanity check on refcount arithmetic
 *    BLOCK_1   T=Tracer    full ctor/dtor visibility, one dtor per object
 *    BLOCK_2   bugs        double-ctrl-block double-free + cycle leak
*/

#include <iostream>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <utility>

static int g_log_indent = 0;
static void log_indent() {
    for (int i = 0; i < g_log_indent; i++) putchar('\t');
}

struct Tracer {
    int id;

    Tracer(int in_id) : id(in_id) {
        log_indent();
        printf("[Tracer{%d}] parametized ctor\n", id);
    }
    Tracer(const Tracer& o) : id(o.id) {
        log_indent();
        printf("[Tracer{%d}] copy ctor\n", id);
    }
    Tracer(Tracer&& o) noexcept : id(o.id) {
        log_indent();
        printf("[Tracer{%d}] move ctor\n", id);
    }
    Tracer& operator=(const Tracer&) = delete;
    Tracer& operator=(Tracer&&) = delete;
    ~Tracer() {
        log_indent();
        printf("[Tracer{%d}] dtor\n", id);
    }
};

template<typename T>
class SharedPtr {
    private:
        T*    ptr_      = nullptr;
        long* refcount_ = nullptr;

        void release() {
            // TODO: implement
            //   if refcount_ is null we own nothing, return
            //   --*refcount_
            //   if it just hit zero: delete ptr_; delete refcount_
            //   printf which path we took, with the refcount value
        }

    public:
        SharedPtr() {
            log_indent();
            printf("[SharedPtr] default ctor (empty)\n");
        }

        explicit SharedPtr(T* raw) : ptr_(raw), refcount_(new long(1)) {
            log_indent();
            printf("[SharedPtr] explicit ctor(T*) refcount=1\n");
        }

        SharedPtr(const SharedPtr& other) {
            // TODO: implement
            //   copy ptr_ and refcount_
            //   if refcount_ non-null, ++*refcount_
            //   printf the new use_count
        }

        SharedPtr& operator=(const SharedPtr& other) {
            // TODO: implement
            //   guard against self-assign (compare refcount_ pointers)
            //   release() current
            //   copy other's pointers
            //   if refcount_ non-null, ++*refcount_
            //   return *this
        }

        SharedPtr(SharedPtr&& other) noexcept {
            // TODO: implement
            //   steal ptr_ and refcount_
            //   null out other
        }

        SharedPtr& operator=(SharedPtr&& other) noexcept {
            // TODO: implement
            //   guard self-move
            //   release() current
            //   steal from other
            //   null out other
            //   return *this
        }

        ~SharedPtr() {
            log_indent();
            printf("[SharedPtr] dtor use_count=%ld\n", use_count());
            g_log_indent++;
            release();
            g_log_indent--;
        }

        T& operator*()  const { return *ptr_; }
        T* operator->() const { return ptr_; }
        T* get()        const { return ptr_; }
        long use_count() const { return refcount_ ? *refcount_ : 0; }
        explicit operator bool() const { return ptr_ != nullptr; }
};

int main() {
    #if defined(BLOCK_0)
        printf("\n==== BLOCK_0: SharedPtr<int> sanity ====\n\n");
        // TODO:
        //   SharedPtr<int> a(new int(42));     expect use_count=1
        //   { SharedPtr<int> b = a;            expect use_count=2
        //     { SharedPtr<int> c = b;          expect use_count=3
        //       printf("*a=%d *b=%d *c=%d\n", ...);
        //     }                                expect use_count=2 after c dies
        //   }                                  expect use_count=1 after b dies
        // a dies at end of scope, refcount->0, int deleted
    #elif defined(BLOCK_1)
        printf("\n==== BLOCK_1: SharedPtr<Tracer> visibility ====\n\n");
        // TODO:
        //   {
        //     SharedPtr<Tracer> a(new Tracer(1));
        //     SharedPtr<Tracer> b = a;
        //     SharedPtr<Tracer> c(new Tracer(2));
        //     b = c;                  // b drops Tracer(1) (refcount->1)
        //                             // then b takes Tracer(2) (refcount->2)
        //     SharedPtr<Tracer> d = std::move(c);   // c becomes empty
        //   }
        // expect: exactly one Tracer(1) dtor and one Tracer(2) dtor.
    #elif defined(BLOCK_2)
        printf("\n==== BLOCK_2: the two bugs that motivate lessons 2 and 6 ====\n\n");

        printf("--- bug 1: double control block (run under -fsanitize=address) ---\n");
        // TODO:
        //   Tracer* raw = new Tracer(99);
        //   SharedPtr<Tracer> a(raw);
        //   SharedPtr<Tracer> b(raw);   // BUG: second independent refcount
        //   // a dies -> deletes raw -> b's refcount block still thinks it owns raw
        //   // b dies -> double-free, ASan should yell
        //   (motivates lesson 6: enable_shared_from_this)

        printf("\n--- bug 2: cycle leak (run under -fsanitize=address) ---\n");
        // TODO:
        //   struct Node { SharedPtr<Node> next; int v; };
        //   {
        //     SharedPtr<Node> a(new Node{});
        //     SharedPtr<Node> b(new Node{});
        //     a->next = b;
        //     b->next = a;     // cycle
        //   }
        //   // scope exit: each refcount stays at 1 forever -> leak
        //   // ASan reports it on exit
        //   (motivates lesson 2: weak_ptr)
    #endif
}
```

### 6.5 BLOCKs — expected output transcripts

**`BLOCK_0`** (`./build.sh --flags BLOCK_0; ./main`):

```
==== BLOCK_0: SharedPtr<int> sanity ====

[SharedPtr] explicit ctor(T*) refcount=1
[SharedPtr]   copy ctor               use_count=2
[SharedPtr]     copy ctor             use_count=3
*a=42 *b=42 *c=42
[SharedPtr]     dtor use_count=3
	[SharedPtr]   --refcount=2
[SharedPtr]   dtor use_count=2
	[SharedPtr] --refcount=1
[SharedPtr] dtor use_count=1
	[SharedPtr] --refcount=0  -> delete ptr_; delete refcount_
```

(Exact indentation depends on how you printf inside `release()`; mirror
`dynamic_array/2`'s style. The key invariant: only **one** "delete
ptr_; delete refcount_" line ever appears.)

**`BLOCK_1`** (`./build.sh --flags BLOCK_1; ./main`):

```
==== BLOCK_1: SharedPtr<Tracer> visibility ====

[Tracer{1}] parametized ctor
[SharedPtr] explicit ctor(T*) refcount=1
[SharedPtr] copy ctor                 use_count=2
[Tracer{2}] parametized ctor
[SharedPtr] explicit ctor(T*) refcount=1
[SharedPtr] copy assign
	[SharedPtr] --refcount=1                <- b drops Tracer(1), refcount 2 -> 1
	[SharedPtr] ++refcount=2                <- b takes Tracer(2)
[SharedPtr] move ctor                 (d steals c, c becomes empty)
[SharedPtr] dtor use_count=2            <- d dies
	[SharedPtr] --refcount=1
[SharedPtr] dtor use_count=0            <- c dies (was emptied by move)
[SharedPtr] dtor use_count=1            <- b dies
	[SharedPtr] --refcount=0  -> delete ptr_; delete refcount_
[Tracer{2}] dtor
[SharedPtr] dtor use_count=1            <- a dies
	[SharedPtr] --refcount=0  -> delete ptr_; delete refcount_
[Tracer{1}] dtor
```

Verify: exactly **one** `Tracer{1}` dtor, exactly **one** `Tracer{2}`
dtor. Both run when their respective refcount hits 0.

**`BLOCK_2`** (`./build.sh --flags BLOCK_2; ./main`):

```
==== BLOCK_2: the two bugs that motivate lessons 2 and 6 ====

--- bug 1: double control block (run under -fsanitize=address) ---
[Tracer{99}] parametized ctor
[SharedPtr] explicit ctor(T*) refcount=1
[SharedPtr] explicit ctor(T*) refcount=1     <- SECOND independent refcount block!
[SharedPtr] dtor use_count=1
	[SharedPtr] --refcount=0  -> delete ptr_; delete refcount_
[Tracer{99}] dtor
[SharedPtr] dtor use_count=1
	[SharedPtr] --refcount=0  -> delete ptr_; delete refcount_

=================================================================
==xxxxx==ERROR: AddressSanitizer: heap-use-after-free on address ...
   double-free of 'Tracer*' or analogous
```

ASan's exact output varies. The point: when both `SharedPtr`s try to
delete the same `Tracer{99}`, you get a double-free. **Fix in lesson
6** with `enable_shared_from_this`, plus the discipline of using
`make_shared` (lesson 4) so you never hand a raw pointer to two
`SharedPtr`s in the first place.

```
--- bug 2: cycle leak (run under -fsanitize=address) ---
[SharedPtr] explicit ctor(T*) refcount=1
[SharedPtr] explicit ctor(T*) refcount=1
[SharedPtr] copy assign                 <- a->next = b
	[SharedPtr] ++refcount=2
[SharedPtr] copy assign                 <- b->next = a
	[SharedPtr] ++refcount=2
[SharedPtr] dtor use_count=2            <- outer b dies, refcount->1
	[SharedPtr] --refcount=1
[SharedPtr] dtor use_count=2            <- outer a dies, refcount->1
	[SharedPtr] --refcount=1
                                        <- nothing else fires; refcounts stuck at 1
=================================================================
==xxxxx==ERROR: LeakSanitizer: detected memory leaks
   Direct leak of N bytes in 2 allocations
```

Both `Node`s leak forever. **Fix in lesson 2** with `WeakPtr` so the
back-edge doesn't keep the forward-edge alive.

### 6.6 What this lesson teaches

- The data structure is **just two pointers**. There is no magic.
- The copy ctor / dtor pair is **the entire RAII contract** for
  shared ownership.
- Manual refcounting works exactly as you'd expect from C, with one
  long extra heap allocation.
- The two bugs (double-control-block, cycle leak) are not bugs of
  this naïve design specifically — they are **fundamental** and
  every shared-ownership scheme has to solve them. Lessons 2 and 6
  solve them.

### 6.7 What's still broken

1. **No `weak_ptr`.** Can't observe an object without keeping it
   alive. Lesson 2.
2. **Cycles leak.** Direct consequence of (1). Lesson 2.
3. **Can't customize destruction.** `delete` is hard-coded. No
   `fclose`, no `free`, no `delete[]`. Lesson 3.
4. **Two allocations per object.** Wasteful. Lesson 4.
5. **Not thread-safe.** Two threads decrementing race. Lesson 5.
6. **Can't take a `SharedPtr` to `*this`.** No way for a member
   function to obtain a `SharedPtr` to the object it belongs to
   without spawning a second control block (the BLOCK_2 bug).
   Lesson 6.
7. **Can't alias.** No way to have a `SharedPtr<Inner>` that keeps a
   `SharedPtr<Outer>` alive. Lesson 7.

### 6.8 Map to libstdc++

This naïve version doesn't really correspond to anything in
`shared_ptr_base.h` because libstdc++ goes straight to the virtual
control block. Treat lesson 1 as a deliberate strawman.

---

## 7. Lesson 2 — `ControlBlock` + `WeakPtr`

### 7.1 Where we are

Lesson 1 gave us a working `SharedPtr` with one refcount living in a
loose `long*`. Two bugs left from BLOCK_2: cycle leak and the
need for `weak_ptr` to observe without owning.

This lesson:

1. Refactors `long*` into a struct `ControlBlock { long strong; long weak; }`.
2. Adds a `WeakPtr<T>` class with the **same** `{T*, ControlBlock*}`
   layout as `SharedPtr<T>`.
3. Establishes the +1 invariant explicitly in code.
4. Implements `lock()` and `expired()`.

By end of lesson, BLOCK_1 BLOCK_2's cycle leak is fixed (using
`WeakPtr` for back-edges). BLOCK_1 BLOCK_2's double-control-block
bug is **not** fixed yet — that's lesson 6.

### 7.2 Data structure

```
   stack:                              heap:
   +-------------------+
   | SharedPtr<T> sp   |               +-----+      +-----------+
   | ptr_      = ...   |-------------->|  T  |      | ctrl      |
   | ctrl_     = ...   |---------------+      \     | strong=2  |
   +-------------------+                +-----+----->| weak  =2  |
                                                    +-----------+
   +-------------------+                            ^
   | WeakPtr<T>   wp   |                            |
   | ptr_      = ...   |---------------------+      |
   | ctrl_     = ...   |--------------------------->+
   +-------------------+                     |
                                       (no T deref allowed
                                        from a WeakPtr;
                                        ptr_ is just a cache)
```

`ControlBlock` carries **both** refcounts. `WeakPtr` and `SharedPtr`
have identical memory layout — `WeakPtr` differs only in which
refcount it bumps (`weak`) and in what its destructor does (never
`dispose`s the object).

`weak` count above is 2 because: 1 from the actual `WeakPtr wp` +
the +1 because `strong > 0`.

### 7.3 Operations

`ControlBlock`:

```
struct ControlBlock {
    long strong = 1;        // initial: one SharedPtr was just constructed
    long weak   = 1;        // initial: the strong group's +1
};
```

`SharedPtr<T>` — same as lesson 1 but route refcount through `ctrl_`:

```
explicit ctor(T*):
    ptr_  = raw
    ctrl_ = new ControlBlock{}     // strong=1, weak=1
    (the weak=1 here is the +1 invariant in action)

copy ctor / copy assign:
    increment ctrl_->strong

release() (private):
    if (!ctrl_) return
    if (--ctrl_->strong == 0) {
        delete ptr_                              // dispose the object
        if (--ctrl_->weak == 0) delete ctrl_     // consume the +1; maybe free the block
    }
```

Notice: when `strong` hits 0 we **always** decrement `weak` (because
we're consuming the strong group's +1). We only `delete ctrl_` if
that decrement *also* takes `weak` to 0.

`WeakPtr<T>`:

```
default ctor:                       ptr_ = nullptr, ctrl_ = nullptr
ctor(const SharedPtr<T>& sp):       copy both pointers; if ctrl_, ++ctrl_->weak
copy ctor(const WeakPtr&):          copy both pointers; if ctrl_, ++ctrl_->weak
copy assign:                        release(); copy; ++weak
move ctor / move assign:            steal pointers; null source
dtor:                               release_weak()
expired() const:                    return !ctrl_ || ctrl_->strong == 0
SharedPtr<T> lock() const:
    if (!ctrl_ || ctrl_->strong == 0) return SharedPtr<T>();
    // single-threaded version: safe to just construct
    return SharedPtr<T>(ptr_, ctrl_, /*adopt=*/true);
    //                                ^^^ private ctor that bumps strong
                                        and doesn't allocate a new block

release_weak():
    if (!ctrl_) return
    if (--ctrl_->weak == 0) delete ctrl_
```

`SharedPtr` needs a new **private** constructor used by `WeakPtr::lock()`:

```
SharedPtr(T* p, ControlBlock* c, adopt_tag):
    ptr_ = p
    ctrl_ = c
    if (ctrl_) ++ctrl_->strong       // promotes the weak observation to strong
```

`WeakPtr` is `friend class SharedPtr<T>` and vice versa, so they can
poke each other's privates. Or expose a couple of helpers.

### 7.4 Code skeleton

```cpp
// lessons/ptrs/2/main.cpp

#include <cstdio>
#include <utility>

static int g_log_indent = 0;
static void log_indent() { for (int i = 0; i < g_log_indent; i++) putchar('\t'); }

struct Tracer { /* same as lesson 1 */ };

struct ControlBlock {
    long strong = 1;
    long weak   = 1;

    ControlBlock() {
        log_indent();
        printf("[ControlBlock] ctor strong=1 weak=1\n");
    }
    ~ControlBlock() {
        log_indent();
        printf("[ControlBlock] dtor (final destroy)\n");
    }
};

template<typename T> class WeakPtr;   // fwd decl

template<typename T>
class SharedPtr {
    template<typename U> friend class WeakPtr;
    private:
        T*            ptr_  = nullptr;
        ControlBlock* ctrl_ = nullptr;

        struct adopt_tag {};
        SharedPtr(T* p, ControlBlock* c, adopt_tag) : ptr_(p), ctrl_(c) {
            if (ctrl_) {
                ++ctrl_->strong;
                log_indent();
                printf("[SharedPtr] adopt ctor strong=%ld weak=%ld\n",
                       ctrl_->strong, ctrl_->weak);
            }
        }

        void release() {
            // TODO:
            //   if !ctrl_ return
            //   --ctrl_->strong, printf
            //   if strong now == 0:
            //       delete ptr_   <-- THE OBJECT DIES
            //       --ctrl_->weak (consume the +1)
            //       if weak now == 0: delete ctrl_   <-- THE BLOCK DIES
        }

    public:
        SharedPtr() = default;
        explicit SharedPtr(T* raw)
            : ptr_(raw), ctrl_(raw ? new ControlBlock{} : nullptr) {
            log_indent();
            printf("[SharedPtr] explicit ctor(T*) strong=%ld weak=%ld\n",
                   ctrl_ ? ctrl_->strong : 0, ctrl_ ? ctrl_->weak : 0);
        }
        // copy ctor / copy assign / move ctor / move assign / dtor — TODO
        // accessors: get(), operator*, operator->, use_count(), bool

        WeakPtr<T> make_weak() const;   // convenience (optional)
};

template<typename T>
class WeakPtr {
    template<typename U> friend class SharedPtr;
    private:
        T*            ptr_  = nullptr;
        ControlBlock* ctrl_ = nullptr;

        void release_weak() {
            // TODO: similar to SharedPtr::release but only decrement weak
        }

    public:
        WeakPtr() = default;

        WeakPtr(const SharedPtr<T>& sp) : ptr_(sp.ptr_), ctrl_(sp.ctrl_) {
            if (ctrl_) {
                ++ctrl_->weak;
                log_indent();
                printf("[WeakPtr] ctor(SharedPtr) strong=%ld weak=%ld\n",
                       ctrl_->strong, ctrl_->weak);
            }
        }
        // copy ctor / copy assign / move ctor / move assign / dtor — TODO

        bool expired() const { return !ctrl_ || ctrl_->strong == 0; }

        SharedPtr<T> lock() const {
            if (!ctrl_ || ctrl_->strong == 0) {
                log_indent();
                printf("[WeakPtr] lock() -> EXPIRED, returning empty SharedPtr\n");
                return SharedPtr<T>();
            }
            log_indent();
            printf("[WeakPtr] lock() -> alive, promoting strong=%ld\n",
                   ctrl_->strong);
            return SharedPtr<T>(ptr_, ctrl_, typename SharedPtr<T>::adopt_tag{});
        }
};

int main() {
    #if defined(BLOCK_0)
        // basic SharedPtr/WeakPtr lifecycle
        // TODO:
        //   SharedPtr<Tracer> sp(new Tracer(1));
        //   WeakPtr<Tracer> wp(sp);
        //   printf("expired? %d\n", wp.expired());      // 0
        //   { SharedPtr<Tracer> sp2 = wp.lock(); }      // strong 1->2->1
        //   sp = SharedPtr<Tracer>();                    // drop strong
        //   printf("expired? %d\n", wp.expired());      // 1
        //   { SharedPtr<Tracer> sp3 = wp.lock(); }      // returns empty
        //   // wp goes out of scope, control block dies HERE
    #elif defined(BLOCK_1)
        // cycle FIXED with WeakPtr
        // struct Node { SharedPtr<Node> next; WeakPtr<Node> back; int v; };
        // TODO:
        //   {
        //     SharedPtr<Node> a(new Node{});
        //     SharedPtr<Node> b(new Node{});
        //     a->next = b;
        //     b->back = a;            // <-- weak back-edge: no leak
        //   }
        //   // scope exit: both Nodes free correctly, no leak under ASan
    #elif defined(BLOCK_2)
        // WeakPtr outlives SharedPtr — control block stays alive between
        // dispose and final destroy.
        // TODO:
        //   WeakPtr<Tracer> wp;
        //   {
        //     SharedPtr<Tracer> sp(new Tracer(7));
        //     wp = WeakPtr<Tracer>(sp);
        //     printf("inside scope: strong=%ld weak=%ld\n", ...);
        //   }
        //   // EXPECT here: Tracer dtor has fired (object dies)
        //   //              control block dtor has NOT fired
        //   printf("after scope, before wp dies: expired=%d\n", wp.expired());
        //   // now wp goes out of scope -> control block dtor fires HERE
    #endif
}
```

### 7.5 BLOCKs — expected output transcripts

**`BLOCK_0`** — basic lifecycle:

```
[Tracer{1}] parametized ctor
[ControlBlock] ctor strong=1 weak=1
[SharedPtr] explicit ctor(T*) strong=1 weak=1
[WeakPtr] ctor(SharedPtr) strong=1 weak=2
expired? 0
[WeakPtr] lock() -> alive, promoting strong=1
[SharedPtr] adopt ctor strong=2 weak=2
[SharedPtr] dtor strong=2 weak=2
	[SharedPtr] --strong=1
[SharedPtr] copy assign        <- sp = SharedPtr<Tracer>() drops sp's object
	[SharedPtr] --strong=0  -> delete ptr_
[Tracer{1}] dtor               <- OBJECT dies (strong hit 0)
	[SharedPtr] --weak=1   <- consume the +1
expired? 1
[WeakPtr] lock() -> EXPIRED, returning empty SharedPtr
[WeakPtr] dtor strong=0 weak=1
	[WeakPtr] --weak=0
[ControlBlock] dtor (final destroy)
```

Verify the two distinct moments: `Tracer{1}` dtor fires when `strong`
hits 0; `ControlBlock` dtor fires later, when `weak` hits 0.

**`BLOCK_1`** — cycle fixed:

```
[Tracer]/[ControlBlock] ctors x 2 for Node a and Node b
... weak back-edge: a->next = b bumps strong; b->back = a bumps weak only ...
[outer b dies]    strong b 1->0 -> b destroyed -> dispose Node b -> --weak
[outer a dies]    strong a 2->1 (still has the now-dead b->back reference)
                  strong a 1->0 -> a destroyed -> dispose Node a -> --weak
[ControlBlock dtor x 2]   both blocks freed, no leak
==xxxxx==NO ERROR DETECTED
```

Compare to lesson 1 BLOCK_2 bug 2 (which leaked forever): the *only*
change is `back` is `WeakPtr` not `SharedPtr`. Identical semantics
except the cycle doesn't keep the refcount alive.

**`BLOCK_2`** — WeakPtr outlives SharedPtr:

```
[Tracer{7}] parametized ctor
[ControlBlock] ctor strong=1 weak=1
[SharedPtr] explicit ctor(T*) strong=1 weak=1
[WeakPtr] ctor(SharedPtr) strong=1 weak=2
inside scope: strong=1 weak=2
[SharedPtr] dtor strong=1 weak=2
	[SharedPtr] --strong=0  -> delete ptr_
[Tracer{7}] dtor                       <-- OBJECT DIES here, at end of inner scope
	[SharedPtr] --weak=1              <-- BUT control block lives on
after scope, before wp dies: expired=1
[WeakPtr] dtor strong=0 weak=1
	[WeakPtr] --weak=0
[ControlBlock] dtor (final destroy)     <-- CONTROL BLOCK DIES here, only now
```

This is the two-tier lifetime made visible. The `Tracer{7}` dtor and
`ControlBlock` dtor are separated in time by the `WeakPtr`'s lifetime.

### 7.6 What this lesson teaches

- Two refcounts, two lifetimes, the +1 invariant ties them together.
- `WeakPtr` is layout-compatible with `SharedPtr` and only differs in
  *which counter it bumps* and *what its destructor does*.
- `lock()` is a single-threaded "if (strong > 0) bump and return" —
  the atomic CAS version comes in lesson 5.
- Cycles are fixed by making back-edges `WeakPtr`. This is the
  canonical use of `WeakPtr` in real code.

### 7.7 What's still broken

- `delete ptr_` is still hard-coded. No custom deleters. Lesson 3.
- Still two heap allocations. Lesson 4.
- Still not thread-safe. Lesson 5.
- Double-control-block bug still un-fixed. Lesson 6.

### 7.8 Map to libstdc++

- `ControlBlock` corresponds to `_Sp_counted_base` in
  `shared_ptr_base.h`. Look for `_M_use_count` (our `strong`) and
  `_M_weak_count` (our `weak`).
- The +1 invariant is implemented in `_Sp_counted_base::_M_release()`
  and friends — search for `_M_weak_release()`.
- `lock()` is `__weak_ptr::lock()` in the header; the
  single-threaded path is `_M_add_ref_lock_nothrow` for
  `_S_single`.

---

## 8. Lesson 3 — Type erasure + custom deleters

### 8.1 Where we are

Lesson 2 fixed `weak_ptr` and cycles, but `release()` still hard-codes
`delete ptr_`. That doesn't work for:

- `FILE*` from `fopen()` (needs `fclose`)
- `int[]` from `new int[N]` (needs `delete[]`)
- `void*` from `mmap()` (needs `munmap`)
- Anything from a C library with a custom free function

We need to capture the "how to destroy" at construction time and store
it in the control block. The standard tool for this is **type erasure
via a virtual base class**.

### 8.2 Data structure

`ControlBlock` becomes an abstract base. Two virtual methods. Two
derived classes (for now).

```
                  +-----------------+
                  | _Sp_counted_base|   abstract; holds the refcounts
                  | strong          |   and the vtable
                  | weak            |
                  | + dispose() = 0 |
                  | + destroy()     |   non-pure: default `delete this`
                  | + ~ctor virtual |
                  +--------+--------+
                           ^
              +------------+--------------+
              |                           |
   +----------+-----------+   +-----------+-------------------+
   | _Sp_counted_ptr<Ptr> |   | _Sp_counted_deleter<Ptr,Del>  |
   | Ptr ptr_             |   | Ptr ptr_                      |
   | dispose: delete ptr_ |   | Del del_                      |
   +----------------------+   | dispose: del_(ptr_)           |
                              +-------------------------------+
```

The `SharedPtr<T>` itself now holds `_Sp_counted_base*` instead of
`ControlBlock*`. The virtual dispatch hides which subtype is on the
other end of the pointer.

### 8.3 Operations

```cpp
struct _Sp_counted_base {
    long strong = 1;
    long weak   = 1;

    virtual void dispose() noexcept = 0;       // kill the OBJECT
    virtual void destroy() noexcept {          // kill THIS control block
        delete this;
    }
    virtual ~_Sp_counted_base() = default;

    // (release / release_weak helpers as before, but call virtual
    // dispose() / destroy() instead of the hard-coded delete)
};

template<typename Ptr>
struct _Sp_counted_ptr : _Sp_counted_base {
    Ptr ptr_;
    explicit _Sp_counted_ptr(Ptr p) : ptr_(p) {}
    void dispose() noexcept override { delete ptr_; }
};

template<typename Ptr, typename Deleter>
struct _Sp_counted_deleter : _Sp_counted_base {
    Ptr     ptr_;
    Deleter del_;
    _Sp_counted_deleter(Ptr p, Deleter d) : ptr_(p), del_(std::move(d)) {}
    void dispose() noexcept override { del_(ptr_); }
};
```

`SharedPtr<T>` constructors:

```cpp
explicit SharedPtr(T* raw)
    : ptr_(raw),
      ctrl_(raw ? new _Sp_counted_ptr<T*>(raw) : nullptr) {}

template<typename Deleter>
SharedPtr(T* raw, Deleter d)
    : ptr_(raw),
      ctrl_(raw ? new _Sp_counted_deleter<T*, Deleter>(raw, std::move(d))
                : nullptr) {}
```

The `release()` helper changes only slightly:

```cpp
void release() {
    if (!ctrl_) return;
    if (--ctrl_->strong == 0) {
        ctrl_->dispose();                         // <-- virtual dispatch
        if (--ctrl_->weak == 0) ctrl_->destroy(); // <-- virtual dispatch
    }
}
```

Note: `dispose()` does **not** know about `ptr_` from `SharedPtr` —
it knows about its own captured `ptr_` member inside the subclass.
That's the type erasure: the deleter and what to delete are captured
once at construction, stored in the control block, and called back
through the vtable. `SharedPtr` becomes "dumb" — it just owns
references to refcounts.

### 8.4 Code skeleton

```cpp
// lessons/ptrs/3/main.cpp

#include <cstdio>
#include <cstdlib>     // for FILE/fopen/fclose if you go that route
#include <utility>

static int g_log_indent = 0;
static void log_indent() { /* same */ }

struct Tracer { /* same */ };

struct _Sp_counted_base {
    long strong = 1;
    long weak   = 1;
    virtual void dispose() noexcept = 0;
    virtual void destroy() noexcept {
        log_indent();
        printf("[_Sp_counted_base] destroy() -> delete this\n");
        delete this;
    }
    virtual ~_Sp_counted_base() = default;
};

template<typename Ptr>
struct _Sp_counted_ptr : _Sp_counted_base {
    Ptr ptr_;
    explicit _Sp_counted_ptr(Ptr p) : ptr_(p) {
        log_indent();
        printf("[_Sp_counted_ptr] ctor with delete-as-deleter\n");
    }
    void dispose() noexcept override {
        log_indent();
        printf("[_Sp_counted_ptr] dispose() -> delete ptr_\n");
        delete ptr_;
    }
};

template<typename Ptr, typename Deleter>
struct _Sp_counted_deleter : _Sp_counted_base {
    Ptr     ptr_;
    Deleter del_;
    _Sp_counted_deleter(Ptr p, Deleter d) : ptr_(p), del_(std::move(d)) {
        log_indent();
        printf("[_Sp_counted_deleter] ctor with custom deleter\n");
    }
    void dispose() noexcept override {
        log_indent();
        printf("[_Sp_counted_deleter] dispose() -> del_(ptr_)\n");
        del_(ptr_);
    }
};

template<typename T> class WeakPtr;
template<typename T>
class SharedPtr {
    private:
        T*                 ptr_  = nullptr;
        _Sp_counted_base*  ctrl_ = nullptr;

        void release() { /* same as lesson 2 but calls virtual dispose/destroy */ }

    public:
        SharedPtr() = default;

        explicit SharedPtr(T* raw)
            : ptr_(raw),
              ctrl_(raw ? new _Sp_counted_ptr<T*>(raw) : nullptr) {}

        template<typename Deleter>
        SharedPtr(T* raw, Deleter d)
            : ptr_(raw),
              ctrl_(raw ? new _Sp_counted_deleter<T*, Deleter>(raw, std::move(d))
                        : nullptr) {}

        // rest: copy/move/dtor, accessors, friends with WeakPtr — same as lesson 2
};

int main() {
    #if defined(BLOCK_0)
        // regression: SharedPtr<Tracer> still works
        // TODO: same as lesson 2 BLOCK_0, but verify the printfs now go through
        //       _Sp_counted_ptr::dispose() -> delete ptr_
    #elif defined(BLOCK_1)
        // SharedPtr<FILE> with fclose deleter
        // TODO:
        //   {
        //     SharedPtr<FILE> f(fopen("/tmp/lesson3.txt", "w"),
        //                       [](FILE* fp){
        //                           printf("[lambda] fclose %p\n", (void*)fp);
        //                           if (fp) fclose(fp);
        //                       });
        //     fputs("hello\n", f.get());
        //   }
        //   // expect: at scope exit, _Sp_counted_deleter::dispose -> lambda -> fclose
    #elif defined(BLOCK_2)
        // SharedPtr<int> with delete[] deleter (managing int[N])
        // TODO:
        //   {
        //     SharedPtr<int> arr(new int[5]{1,2,3,4,5},
        //                        [](int* p){
        //                            printf("[lambda] delete[]\n");
        //                            delete[] p;
        //                        });
        //     for (int i=0; i<5; i++) printf("%d ", arr.get()[i]);
        //     printf("\n");
        //   }
        //   // expect: delete[] called via lambda via _Sp_counted_deleter::dispose
    #endif
}
```

### 8.5 BLOCKs — expected output transcripts

**`BLOCK_0`** — regression, same as lesson 2 BLOCK_0 except dispose
goes through the vtable:

```
[Tracer{1}] parametized ctor
[_Sp_counted_ptr] ctor with delete-as-deleter
[SharedPtr] explicit ctor(T*) ...
... (same lifecycle) ...
[_Sp_counted_ptr] dispose() -> delete ptr_
[Tracer{1}] dtor
[_Sp_counted_base] destroy() -> delete this
```

**`BLOCK_1`** — custom deleter on `FILE*`:

```
[_Sp_counted_deleter] ctor with custom deleter
[SharedPtr] explicit ctor(T*,Deleter)
... fputs writes ...
[SharedPtr] dtor
	[SharedPtr] --strong=0
[_Sp_counted_deleter] dispose() -> del_(ptr_)
[lambda] fclose 0x...
	[SharedPtr] --weak=0
[_Sp_counted_base] destroy() -> delete this
```

Verify: `fclose` runs exactly once, at scope exit, with no manual
intervention. This is the **RAII-over-C-API** idiom. If you ever
worked with `pthread_mutex_t` + `pthread_mutex_init` / `_destroy`,
this is the pattern that replaces it.

**`BLOCK_2`** — `delete[]` via deleter:

```
[_Sp_counted_deleter] ctor with custom deleter
[SharedPtr] explicit ctor(T*,Deleter)
1 2 3 4 5
[SharedPtr] dtor
	[SharedPtr] --strong=0
[_Sp_counted_deleter] dispose() -> del_(ptr_)
[lambda] delete[]
	[SharedPtr] --weak=0
[_Sp_counted_base] destroy() -> delete this
```

(In C++17, `std::shared_ptr<int[]>` does this for you automatically
via the array specialization. We're doing it the hand-rolled way for
pedagogy.)

### 8.6 What this lesson teaches

- Type erasure via virtual base is the **single most important
  pattern** in `std::shared_ptr`'s design. Everything else (custom
  deleters, custom allocators, `make_shared`, `allocate_shared`) is
  just another derived class.
- The control block stores **both the pointer AND the deleter**.
  `SharedPtr` itself stores neither (well, it caches `ptr_` for
  fast deref, but the *owning* copy lives in the control block
  subclass).
- The lambda + `SharedPtr` combination is your new
  `pthread_cleanup_push`. RAII over any C resource.

### 8.7 What's still broken

- Two allocations: control block + managed object. Lesson 4 fuses
  them.
- Not thread-safe. Lesson 5.
- Double-control-block bug still present. Lesson 6.

### 8.8 Map to libstdc++

- `_Sp_counted_base` is literally what we built. See
  `shared_ptr_base.h`, search for `class _Sp_counted_base`.
- `_Sp_counted_ptr<_Ptr>` and `_Sp_counted_deleter<_Ptr,_Deleter,_Alloc,_Lp>`
  exist there too. The real ones take an allocator too (we'll ignore
  that — it's the same pattern with one more captured field).
- `_M_dispose()` and `_M_destroy()` are libstdc++'s names for what
  we called `dispose()` and `destroy()`.

---

## 9. Lesson 4 — `make_shared` single allocation

### 9.1 Where we are

We have a working `SharedPtr<T>` with custom deleters and `weak_ptr`.
Every `SharedPtr<T>(new T(...))` does **two** heap allocations: one
for `T`, one for the control block. Two cache lines, two pointer
chases, two potential `bad_alloc`s.

`make_shared<T>(args...)` solves this with one allocation that
contains:

- A control block (with refcounts and vtable)
- An aligned slot of `sizeof(T)` bytes immediately after it

`T` is constructed into that slot via placement new. When `dispose()`
fires, only the destructor runs (no `delete`). When `destroy()` fires,
the whole combined block is freed in one `::operator delete`.

This also matters because forwarding ctor args lets you write
`make_shared<Tracer>(1, 2, 3)` with **no** temporary `Tracer` — same
trick as `emplace_back`.

### 9.2 Data structure

A new control block subclass:

```
   +------------------------------------+
   | _Sp_counted_ptr_inplace<T>         |
   |   strong = 1                       |
   |   weak   = 1                       |
   |   vtable: dispose, destroy         |  <-- inherited
   |   alignas(T) char storage_[sizeof(T)]
   |       <-- T lives HERE, in place   |
   +------------------------------------+
                ^
                |
                + 1 heap alloc holds all of the above
```

Compared to the two-allocation version, the layout becomes:

```
   BEFORE (lesson 3):                AFTER (lesson 4):
   +-----------+                     +------------------------+
   | ctrl_block|---+                 | ctrl_block + T fused   |
   +-----------+   |                 |                        |
                   v                 | strong, weak, vtable   |
                +---+                |                        |
                | T |                | alignas(T) storage_[] -+--> T constructed
                +---+                +------------------------+    in-place here
   2 allocs                          1 alloc
```

One allocation. Better cache locality. Half the alloc/free traffic.

### 9.3 Operations

```cpp
template<typename T>
struct _Sp_counted_ptr_inplace : _Sp_counted_base {
    alignas(T) unsigned char storage_[sizeof(T)];

    T* get_ptr() noexcept {
        return reinterpret_cast<T*>(&storage_);
    }

    template<typename... Args>
    _Sp_counted_ptr_inplace(Args&&... args) {
        ::new (get_ptr()) T(std::forward<Args>(args)...);
    }

    void dispose() noexcept override {
        get_ptr()->~T();
        // do NOT ::operator delete here — storage_ is part of *this,
        // which is freed in destroy() when weak hits 0.
    }

    void destroy() noexcept override {
        // we were allocated with new _Sp_counted_ptr_inplace<T>(...),
        // so delete this works. (If we'd allocated via custom alloc,
        // we'd dispatch to it here.)
        delete this;
    }
};
```

`SharedPtr<T>` needs a new **private** constructor that takes a
pre-built control block and a `T*` already pointing into the
in-place slot, WITHOUT bumping the refcount (the control block is
already at strong=1 from its own ctor):

```cpp
private:
    struct make_shared_tag {};
    SharedPtr(T* p, _Sp_counted_base* c, make_shared_tag)
        : ptr_(p), ctrl_(c) {
        // strong/weak already initialized to 1 in _Sp_counted_base
        // do NOT increment here
    }
```

`make_shared<T>`:

```cpp
template<typename T, typename... Args>
SharedPtr<T> make_shared(Args&&... args) {
    auto* ctrl = new _Sp_counted_ptr_inplace<T>(std::forward<Args>(args)...);
    return SharedPtr<T>(ctrl->get_ptr(), ctrl,
                        typename SharedPtr<T>::make_shared_tag{});
}
```

### 9.4 Code skeleton

```cpp
// lessons/ptrs/4/main.cpp

// (everything from lesson 3 plus:)

template<typename T>
struct _Sp_counted_ptr_inplace : _Sp_counted_base {
    alignas(T) unsigned char storage_[sizeof(T)];

    T* get_ptr() noexcept { return reinterpret_cast<T*>(&storage_); }

    template<typename... Args>
    _Sp_counted_ptr_inplace(Args&&... args) {
        log_indent();
        printf("[_Sp_counted_ptr_inplace] ctor, in-place T at %p\n",
               (void*)get_ptr());
        ::new (get_ptr()) T(std::forward<Args>(args)...);
    }
    void dispose() noexcept override {
        log_indent();
        printf("[_Sp_counted_ptr_inplace] dispose() -> ~T()\n");
        get_ptr()->~T();
    }
    void destroy() noexcept override {
        log_indent();
        printf("[_Sp_counted_ptr_inplace] destroy() -> delete this (combined block)\n");
        delete this;
    }
};

template<typename T, typename... Args>
SharedPtr<T> make_shared(Args&&... args) {
    log_indent();
    printf("[make_shared] allocating combined block sizeof=%zu\n",
           sizeof(_Sp_counted_ptr_inplace<T>));
    auto* ctrl = new _Sp_counted_ptr_inplace<T>(std::forward<Args>(args)...);
    return SharedPtr<T>(ctrl->get_ptr(), ctrl,
                        typename SharedPtr<T>::make_shared_tag{});
}

int main() {
    #if defined(BLOCK_0)
        // make_shared basic usage
        // TODO:
        //   auto sp = make_shared<Tracer>(42);
        //   // observe: ONE allocation log, ONE Tracer ctor (forwarded), no temp
    #elif defined(BLOCK_1)
        // sizeof comparison
        // TODO:
        //   printf("sizeof(_Sp_counted_ptr<Tracer*>)         = %zu\n", ...);
        //   printf("sizeof(_Sp_counted_ptr_inplace<Tracer>)  = %zu\n", ...);
        //   // expect the inplace one is larger by sizeof(Tracer) (+ alignment)
        //   printf("sizeof(SharedPtr<Tracer>)                = %zu\n", ...);
        //   // expect 16 on x86_64
    #elif defined(BLOCK_2)
        // the make_shared pitfall: WeakPtr pins the COMBINED BLOCK,
        // so a huge Tracer's memory stays allocated until the last weak_ptr dies
        // TODO:
        //   WeakPtr<Tracer> wp;
        //   {
        //     auto sp = make_shared<Tracer>(7);
        //     wp = WeakPtr<Tracer>(sp);
        //   }
        //   // Tracer dtor has run, BUT the combined block is still alive
        //   // (sizeof(Tracer) bytes pinned)
        //   printf("after scope: expired=%d, block still alive\n", wp.expired());
        //   // wp dies -> combined block freed
        //   // Compare to lesson 3 where Tracer's memory was freed at dispose
        //   // and only the small control block was pinned by WeakPtr.
    #endif
}
```

### 9.5 BLOCKs — expected output transcripts

**`BLOCK_0`**:

```
[make_shared] allocating combined block sizeof=N
[_Sp_counted_ptr_inplace] ctor, in-place T at 0x...
[Tracer{42}] parametized ctor              <-- forwarded directly, no temp, no copy
... usage ...
[SharedPtr] dtor
	[SharedPtr] --strong=0
[_Sp_counted_ptr_inplace] dispose() -> ~T()
[Tracer{42}] dtor
	[SharedPtr] --weak=0
[_Sp_counted_ptr_inplace] destroy() -> delete this (combined block)
```

The "no temp" matters: compare to `SharedPtr<Tracer>(new Tracer(42))`
which constructs the temp on the heap as a standalone object. With
`make_shared`, the args are forwarded straight into the in-place slot.

**`BLOCK_1`**:

```
sizeof(_Sp_counted_ptr<Tracer*>)         = 32   (refcounts + vtable + Tracer*)
sizeof(_Sp_counted_ptr_inplace<Tracer>)  = 40   (refcounts + vtable + sizeof(Tracer))
sizeof(SharedPtr<Tracer>)                = 16   (always; just two pointers)
```

Sizes will vary slightly with your compiler / `Tracer` layout. The
ratio is the point: `_Sp_counted_ptr_inplace` is bigger than
`_Sp_counted_ptr` by `sizeof(T)` (plus padding), because it holds T
inline.

**`BLOCK_2`** — the pitfall:

```
[make_shared] allocating combined block sizeof=40
[_Sp_counted_ptr_inplace] ctor, in-place T at 0x...
[Tracer{7}] parametized ctor
[WeakPtr] ctor(SharedPtr) strong=1 weak=2
[SharedPtr] dtor strong=1 weak=2
	[SharedPtr] --strong=0
[_Sp_counted_ptr_inplace] dispose() -> ~T()
[Tracer{7}] dtor
	[SharedPtr] --weak=1
after scope: expired=1, block still alive
[WeakPtr] dtor strong=0 weak=1
	[WeakPtr] --weak=0
[_Sp_counted_ptr_inplace] destroy() -> delete this (combined block)
```

Critical observation: the **40-byte block** stays allocated until the
weak_ptr dies. With the lesson-3 two-allocation version, only the
small control block stays alive; the `Tracer` itself is freed at
`dispose`. So for **huge `T`**, `make_shared` + long-lived `weak_ptr`
keeps `sizeof(T)` bytes pinned that you might not have expected.

This is a known trade-off. The fix is either:

- Don't use `make_shared` if `T` is large and `weak_ptr` is long-lived.
- Or accept the memory cost in exchange for the allocation win.

### 9.6 What this lesson teaches

- `make_shared` is not just a syntactic convenience — it's a real
  layout optimization.
- One allocation, two destruction phases (object destructor, then
  block free), virtual dispatch keeps the caller agnostic.
- The pitfall: `make_shared` + long-lived `weak_ptr` + large `T`
  pins memory unexpectedly.
- Use `alignas(T)` not `std::aligned_storage` (deprecated in C++23).
  Or use a `union { T t; }` trick.

### 9.7 What's still broken

- Not thread-safe. Lesson 5.
- Double-control-block bug still present. Lesson 6.
- No way to alias a sub-object. Lesson 7.

### 9.8 Map to libstdc++

- `_Sp_counted_ptr_inplace<_Tp, _Alloc, _Lp>` is in
  `shared_ptr_base.h`. Same shape, plus the allocator.
- The dispatching constructor on `__shared_ptr` takes a
  `_Sp_make_shared_tag` — same idea as our `make_shared_tag`.
- `std::make_shared<T>(args...)` is in `shared_ptr.h`, lines ~900+.
  It dispatches to `__shared_ptr<T>::__shared_ptr(_Sp_make_shared_tag, ...)`.
- For the allocator variant, see `std::allocate_shared<T>(alloc, args...)`.

---

## 10. Lesson 5 — Atomic refcounts

### 10.1 Where we are

The implementation works in a single thread. With two threads,
everything breaks:

```
   Thread A                Thread B
   --strong (5 -> 4)       --strong (4 -> 3)        ok
   --strong (3 -> 2)       --strong (2 -> 1)        ok
   --strong (1 -> 0)       --strong (1 -> 0)        DOUBLE DISPOSE
   dispose                 dispose
```

The `--strong` and the "if it hit 0" check are not atomic together,
and the decrement itself is racy on `long`. Both threads can read 1,
both decrement to 0 (or one to -1), both dispose. Use-after-free.

Fix: make `strong_` and `weak_` `std::atomic<long>`. Use
**release** on decrement (publish your modifications to other
threads), **acquire** on the winning decrement (synchronize with all
losers' releases before disposing). On increment, **relaxed** is
enough — incrementing while holding a reference can't race with the
zero check because you're keeping the refcount > 0 by definition.

### 10.2 Data structure

```cpp
struct _Sp_counted_base {
    std::atomic<long> strong{1};
    std::atomic<long> weak{1};
    // virtual dispose/destroy/~ctor as before
};
```

Otherwise unchanged. The atomicity is enforced by the type system.

### 10.3 Operations

**Increment (copy ctor):**

```cpp
ctrl_->strong.fetch_add(1, std::memory_order_relaxed);
```

Relaxed is enough because you're not reading any other shared state
on this operation; you just need the count to be visible eventually.
No release/acquire needed.

**Decrement (destructor / release):**

The standard pattern from libstdc++:

```cpp
void release() noexcept {
    if (!ctrl_) return;
    // release on decrement so that any writes we made to *ptr_
    // (before destruction) are visible to the thread that destroys it
    if (ctrl_->strong.fetch_sub(1, std::memory_order_release) == 1) {
        // we were the last; acquire-fence so we see all other threads'
        // pre-release writes before calling dispose
        std::atomic_thread_fence(std::memory_order_acquire);
        ctrl_->dispose();
        // weak decrement also acq_rel pattern (less critical here)
        if (ctrl_->weak.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            ctrl_->destroy();
        }
    }
}
```

Why `release` + `acquire-fence-on-winner` and not just `acq_rel`
everywhere? Both are correct. `acq_rel` on every decrement is
slightly more expensive on weakly ordered architectures (ARM, POWER).
The libstdc++ pattern moves the acquire fence to the rare case
(strong went to 0), which is the hot-path optimization.

On x86-64 the difference is mostly free (x86's TSO model gives you
release semantics on plain stores anyway), but the pattern is the
canonical one for portable code.

**WeakPtr::lock — CAS loop:**

You cannot just `++strong` and check, because `strong` might already
be 0. You need to atomically observe "strong > 0 AND increment":

```cpp
SharedPtr<T> lock() const noexcept {
    if (!ctrl_) return {};
    long s = ctrl_->strong.load(std::memory_order_relaxed);
    while (s != 0) {
        if (ctrl_->strong.compare_exchange_weak(
                s, s + 1,
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            // success: we incremented; safe to construct a SharedPtr
            return SharedPtr<T>(ptr_, ctrl_, adopt_tag{});
            //                                     ^^ but here adopt_tag should NOT
            //                                     bump strong AGAIN — we already
            //                                     did, via CAS. Pick a different tag
            //                                     or pass a "we already bumped" flag.
        }
        // s was overwritten by CAS with the actual current value; loop
    }
    return {};   // strong hit 0 while we were spinning; the object is dead
}
```

A subtlety: in the success path, `SharedPtr`'s "adopt"-style ctor
must **not** increment again. Either use a different tag (e.g.
`already_bumped_tag`) or have the ctor's body just take ownership of
the bump we already did.

### 10.4 Code skeleton

```cpp
// lessons/ptrs/5/main.cpp

#include <atomic>
#include <thread>
#include <vector>
#include <cstdio>
#include <utility>

static int g_log_indent = 0;
static void log_indent() { /* same */ }

struct Tracer { /* same */ };

struct _Sp_counted_base {
    std::atomic<long> strong{1};
    std::atomic<long> weak{1};
    virtual void dispose() noexcept = 0;
    virtual void destroy() noexcept { delete this; }
    virtual ~_Sp_counted_base() = default;
};

// _Sp_counted_ptr / _Sp_counted_deleter / _Sp_counted_ptr_inplace
// as in lessons 3-4, unchanged

template<typename T>
class SharedPtr {
    private:
        T*                ptr_  = nullptr;
        _Sp_counted_base* ctrl_ = nullptr;

        struct adopt_tag {};
        struct already_bumped_tag {};

        // construct from an existing ctrl and bump strong (used by copy)
        // construct from an existing ctrl WITHOUT bumping (used by lock)

        void release() noexcept {
            if (!ctrl_) return;
            if (ctrl_->strong.fetch_sub(1, std::memory_order_release) == 1) {
                std::atomic_thread_fence(std::memory_order_acquire);
                ctrl_->dispose();
                if (ctrl_->weak.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    ctrl_->destroy();
                }
            }
        }

    public:
        SharedPtr() = default;
        explicit SharedPtr(T* raw)
            : ptr_(raw),
              ctrl_(raw ? new _Sp_counted_ptr<T*>(raw) : nullptr) {}

        SharedPtr(const SharedPtr& o) noexcept
            : ptr_(o.ptr_), ctrl_(o.ctrl_) {
            if (ctrl_) ctrl_->strong.fetch_add(1, std::memory_order_relaxed);
        }
        // ... copy assign, move ctor, move assign, dtor — TODO
        // ... accessors — same as before
};

template<typename T>
class WeakPtr {
    private:
        T*                ptr_  = nullptr;
        _Sp_counted_base* ctrl_ = nullptr;
        // ... weak refcount management ...
    public:
        SharedPtr<T> lock() const noexcept {
            if (!ctrl_) return {};
            long s = ctrl_->strong.load(std::memory_order_relaxed);
            while (s != 0) {
                if (ctrl_->strong.compare_exchange_weak(
                        s, s + 1,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed)) {
                    log_indent();
                    printf("[WeakPtr] lock() CAS succeeded at strong=%ld\n",
                           s + 1);
                    return SharedPtr<T>(ptr_, ctrl_,
                                        typename SharedPtr<T>::already_bumped_tag{});
                }
                // s was updated by CAS to the actual current value
            }
            log_indent();
            printf("[WeakPtr] lock() failed, strong hit 0\n");
            return {};
        }
};

int main() {
    #if defined(BLOCK_0)
        // N threads, each holds + drops a copy of the same shared_ptr
        // TODO:
        //   auto sp = make_shared<Tracer>(123);
        //   std::vector<std::thread> ts;
        //   for (int i = 0; i < 8; i++) {
        //       ts.emplace_back([sp]{ /* hold copy */ });
        //   }
        //   for (auto& t : ts) t.join();
        //   // sp also drops at end of scope
        //   // EXPECT: exactly one Tracer dtor, exactly one destroy()
    #elif defined(BLOCK_1)
        // lock() race: one thread destroying, one thread locking
        // TODO:
        //   auto sp = make_shared<Tracer>(7);
        //   WeakPtr<Tracer> wp(sp);
        //   std::thread destroyer([&]{ sp = {}; });          // drops last strong
        //   std::thread locker([&]{
        //       for (int i=0; i<10; i++) {
        //           auto lck = wp.lock();
        //           if (lck) { /* observed alive */ }
        //           else     { /* observed dead   */ }
        //       }
        //   });
        //   destroyer.join();
        //   locker.join();
        //   // EXPECT: depending on timing, some locks succeed, some fail.
        //   //         NO double dispose, NO use-after-free.
    #elif defined(BLOCK_2)
        // memory-order observation: compare acq_rel-on-every-decrement
        // vs release + acquire-fence-on-winner
        // TODO: provide both paths under a #ifdef inside this block, time them
        //       with high_resolution_clock, print delta. On x86 the gap is small;
        //       on ARM/POWER it's measurable.
    #endif
}
```

### 10.5 BLOCKs — expected output transcripts

**`BLOCK_0`** — should be:

```
[make_shared] allocating combined block ...
[Tracer{123}] parametized ctor
... 8 threads spawn, each copy-constructs a SharedPtr, holds, then dies ...
[SharedPtr] copies x 8 (interleaved with strong=N output)
[SharedPtr] dtors x 8 (interleaved, decrementing strong)
[_Sp_counted_ptr_inplace] dispose() -> ~T()
[Tracer{123}] dtor                          <-- exactly ONCE
[_Sp_counted_ptr_inplace] destroy() ...     <-- exactly ONCE
```

The interleaving of "copy" and "dtor" lines varies run-to-run.
Invariant: exactly one `Tracer{123}` dtor across all runs. Run with
ASan + TSan to confirm.

**`BLOCK_1`** — race between destroy and lock:

The output is timing-dependent. Some `wp.lock()` calls return alive
(strong CAS succeeded before destroyer hit 0), others return dead
(CAS observed strong=0 and bailed). The key transcript invariant:

```
[WeakPtr] lock() CAS succeeded at strong=N    <-- some count >= 2
[WeakPtr] lock() failed, strong hit 0         <-- after destroyer wins
[Tracer{7}] dtor                              <-- exactly ONCE
```

No double-free under ASan. No data race under TSan.

**`BLOCK_2`** — timing comparison:

```
1M increment/decrement cycles per scheme:
  acq_rel-every-decrement:        45 ms
  release + acquire-fence-on-1:   38 ms  (~15% faster on x86)
```

Numbers will vary. On x86 the difference is small; on weakly ordered
machines it's larger. The pedagogical point: memory orders are not
all the same cost, and the libstdc++ pattern exists for a reason.

### 10.6 What this lesson teaches

- Refcounts must be `std::atomic`.
- Increment can be `relaxed` (you hold a strong reference, so the
  refcount cannot reach 0 during your increment).
- Decrement must be `release` (publishes your writes to *T before
  destruction); the **last** decrement must additionally `acquire`
  (synchronizes with all other threads' releases) before calling
  `dispose`.
- `WeakPtr::lock()` is a **CAS loop**, never a plain `++strong`.
- This is your map between C++ atomics and the kernel
  `READ_ONCE`/`smp_load_acquire`/`smp_store_release` vocabulary
  you already know. (See `PLAN.md` lesson 11.)

### 10.7 What's still broken

- Double-control-block bug. Lesson 6.
- No aliasing. Lesson 7.

### 10.8 Map to libstdc++

- `_Lock_policy` enum in `shared_ptr_base.h`: `_S_single` (no
  atomics), `_S_mutex` (mutex-protected), `_S_atomic` (atomics —
  what we built).
- `_Sp_counted_base<_S_atomic>::_M_add_ref_lock_nothrow()` is the
  real CAS loop for `lock()`. Read it — it's exactly our pattern.
- `_Sp_counted_base::_M_release()` has the
  release-then-acquire-fence-on-winner trick. Look for
  `__atomic_thread_fence`.

---

## 11. Lesson 6 — `enable_shared_from_this`

### 11.1 Where we are

The double-control-block bug from lesson 1 BLOCK_2 is still there:

```cpp
Tracer* raw = new Tracer(99);
SharedPtr<Tracer> a(raw);
SharedPtr<Tracer> b(raw);   // BUG: second control block
// double-free at scope exit
```

This is fundamental — handing the same raw pointer to two `SharedPtr`
constructors creates two independent refcounts. The standard rule is
"don't do that," and `make_shared` makes it hard to do by accident
(no raw pointer to share).

But there's one case where you NEED to do exactly this: inside a
member function, when you want to hand out a `SharedPtr` to `*this`.
You don't have access to the existing `SharedPtr` that owns you —
you only have `this`. So:

```cpp
struct Widget {
    SharedPtr<Widget> me() { return SharedPtr<Widget>(this); }  // BUG
};
auto w = make_shared<Widget>();
auto w2 = w->me();    // two control blocks, double-free
```

`enable_shared_from_this<T>` fixes this. The trick: T inherits from
it, which adds a `WeakPtr<T> weak_this_` member. The
`SharedPtr` constructor, when called with a `T*`, checks if `T`
derives from `enable_shared_from_this<T>`; if yes, populates
`weak_this_` from `*this`. Now `weak_this_.lock()` gives you a
properly-counted `SharedPtr<T>`.

### 11.2 Data structure

```
   Widget (derives from enable_shared_from_this<Widget>):
   +-------------------------+
   | Widget                  |
   | enable_shared_from_this |
   |   weak_this_  (WeakPtr) |---+
   | ... Widget fields ...   |   |
   +-------------------------+   |
              ^                  |
              |                  |
              | (managed)        |
              |                  |
   +----------+----------+       |
   | control block       |<------+
   | strong, weak,vtable |
   +---------------------+
```

The first time a `SharedPtr<Widget>` is constructed pointing at a
`Widget*`, we populate `weak_this_` with that same `SharedPtr`.
Later, `Widget::shared_from_this()` calls `weak_this_.lock()` to
get a fresh `SharedPtr<Widget>` that shares the **same control block**
as the original.

### 11.3 Operations

```cpp
template<typename T>
class enable_shared_from_this {
    protected:
        enable_shared_from_this() noexcept = default;
        enable_shared_from_this(const enable_shared_from_this&) noexcept = default;
        enable_shared_from_this& operator=(const enable_shared_from_this&) noexcept = default;
        ~enable_shared_from_this() = default;
    public:
        SharedPtr<T> shared_from_this() {
            SharedPtr<T> p = weak_this_.lock();
            if (!p) {
                // std throws bad_weak_ptr here; we can throw or abort
                fprintf(stderr, "[shared_from_this] no controlling SharedPtr exists\n");
                std::abort();
            }
            return p;
        }
    private:
        mutable WeakPtr<T> weak_this_;
        template<typename U> friend class SharedPtr;
};
```

`SharedPtr<T>`'s explicit ctor learns to detect the mixin and wire
`weak_this_`:

```cpp
template<typename T>
static void enable_shared_from_this_with(T* ptr, _Sp_counted_base* ctrl) {
    if constexpr (std::is_convertible_v<T*, enable_shared_from_this<T>*>) {
        if (ptr && ptr->weak_this_.expired()) {
            // populate weak_this_ from a freshly-constructed (no-bump) WeakPtr
            // referencing the just-built control block.
            // careful: this MUST be done after ctrl_ is set on `this`,
            // and the WeakPtr ctor we use here must bump weak (just one,
            // because shared_from_this is itself a WeakPtr).
            ptr->weak_this_._M_assign(ptr, ctrl);
        }
    }
}

explicit SharedPtr(T* raw)
    : ptr_(raw),
      ctrl_(raw ? new _Sp_counted_ptr<T*>(raw) : nullptr) {
    enable_shared_from_this_with(raw, ctrl_);
}
```

`make_shared` does the same wire-up at the end of its body.

The `_M_assign` helper on `WeakPtr` is what populates `weak_this_`
without ever having a full `SharedPtr` to copy from.

### 11.4 Code skeleton

```cpp
// lessons/ptrs/6/main.cpp

#include <type_traits>
#include <cstdlib>
#include <cstdio>

// (everything from lesson 5)

template<typename T>
class enable_shared_from_this {
    protected:
        constexpr enable_shared_from_this() noexcept = default;
        enable_shared_from_this(const enable_shared_from_this&) noexcept = default;
        enable_shared_from_this& operator=(const enable_shared_from_this&) noexcept = default;
        ~enable_shared_from_this() = default;
    public:
        SharedPtr<T> shared_from_this() {
            log_indent();
            printf("[enable_shared_from_this] shared_from_this() lock weak_this_\n");
            SharedPtr<T> p = weak_this_.lock();
            if (!p) {
                fprintf(stderr, "[enable_shared_from_this] BAD: no controlling SharedPtr\n");
                std::abort();
            }
            return p;
        }
    private:
        mutable WeakPtr<T> weak_this_;
        template<typename U> friend class SharedPtr;
};

// SharedPtr<T> grows a wire-up helper used by all ctors that take T*:
template<typename T>
static void _M_enable_shared_from_this_with(T* ptr, _Sp_counted_base* ctrl) noexcept {
    if constexpr (std::is_convertible_v<T*, enable_shared_from_this<T>*>) {
        if (ptr && ptr->weak_this_.expired()) {
            log_indent();
            printf("[SharedPtr] wiring up enable_shared_from_this::weak_this_\n");
            ptr->weak_this_._M_assign(ptr, ctrl);
        }
    }
}

int main() {
    #if defined(BLOCK_0)
        // the bug: without enable_shared_from_this, double control block
        // TODO:
        //   struct Plain { int v = 42; };
        //   {
        //     Plain* raw = new Plain;
        //     SharedPtr<Plain> a(raw);
        //     SharedPtr<Plain> b(raw);   // BUG
        //   }
        //   // expect: double-free under ASan
    #elif defined(BLOCK_1)
        // the fix: derive from enable_shared_from_this
        // TODO:
        //   struct Widget : enable_shared_from_this<Widget> {
        //       int v = 42;
        //       SharedPtr<Widget> me() { return shared_from_this(); }
        //   };
        //   {
        //     auto w = make_shared<Widget>();          // wires up weak_this_
        //     auto w2 = w->me();                        // SAME control block
        //     printf("strong = %ld (expect 2)\n", w.use_count());
        //   }
        //   // expect: exactly one Widget dtor; one combined-block destroy
    #elif defined(BLOCK_2)
        // the trap: shared_from_this() on a stack object aborts
        // TODO:
        //   Widget w_stack;            // no controlling SharedPtr exists
        //   w_stack.me();              // weak_this_ is empty -> lock() returns null
        //                              // -> our enable_shared_from_this aborts
        //   // EXPECT: program aborts with the bad-weak printf
    #endif
}
```

### 11.5 BLOCKs — expected output transcripts

**`BLOCK_0`** — the bug:

```
[_Sp_counted_ptr] ctor ...
[SharedPtr] explicit ctor(T*) strong=1 weak=1
[_Sp_counted_ptr] ctor ...
[SharedPtr] explicit ctor(T*) strong=1 weak=1     <-- SECOND ctrl block
[SharedPtr] dtor ... --strong=0 ... delete ptr_
[SharedPtr] dtor ... --strong=0 ... delete ptr_   <-- SECOND delete of same ptr
=================================================================
==xxxxx==ERROR: AddressSanitizer: heap-use-after-free / double-free
```

**`BLOCK_1`** — the fix:

```
[make_shared] allocating combined block ...
[Widget] (its own ctor)
[SharedPtr] wiring up enable_shared_from_this::weak_this_
... use_count = 1 ...
[enable_shared_from_this] shared_from_this() lock weak_this_
[WeakPtr] lock() CAS succeeded at strong=2
strong = 2 (expect 2)
[SharedPtr] dtor strong=2 ... --strong=1
[SharedPtr] dtor strong=1 ... --strong=0
[_Sp_counted_ptr_inplace] dispose() -> ~Widget()
[Widget] dtor
[_Sp_counted_ptr_inplace] destroy() -> delete this (combined block)
```

Exactly one `Widget` dtor, exactly one block destroy. No double-free.

**`BLOCK_2`** — the trap:

```
[Widget] ctor (on stack)
[enable_shared_from_this] shared_from_this() lock weak_this_
[WeakPtr] lock() failed, strong hit 0           <-- weak_this_ was never wired
[enable_shared_from_this] BAD: no controlling SharedPtr
Aborted (core dumped)
```

The real `std::shared_ptr` throws `std::bad_weak_ptr` here, which
becomes terminate if not caught. We abort because exceptions aren't
worth pulling in for one rare path.

### 11.6 What this lesson teaches

- The "two control blocks for one object" problem is real and
  fundamental.
- `make_shared` is the first line of defense (no raw pointer = no
  way to hand it to two SharedPtrs).
- `enable_shared_from_this` is the second line, for the unavoidable
  case where a member function needs a `SharedPtr` to `*this`.
- The implementation is "store a `weak_ptr` member, populate it
  during `SharedPtr` construction, hand out `lock()`s later."
- `if constexpr` + `std::is_convertible_v` is the C++17 way to
  conditionally execute code based on type traits. No SFINAE
  required. (See `PLAN.md` lesson 9.)

### 11.7 What's still broken

- Aliasing constructor (lesson 7).

### 11.8 Map to libstdc++

- `std::enable_shared_from_this<T>` in `shared_ptr.h`, with the real
  implementation in `__enable_shared_from_this<_Tp, _Lp>` in
  `shared_ptr_base.h`.
- The wire-up helper is `__shared_ptr<>::_M_enable_shared_from_this_with`
  in `shared_ptr_base.h`. The detection uses `__has_esft_base` SFINAE
  (pre-C++17 style) — equivalent to our `if constexpr` +
  `is_convertible_v`.

---

## 12. Lesson 7 — Aliasing constructor

### 12.1 Where we are

Everything works. `SharedPtr<T>` is essentially `std::shared_ptr<T>`
with single-allocation, atomics, custom deleters, and proper
`enable_shared_from_this`. One feature missing: **aliasing**.

The use case:

```cpp
struct Outer {
    Inner inner;
    int v;
};
auto outer = make_shared<Outer>();
// I want a SharedPtr<Inner> pointing at outer->inner,
// but reference-counted against the WHOLE Outer
```

This isn't gymnastics — it's how `dynamic_pointer_cast`,
`static_pointer_cast`, and a handful of internal STL paths work. It's
also genuinely useful: a `SharedPtr<int>` pointing at an element of
a `make_shared`'d `std::array`, keeping the whole array alive.

### 12.2 Data structure

No change to the control block. The shared_ptr's `ptr_` member is the
only thing that differs from the control block's "managed pointer":

```
   shared_ptr<Outer> outer ----+
                                v
                            +------+   +-------------+
                            | ctrl |   | Outer       |
                            |s=2   |---| .inner      |---+
                            |w=1   |   | .v          |   |
                            +------+   +-------------+   |
                                ^                        |
                                |                        |
   shared_ptr<Inner> inner -----+                        |
                                |                        |
                ptr_  ----------------------> &outer->inner
```

`outer.ptr_` points at the `Outer`. `inner.ptr_` points at
`&outer->inner`. Both share the same `ctrl`. The control block still
knows about and disposes the **Outer** (because that's what was
managed-allocated). `inner.ptr_` is just a cache — it points
**into** the managed Outer's storage.

This is why the 2-pointer layout (ptr_ + ctrl_) was the right design
all along, not a 1-pointer wrapper.

### 12.3 Operations

A new constructor:

```cpp
template<typename U>
SharedPtr(const SharedPtr<U>& other, T* alias_ptr) noexcept
    : ptr_(alias_ptr), ctrl_(other.ctrl_) {
    if (ctrl_) ctrl_->strong.fetch_add(1, std::memory_order_relaxed);
}
```

Or the rvalue version (steals instead of bumps):

```cpp
template<typename U>
SharedPtr(SharedPtr<U>&& other, T* alias_ptr) noexcept
    : ptr_(alias_ptr), ctrl_(other.ctrl_) {
    other.ptr_ = nullptr;
    other.ctrl_ = nullptr;
}
```

Note: `U` and `T` are unrelated template types. The alias pointer
type determines `T`; the control block was originally for some
`U`-derived managed object. The control block doesn't care — it
only knows how to call `dispose` and `destroy`, and those work on
the original `U`, not on `T`.

### 12.4 Code skeleton

```cpp
// lessons/ptrs/7/main.cpp

// (everything from lesson 6)

template<typename T>
class SharedPtr {
    // ... all prior content ...

    public:
        // aliasing ctor (lvalue)
        template<typename U>
        SharedPtr(const SharedPtr<U>& other, T* alias_ptr) noexcept
            : ptr_(alias_ptr), ctrl_(other.ctrl_) {
            log_indent();
            printf("[SharedPtr] aliasing ctor (lvalue): ptr_=%p, "
                   "shares ctrl with other\n", (void*)ptr_);
            if (ctrl_) ctrl_->strong.fetch_add(1, std::memory_order_relaxed);
        }

        // aliasing ctor (rvalue)
        template<typename U>
        SharedPtr(SharedPtr<U>&& other, T* alias_ptr) noexcept
            : ptr_(alias_ptr), ctrl_(other.ctrl_) {
            log_indent();
            printf("[SharedPtr] aliasing ctor (rvalue): ptr_=%p, "
                   "stealing ctrl from other\n", (void*)ptr_);
            other.ptr_ = nullptr;
            other.ctrl_ = nullptr;
        }
};

int main() {
    #if defined(BLOCK_0)
        // basic aliasing: SharedPtr<Inner> pinning Outer
        // TODO:
        //   struct Inner { Tracer t{1}; };
        //   struct Outer { Inner inner; Tracer t{2}; };
        //   SharedPtr<Inner> inner_sp;
        //   {
        //     auto outer = make_shared<Outer>();
        //     inner_sp = SharedPtr<Inner>(outer, &outer->inner);
        //     printf("after alias: strong = %ld (expect 2)\n", outer.use_count());
        //     printf("inner_sp.get() = %p, &outer->inner = %p\n",
        //            (void*)inner_sp.get(), (void*)&outer->inner);
        //   }
        //   // outer dies, but Outer is NOT destroyed yet — inner_sp pins it
        //   printf("after outer scope: inner_sp.get() = %p\n", (void*)inner_sp.get());
        //   // inner_sp dies at end of main, NOW the Outer dies (Tracer 1 then Tracer 2)
    #elif defined(BLOCK_1)
        // aliasing into an array element
        // TODO:
        //   auto arr = make_shared<std::array<Tracer, 3>>();  // 3 Tracers in one alloc
        //   SharedPtr<Tracer> middle(arr, &(*arr)[1]);
        //   // arr drops; middle keeps all 3 Tracers alive
    #elif defined(BLOCK_2)
        // dangling alias: a UB demo
        // TODO:
        //   auto outer = make_shared<Outer>();
        //   int local = 99;
        //   SharedPtr<int> dangling(outer, &local);   // alias to stack memory
        //   // dangling.get() points at &local, which is fine WHILE local is alive.
        //   // If we leak dangling past local's scope, deref-ing it is UB.
        //   // This is a deliberate footgun; document it loudly in the printf.
    #endif
}
```

### 12.5 BLOCKs — expected output transcripts

**`BLOCK_0`**:

```
[make_shared] allocating combined block ...
[Tracer{1}] parametized ctor       <-- Inner's Tracer
[Tracer{2}] parametized ctor       <-- Outer's Tracer
[SharedPtr] aliasing ctor (lvalue): ptr_=0x... shares ctrl with other
after alias: strong = 2 (expect 2)
inner_sp.get() = 0x... &outer->inner = 0x...  (SAME address)
[SharedPtr] dtor strong=2 weak=1
	[SharedPtr] --strong=1                  <-- outer dies, but Outer NOT destroyed
after outer scope: inner_sp.get() = 0x...     <-- inner_sp still valid
[SharedPtr] dtor strong=1 weak=1              <-- inner_sp dies at main end
	[SharedPtr] --strong=0
[_Sp_counted_ptr_inplace] dispose() -> ~Outer()
[Tracer{2}] dtor                              <-- Outer's Tracer
[Tracer{1}] dtor                              <-- Inner's Tracer (member dtor order)
[_Sp_counted_ptr_inplace] destroy() ...
```

Verify: when `outer` goes out of scope, the Outer is **not**
destroyed. Only when `inner_sp` (the alias) is destroyed does the
Outer (and via member dtor chain, the Inner) finally die.

**`BLOCK_1`** — array element alias:

```
[make_shared] combined block for std::array<Tracer, 3>
[Tracer{0}], [Tracer{1}], [Tracer{2}] ctors
[SharedPtr] aliasing ctor middle = &arr[1]
arr drops, middle pins array
... at scope end ...
[_Sp_counted_ptr_inplace] dispose() -> ~array()
[Tracer{2}] dtor, [Tracer{1}] dtor, [Tracer{0}] dtor   (reverse order)
[_Sp_counted_ptr_inplace] destroy() ...
```

The whole array stays alive until `middle` dies. Aliasing keeps the
managed object alive, not just the aliased sub-element.

**`BLOCK_2`** — UB demo:

```
[SharedPtr] aliasing ctor (lvalue): ptr_=&local (stack address!)
*dangling = 99 (works while local is alive)
... local goes out of scope here ...
*dangling = ??? (UB; may be garbage, may be the old value, may segfault)
```

The point: the aliasing ctor lets you build pointers that
**aren't** kept alive by the refcount. The refcount keeps the managed
object alive, but if the alias points OUTSIDE that object, the
refcount can't help.

### 12.6 What this lesson teaches

- `SharedPtr` is two pointers because **the stored pointer and the
  managed pointer can be different**. The aliasing constructor is
  what makes that distinction useful.
- Aliasing is what makes `dynamic_pointer_cast` work: it shares the
  control block of a `SharedPtr<Base>` while presenting a
  `SharedPtr<Derived>` with the down-cast pointer.
- Aliasing into a `make_shared`'d composite (struct, array) is the
  canonical "shared view of a sub-object" idiom.
- Aliasing to memory **outside** the managed object is a footgun.
  Don't.

### 12.7 What's still broken

Honestly: not much. At this point you've recreated the substantive
core of `std::shared_ptr`. The remaining gap with libstdc++ is
discussed in the next section.

### 12.8 Map to libstdc++

- The aliasing ctor in libstdc++ is `__shared_ptr(const __shared_ptr<_Yp, _Lp>&, element_type*) noexcept`.
- Used by `std::dynamic_pointer_cast`, `std::static_pointer_cast`,
  `std::const_pointer_cast`, `std::reinterpret_pointer_cast`.
- For `dynamic_pointer_cast<U>(sp)`, the implementation is roughly:
  ```cpp
  U* p = dynamic_cast<U*>(sp.get());
  return p ? shared_ptr<U>(sp, p) : shared_ptr<U>();
  ```
  Aliasing ctor does the refcount work; `dynamic_cast` does the type
  work.

---

# Layer C — Bridge to reality

## 13. What we did NOT build (and why each is a small extension)

The seven lessons above cover the substantive design of
`std::shared_ptr`. Here's the gap-list — features in real libstdc++
that we omitted, with one paragraph each on what they are.

### 13.1 `allocate_shared<T, Alloc>(alloc, args...)`

Same idea as `make_shared` but uses a custom allocator for the
combined block. Add another control-block subclass
`_Sp_counted_ptr_inplace_alloc<T, Alloc>` that captures the allocator
by value, uses it in `destroy()` to free `this`. Used by `pmr`
(lesson 14 in `PLAN.md`).

### 13.2 `shared_ptr<T[]>`

Array specialization. `T` is a complete type, but the deleter is
`delete[]` and `operator[]` is provided instead of `operator->`.
Single new partial specialization of `SharedPtr` with array operator,
plus a `_Sp_counted_ptr<T*>` whose `dispose()` calls `delete[]`. C++17
added it; before that, you used `shared_ptr<T>(new T[n], [](T* p){
delete[] p; })`.

### 13.3 `weak_type`

`std::shared_ptr<T>::weak_type` is an alias for `std::weak_ptr<T>`.
Convenience for generic code: given a `SharedPtr` type without
knowing `T`, you can name its weak counterpart. Add `using weak_type
= WeakPtr<T>;` to `SharedPtr` and you've got it.

### 13.4 `owner_before` / owner-based ordering

`std::shared_ptr` orders by **control block address**, not managed
pointer address. So two `SharedPtr`s aliasing into the same managed
object compare equal under `owner_before`, even if their `ptr_`
members differ. Provided so you can use `SharedPtr`s as keys in a
`std::map` keyed by "are these the same shared resource?". Add
`bool owner_before(const SharedPtr&) const { return ctrl_ < other.ctrl_; }`.

### 13.5 `reset(p, d, alloc)` overloads

`sp.reset()` makes `sp` empty. `sp.reset(p)` is equivalent to `sp =
SharedPtr<T>(p)`. `sp.reset(p, d)` ditto with a deleter; `sp.reset(p,
d, a)` with allocator. Mechanically straightforward; pile on overloads.

### 13.6 `__shared_ptr_access` (the `->`/`*` CRTP helper)

libstdc++ uses a CRTP helper class to provide `operator*` and
`operator->`, specialized differently for `T`, `T[]`, and
`T[N]` (the array case has no `*` and no `->`, but has `[]`). We
inlined the two operators directly into `SharedPtr<T>`. The CRTP
version is purely a code-deduplication scheme.

### 13.7 `_Lock_policy` template parameter

We hard-coded atomics (`_S_atomic`) in lesson 5. libstdc++'s
`__shared_ptr<T, _Lp>` template takes a `_Lock_policy` so you can
opt out: `_S_single` (no synchronization, single-threaded only)
and `_S_mutex` (mutex-protected, for ancient architectures without
atomics). Conditional specialization of `_Sp_counted_base<_Lp>` with
the right primitives. The public `std::shared_ptr` always uses
`_S_atomic`; the policy is an internal knob.

### 13.8 `std::atomic_shared_ptr<T>` (C++20)

A `shared_ptr` whose **pointer** (the two-pointer struct itself, not
just its refcounts) can be atomically replaced and observed. Useful
for lock-free data structures. Distinct from our lesson 5, which made
the refcounts atomic but left the `{ptr_, ctrl_}` struct itself
non-atomic. Out of scope for this series.

### 13.9 `std::out_ptr` / `std::inout_ptr` (C++23)

Adapters for passing `unique_ptr` / `shared_ptr` to C APIs that take
`T**` to fill in. Pure interop sugar. Doesn't change the underlying
data structure.

---

## 14. For the next Claude session

You are picking up a 7-lesson series teaching the user (a C
programmer with kernel/DBMS/CUDA background) `std::shared_ptr` by
rebuilding it.

### 14.1 What to do first

1. **Confirm pre-flight.** Check `lessons/ptrs/1/` for the three
   libstdc++ reference files (`shared_ptr.hpp`,
   `shared_ptr_base.hpp`, `shared_ptr_atomic.hpp`). If any is missing
   or starts with `<!DOCTYPE html>`, re-fetch from
   `raw.githubusercontent.com/gcc-mirror/gcc/refs/heads/master/libstdc%2B%2B-v3/include/bits/`
   (NOT `github.com/.../blob/...` which serves HTML).

2. **Ask the user which lesson they're on.** Lessons are
   `lessons/ptrs/1/` through `lessons/ptrs/7/`. If a lesson dir is
   empty or missing, they haven't started it yet.

3. **Open this README to the relevant section.** Use it as the spec.
   The skeletons inside are not literal code to dump — they are
   structured prompts the user types through. The `// TODO:` markers
   are where they think.

### 14.2 House-style invariants (do not deviate)

- **One `main.cpp` per lesson directory.** Multiple `BLOCK_N`
  sections gated by `#if defined(BLOCK_N)`. Build one at a time.
- **`build.sh` parses `--flags X,Y,Z`** and emits `-DX -DY -DZ`.
  Copy verbatim from `lessons/dynamic_array/2/build.sh`.
- **C++17 target.** `-std=c++17`. See `lessons/Makefile` for the
  full warning/sanitizer flag set.
- **`Tracer` struct.** Per-special-member printfs. Modeled on
  `Vertex` in `lessons/dynamic_array/2/main.cpp`. Lets the user
  see ctor/dtor interleaving with refcount changes.
- **`g_log_indent` + `log_indent()`.** Depth-aware printf. Methods
  bump on entry, drop on exit. Nested operations indent.
- **`SharedPtr` not `shared_ptr`.** Capital S, capital P. So we
  don't shadow `std::shared_ptr` in the same TU and so it reads as
  "our thing, not stdlib."
- **Same for `WeakPtr`, `MakeShared` (or `make_shared` in lesson 4 —
  the function form is fine lowercase, but adjust if the user
  prefers).**

### 14.3 What to do when the user is stuck

- If the user is debugging weird output: walk the **five invariants**
  in section 4. Whichever one is broken is the bug.
- If the user asks "but how does std::shared_ptr do this?": consult
  `shared_ptr_base.hpp` and quote the line. The "Map to libstdc++"
  subsection at the end of each lesson points you at the right
  classes.
- If the user asks about a feature this README doesn't cover (e.g.
  `pmr`, `atomic_shared_ptr`): check section 13 first. If still not
  there, it's probably out of scope; redirect to `PLAN.md` to find
  the right lesson.

### 14.4 Cross-lesson dependencies

Lesson N starts from lesson N-1's code. The user should `cp -r ../N-1/* .`
(minus the reference `.hpp` files which live only in `1/`) and evolve
from there. Each lesson's section starts with "Where we are" — that
is the entry condition.

### 14.5 The user's preferences (from `PLAN.md` and observed style)

- Terse comments. Why, not what. The dynamic_array code has tight
  one-line comments explaining the **reason** for a line, not its
  effect.
- ASCII diagrams in comments are welcome when geometry matters.
- Sanitizers are always on. ASan/UBSan output IS the spec; lessons
  deliberately trigger them in some BLOCKs.
- No futures, no exceptions outside of where the standard requires
  them (e.g. `bad_weak_ptr` — we `abort()` instead).
- The user wants to **type through** the implementation, not
  copy-paste. Don't hand them finished `main.cpp`s. Coach.

---

## Appendix A — Quick reference card

```
SharedPtr<T>:
  {T* ptr_, _Sp_counted_base* ctrl_}     16 bytes
  copy:    ++ctrl_->strong
  dtor:    if (--ctrl_->strong == 0) {
               ctrl_->dispose();
               if (--ctrl_->weak == 0) ctrl_->destroy();
           }

WeakPtr<T>:
  {T* ptr_, _Sp_counted_base* ctrl_}     16 bytes
  copy:    ++ctrl_->weak
  dtor:    if (--ctrl_->weak == 0) ctrl_->destroy();
  lock():  CAS loop to bump strong iff strong > 0

_Sp_counted_base:                         (abstract)
  atomic<long> strong = 1
  atomic<long> weak   = 1
  virtual dispose() = 0       <- destroy the OBJECT
  virtual destroy()           <- destroy THIS control block (default: delete this)

Concrete subclasses:
  _Sp_counted_ptr<Ptr>                    -> dispose: delete ptr_
  _Sp_counted_deleter<Ptr, Deleter>       -> dispose: del_(ptr_)
  _Sp_counted_ptr_inplace<T>              -> dispose: get_ptr()->~T()
                                             destroy: ::operator delete(this)

make_shared<T>(args...):
  new _Sp_counted_ptr_inplace<T>(forward<Args>(args)...)
  -> one allocation, in-place T, no temp

enable_shared_from_this<T>:
  mixin holding mutable WeakPtr<T> weak_this_
  SharedPtr ctors detect via is_convertible_v and populate

aliasing ctor:
  SharedPtr(const SharedPtr<U>& other, T* alias)
      : ptr_(alias), ctrl_(other.ctrl_)
  -> shares refcount with other, but ptr_ may point ANYWHERE
```

## Appendix B — Build cheatsheet

```bash
cd lessons/ptrs/N
./build.sh --flags BLOCK_0
./main
./build.sh --flags BLOCK_1
./main
./build.sh --flags BLOCK_2
./main
```

Or via the lessons-level Makefile (works for top-level numbered
lessons, but not nested dirs like `ptrs/N/` — use `build.sh` for
these).
