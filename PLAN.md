# C++ for the C programmer — 20 lessons

> This directory doubles as my CV / job-search workspace. The C++ curriculum
> lives under `./lessons/`; the PDFs, cover letters, and research notes at the
> root are unrelated. Read this file for the lessons; ignore the rest.

## Why this exists

I'm a strong C programmer (kernel/driver work, a custom DBMS, CUDA analytics).
The gap is **C++ the language** — idioms, the type system, ownership, templates,
the bits a C programmer either reinvents badly or avoids entirely.

The curriculum below targets that gap. It does not teach programming, it does
not teach algorithms. It teaches you the parts of modern C++ that bite a
disciplined C programmer, in roughly the order they bite.

## Defaults

- **Standard:** C++17. No concepts, no `<ranges>`, no `std::expected`, no
  `std::jthread`. Where C++20 or C++23 would help, the NOTES.md says so.
- **Compiler:** g++ with `-std=c++17 -Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wold-style-cast -Wnon-virtual-dtor -g -O2`, plus
  `-fsanitize=address,undefined -fno-omit-frame-pointer`.
- **Freestanding variant:** every lesson can also be built with
  `-fno-exceptions -fno-rtti` via `make N-rt`. Lesson 18 is the dedicated
  embedded/freestanding treatment; the flag set in earlier lessons is there
  so you can see what breaks.
- **Lesson style:** each lesson is a complete, runnable worked example.
  `main.cpp` (and any extra TUs) is what you read first. `NOTES.md` then
  asks you to modify the code in 2–4 ways and re-run.

## Layout

```
lessons/
  Makefile           one-shot build for every lesson
  common/            shared utilities (only when justified)
  1/  main.cpp util.h util.cpp NOTES.md
  2/  main.cpp NOTES.md
  ...
  20/ main.cpp NOTES.md
```

A lesson with multiple `.cpp` files (like lesson 1) is intentional — that's
the whole point of the topic.

## Curriculum

### Phase A — "C with classes" detox

| # | Topic | Why it bites C programmers |
|---|---|---|
| 1 | TU, linkage, ODR, headers, `inline`, `extern "C"` | `static` means three different things; const has different default linkage; templates can't live in `.cpp` files |
| 2 | References, `const`, `constexpr` | References aren't pointers; reading const declarations right-to-left; what `constexpr` actually guarantees |
| 3 | RAII and the rule of 0/3/5 | `goto cleanup;` becomes destructors; move semantics replace ownership transfer-by-comment |
| 4 | Value categories: lvalue, xvalue, prvalue; move, copy elision, RVO/NRVO | `std::move` is a cast; `return std::move(x)` is an anti-pattern |
| 5 | Namespaces, ADL, two-phase lookup, hidden friends | Why your `std::swap` override doesn't get called; the `using std::swap;` idiom |

### Phase B — Type system + STL

| # | Topic | Why |
|---|---|---|
| 6 | `std::vector`, `std::array`, and hand-rolling `span<T>` | Iterator invalidation, growth strategy, why `(T*, size_t)` is bad; `std::span` is C++20 so we build it |
| 7 | `std::string`, `std::string_view`, SSO | When `string_view` dangles; SSO sizes; why `const char*` parameters age badly |
| 8 | `unique_ptr`, `shared_ptr`, `weak_ptr` | Custom deleters as the RAII-over-C-API pattern; why `shared_ptr` is rarely the right answer in hot paths |
| 9 | Templates: function/class, `enable_if`, `void_t`, `if constexpr`, tag dispatch, CRTP teaser | Where C macros went; how to compile-time dispatch; reading template error messages |
| 10 | STL algorithms + lambdas | `<algorithm>`, `<numeric>`, projections via composition; honest sidebar on what C++20 ranges fix |

### Phase C — Memory, concurrency, the model

