## Why C++ on Jetson at all (quick)

Jetson is a strange beast: it's an "embedded" SoC but it has gigabytes of RAM, a full Ubuntu, and a GPU. Three reasons C++ dominates there:

1. **NVIDIA's whole stack is C++**: CUDA, TensorRT, cuDNN, cuBLAS, Thrust, CUB. The drivers expose C handles (`cudaStream_t`, `cudaEvent_t`) that you wrap in C++ RAII.
2. **Zero-cost abstractions**: a templated `fir_filter<float, 64>` compiles down to the same loop you'd write in C, but is reusable for `double`, `half`, `int16_t` without runtime polymorphism. Critical for DSP kernels.
3. **Determinism is achievable in C++**: with `-fno-exceptions`, `-fno-rtti`, `noexcept` move ctors, `constexpr` compile-time tables, and `alignas(64)` for cache lines, you get real-time-friendly code without giving up generic algorithms. C can't express the generic part.

The "embedded but not microcontroller" sweet spot (Jetson, Snapdragon Auto, NXP i.MX 8) is **exactly** where modern C++17/20/23 lives.

---

## Where you are vs the Day 4 topic list

Recap of the original list, with status:

| Topic | Status |
|---|---|
| Classes, copy/move semantics | Lesson 3 — done, kinda understood |
| Default ctor/dtor + memory layout | Lesson 4 — in progress |
| `std::move` vs `std::forward`, perfect forwarding | not done |
| RAII with custom deleters (CUDA handles!) | not done — **high value for Jetson** |
| Templates → concepts (C++20), SFINAE → requires | not done |
| `std::span`, `std::mdspan`, `std::ranges` | not done — **high value for radar tensors** |
| `std::optional`, `std::variant`, `std::expected` | not done |
| `-fno-exceptions` real-time philosophy | not done |
| `-fno-rtti` same | not done |
| `alignas(64)`, false sharing, cache lines | not done — **high value for DSP** |
| `constexpr`, `consteval` | not done |
| "No Boost" philosophy | not done |
| Live mock interview drill | final step |

The 4 things I'd star for Jetson + radar specifically: **RAII custom deleters**, **`std::span`/`mdspan`**, **`alignas` + false sharing**, **the no-exceptions/no-RTTI cost model**.

---

## Three candidate paths — pick one

### Path A — "Finish the Day 4 list as-is" (12 more lessons, ~2 days)

One file per topic, each ~150 lines + walkthrough README. Best if you have time and want to genuinely be able to answer any C++ question cold.

### Path B — "Interview-critical only" (5 more lessons, ~1 day) (Recommended)

Skips the academic stuff, focuses on what an interviewer for a Jetson DSP job will actually probe:

- **Lesson 5** — References & forwarding: closes out the `T&&` confusion, covers `std::forward`, perfect forwarding. (Foundation for everything else.)
- **Lesson 6** — RAII + custom deleters with a fake `cudaStream_t` analog. `unique_ptr<T, Deleter>` pattern. This is the question they will absolutely ask.
- **Lesson 7** — `std::span`, `std::array`, `std::mdspan` for radar buffer views. Zero-copy views over GPU pinned memory.
- **Lesson 8** — `alignas(64)`, false sharing demo (two threads on same cache line → measure slowdown). DSP/real-time bread and butter.
- **Lesson 9** — The "no-exceptions / no-RTTI" model: `std::optional`, `std::expected`, error-code returns. What it costs you, what it buys you.
- **Lesson 10** — Mock interview drill, cold answers, pushback.

### Path C — "Just get to the live drill" (skip to the mock)

Go straight to lesson 10. You answer cold, I push back, we identify the holes, then we backfill only those topics. Faster but riskier — you may discover the holes mid-interview instead of mid-drill.