| # | Topic | Why |
|---|---|---|
| 11 | C++ memory model, `std::atomic`, memory orders | Direct map onto your kernel `READ_ONCE` / `smp_*` / acquire-release instincts |
| 12 | `std::thread`, `mutex`, `condition_variable`, `shared_mutex`, `async`, `future` | Worked example: a `joining_thread` RAII wrapper (the thing C++20 `jthread` is) |
| 13 | Lock-free SPSC ring | The bridge from kernel page-pool thinking to userspace C++; cache-line padding, atomics, benchmark vs mutex |
| 14 | Allocators, arenas, `std::pmr` | `monotonic_buffer_resource`, `polymorphic_allocator`; the "no malloc in the steady state" pattern radar/HFT shops live by |
| 15 | Errors: exceptions vs error codes vs `std::optional`/`std::variant` | Hand-roll `Expected<T,E>` so `std::expected` (C++23) lands as obvious; `noexcept` discipline; when to `-fno-exceptions` |

### Phase D — Performance + advanced + capstone

| # | Topic | Why |
|---|---|---|
| 16 | CRTP, type erasure, `std::function` overhead, sketch `inplace_function` | Static polymorphism for hot paths; what `std::function` costs and when not to pay it |
| 17 | `alignas`, `alignof`, false sharing, `std::hardware_destructive_interference_size` | The C++ vocabulary for `____cacheline_aligned` and friends |
| 18 | Freestanding / embedded subset | `-fno-exceptions -fno-rtti -ffreestanding`; no dynamic alloc; ISR-safe; the constraints the embedded trajectory imposes |
| 19 | PIMPL, ABI boundary, virtual interfaces, dynamic vs static polymorphism | The vocabulary for stable library boundaries and plug-in points |
| 20 | **Capstone** — typed in-memory store with thread-safe ingest, snapshot reads, pluggable allocator | Move-only types, templates, atomics + locks, RAII, PIMPL, `pmr`. The "you've made it" exercise. |

Lessons 5–20 ship as `NOTES.md` stubs (concept, traps, exercises). You fill in
`main.cpp` as you reach each one, or I write it when you get there — your call.

## How to build and run

From the repo root:

```bash
cd lessons
make help          # see available targets
make 1             # build lesson 1 -> lessons/1/main
./1/main           # run it
make run-1         # build and run in one shot
make 1-rt          # same lesson, built with -fno-exceptions -fno-rtti
make clean         # rm every built binary
make all           # build every lesson that has source
```

Generate `compile_commands.json` for your editor (requires `bear`):

```bash
make compile_commands.json
```

## Prerequisites

- `g++` (any version from gcc 9 onwards — C++17 is well-supported).
- `make`.
- Optional: `bear` for `compile_commands.json`, `clang-format` and
  `clang-tidy` if you want to lint locally.

## How to use the lessons

1. Read `lessons/N/NOTES.md`. Concept → C contrast → Traps.
2. Read `lessons/N/main.cpp`. Run it.
3. Do the exercises in `NOTES.md`. Each one asks you to modify `main.cpp`
   and explain the new behaviour.
4. Move on. Don't skip the traps — they're where C-trained intuition fails.

## A note on style

The code in these lessons is deliberately not always "best practice." Some
lessons show you the bad pattern *first*, because the C reflex points there.
The NOTES.md flags it. Don't lift lesson code verbatim into production
without re-reading the NOTES.

## C-to-C++ mental-model intro

Six things that will save you weeks if you internalise them now:

1. **The destructor is the new `goto cleanup;`.** Stop writing manual
   cleanup. Wrap every C resource in a class whose destructor releases it.
   This is *the* idiom — RAII. See lesson 3.

2. **References are not pointers.** They cannot be null, cannot be rebound,
   and "taking a reference" doesn't cost anything compared to a pointer.
   The reason they exist is to let copy/move constructors and operator
   overloads have sensible signatures. See lesson 2.

3. **Values, not pointers, are the default.** A function that returns a big
   thing returns it by value. The compiler elides the copy (lesson 4). You
   stop reaching for output parameters.

4. **Templates are not generics.** A template is a compile-time recipe. The
   compiler instantiates it per type used. Errors are bad until you put
   concepts (C++20) or `static_assert`s on them; lesson 9 shows the C++17
   tools to make errors livable.

5. **The STL container/algorithm split is the C++ vocabulary.** Once you
   read `std::find_if(v.begin(), v.end(), pred)` as fluently as `for (...)`,
   you've crossed over. Lesson 10.

6. **`const` is a contract, not an optimization hint.** Functions that
   don't mutate take `const T&`. Members that don't mutate are `const`.
   The type system enforces this, not the compiler's optimiser. Lesson 2.

Good luck.

