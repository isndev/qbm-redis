/**
 * @file unit/parser/resp-aggregate-reserve.cpp
 * @brief A declared aggregate count must not let a peer pre-commit memory it never sends.
 *
 * `ParserConfig` bounds a single aggregate to `max_array_size` (1e6 elements) and the nesting to
 * `max_nesting_depth` (64), and its own comment says these "guard against hostile or corrupt input
 * that would otherwise cause unbounded allocation while decoding a single frame". But nothing
 * bounded the PRODUCT: every aggregate parser reserved its full DECLARED count up front, so a
 * frame of 64 nested `*1000000\r\n` headers — about 768 bytes, and rejected at the depth limit —
 * walked down committing an element vector per level on the way.
 *
 * The declared count is a claim, not delivered data. `MAX_AGGREGATE_RESERVE` now caps what the
 * claim may reserve; the vectors still grow as elements actually parse, so the commitment tracks
 * received bytes. The `max_array_size` validity check is untouched.
 *
 * The oracle is a replaced global `operator new` counting bytes during the parse. It is verified
 * live before use (an elided probe allocation would make the budget assertion pass vacuously —
 * that exact trap was hit while writing the equivalent HTTP/1.1 test), and paired with a
 * behavioural check that ordinary aggregates still decode correctly. The replacement is compiled
 * out under ThreadSanitizer, where it is an ODR collision with the TSan runtime that broke the
 * whole lane's link on Linux — see QB_TEST_TSAN_ACTIVE below; the behavioural check still runs.
 *
 * qb - C++ Actor Framework
 * Copyright (C) 2011-2026 isndev. All rights reserved.
 * Licensed under the Apache License, Version 2.0.
 */

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>

#ifdef _MSC_VER
#include <malloc.h> // _aligned_malloc / _aligned_free (see the aligned pair below)
#endif

#include <gtest/gtest.h>

#include <qbm/redis/parser/parser.h>

using namespace qb::redis::parser;

namespace {
std::atomic<bool>        g_counting{false};
std::atomic<std::size_t> g_bytes{0};

struct AllocScope {
    AllocScope() {
        g_bytes.store(0, std::memory_order_relaxed);
        g_counting.store(true, std::memory_order_relaxed);
    }
    ~AllocScope() {
        g_counting.store(false, std::memory_order_relaxed);
    }
    [[nodiscard]] static std::size_t
    bytes() {
        return g_bytes.load(std::memory_order_relaxed);
    }
};
} // namespace

// ---------------------------------------------------------------------------------------------
// ThreadSanitizer: the allocator replacement below must NOT be compiled.
//
// `libclang_rt.tsan_cxx.a(tsan_new_delete.cpp.o)` defines every global `operator new` /
// `operator delete` form as a STRONG symbol (ASan's equivalents are weak, which is why the
// `sanitize` preset links, and there the replacement even wins so the counter stays live).
// Defining them here too is a plain ODR collision the static linker rejects:
//
//   ld: multiple definition of `operator new(unsigned long)';
//       libclang_rt.tsan_cxx-aarch64.a(tsan_new_delete.cpp.o): first defined here
//
// That is not local to this test: ninja stops the build, and every qbm-redis test target that
// had not linked yet reports `(Not Run)` — 38 of them, measured on the first `sanitize-thread`
// run this repository ever did on Linux. (macOS links because the Apple TSan runtime interposes
// rather than defining these strongly, so the collision is Linux-only and invisible here. The
// identical replacement in qbm/http's content-length-preallocation.cpp carries the same guard —
// it was the one that was reported; this file is its sibling, found by running the lane.)
//
// Compiling it out costs the TSan lane nothing it could ever have had: with `tsan_cxx` owning
// allocation there is no way for a replaced `operator new` to observe anything, so
// `allocation_counter_is_live()` returns false and the byte-budget cases self-skip exactly as
// they already do on any toolchain that intercepts first. Keeping the test in the lane keeps the
// RESP parser under TSan's actual instrumentation, which excluding the binary would forfeit.
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define QB_TEST_TSAN_ACTIVE 1
#endif
#endif
#if defined(__SANITIZE_THREAD__) // GCC spelling
#define QB_TEST_TSAN_ACTIVE 1
#endif
#ifndef QB_TEST_TSAN_ACTIVE
#define QB_TEST_TSAN_ACTIVE 0
#endif

#if !QB_TEST_TSAN_ACTIVE
namespace {
inline void *
tracked_alloc_nothrow(std::size_t n) noexcept {
    if (g_counting.load(std::memory_order_relaxed))
        g_bytes.fetch_add(n, std::memory_order_relaxed);
    return std::malloc(n ? n : 1);
}

inline void *
tracked_alloc(std::size_t n) {
    void *p = tracked_alloc_nothrow(n);
    if (!p)
        throw std::bad_alloc();
    return p;
}

// Aligned storage must be released by the SAME family as it was allocated from, and there is no
// one portable pair. MSVC's UCRT deliberately ships no C11 `std::aligned_alloc`: on Windows
// `free()` cannot release an over-aligned block, so `_aligned_malloc` must be paired with
// `_aligned_free`. Elsewhere `std::free` does accept `std::aligned_alloc` memory. Both halves are
// selected together here; the aligned `operator delete` overloads below route to
// qb_aligned_free(), never to std::free().
// NOTE the argument order differs between the two (`_aligned_malloc(size, align)` vs
// `aligned_alloc(align, size)`), which is why this is a function and not a bare macro.
inline void *
qb_aligned_alloc(std::size_t align, std::size_t size) noexcept {
#ifdef _MSC_VER
    return _aligned_malloc(size, align);
#else
    return std::aligned_alloc(align, size);
#endif
}
inline void
qb_aligned_free(void *p) noexcept {
#ifdef _MSC_VER
    _aligned_free(p);
#else
    std::free(p);
#endif
}

inline void *
tracked_alloc_aligned_nothrow(std::size_t n, std::align_val_t a) noexcept {
    const std::size_t align = static_cast<std::size_t>(a);
    // aligned_alloc requires a size that is a multiple of the alignment (harmless for
    // _aligned_malloc, which does not).
    const std::size_t size = ((n ? n : 1) + align - 1) / align * align;
    if (g_counting.load(std::memory_order_relaxed))
        g_bytes.fetch_add(size, std::memory_order_relaxed);
    return qb_aligned_alloc(align, size);
}

inline void *
tracked_alloc_aligned(std::size_t n, std::align_val_t a) {
    void *p = tracked_alloc_aligned_nothrow(n, a);
    if (!p)
        throw std::bad_alloc();
    return p;
}

} // namespace
// The replacement set must be COMPLETE. libstdc++ reaches for `operator new(size_t, nothrow_t)`
// (std::get_temporary_buffer) and, for over-aligned types, the `align_val_t` overloads. Replacing
// only the two plain forms left those going to the sanitizer's allocator while the plain
// `operator delete` below freed them with std::free -- ASan reports alloc-dealloc-mismatch and
// aborts. Found on Linux; macOS never took the nothrow path. Every allocating form below routes
// through the tracker and is freed by a matching form.
void *
operator new(std::size_t n) {
    return tracked_alloc(n);
}
void *
operator new[](std::size_t n) {
    return tracked_alloc(n);
}
void *
operator new(std::size_t n, const std::nothrow_t &) noexcept {
    return tracked_alloc_nothrow(n);
}
void *
operator new[](std::size_t n, const std::nothrow_t &) noexcept {
    return tracked_alloc_nothrow(n);
}
void *
operator new(std::size_t n, std::align_val_t a) {
    return tracked_alloc_aligned(n, a);
}
void *
operator new[](std::size_t n, std::align_val_t a) {
    return tracked_alloc_aligned(n, a);
}
void *
operator new(std::size_t n, std::align_val_t a, const std::nothrow_t &) noexcept {
    return tracked_alloc_aligned_nothrow(n, a);
}
void *
operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t &) noexcept {
    return tracked_alloc_aligned_nothrow(n, a);
}
void
operator delete(void *p) noexcept {
    std::free(p);
}
void
operator delete[](void *p) noexcept {
    std::free(p);
}
void
operator delete(void *p, std::size_t) noexcept {
    std::free(p);
}
void
operator delete[](void *p, std::size_t) noexcept {
    std::free(p);
}
void
operator delete(void *p, const std::nothrow_t &) noexcept {
    std::free(p);
}
void
operator delete[](void *p, const std::nothrow_t &) noexcept {
    std::free(p);
}
void
operator delete(void *p, std::align_val_t) noexcept {
    // Every ALIGNED form pairs with qb_aligned_free -- std::free here would be an
    // allocator-family mismatch (heap corruption on Windows, where the aligned block carries a
    // header std::free knows nothing about).
    qb_aligned_free(p);
}
void
operator delete[](void *p, std::align_val_t) noexcept {
    qb_aligned_free(p);
}
void
operator delete(void *p, std::size_t, std::align_val_t) noexcept {
    qb_aligned_free(p);
}
void
operator delete[](void *p, std::size_t, std::align_val_t) noexcept {
    qb_aligned_free(p);
}
void
operator delete(void *p, std::align_val_t, const std::nothrow_t &) noexcept {
    qb_aligned_free(p);
}
void
operator delete[](void *p, std::align_val_t, const std::nothrow_t &) noexcept {
    qb_aligned_free(p);
}
#endif // !QB_TEST_TSAN_ACTIVE

namespace {

bool
allocation_counter_is_live() {
    // Opaque size + touched buffer, or the compiler elides the new/delete pair at -O2 and the
    // probe reports a dead instrument on every optimized build.
    static volatile std::size_t opaque = 4096;
    const std::size_t           n      = opaque;
    AllocScope                  scope;
    auto                       *p = new char[n];
    p[0]                           = 'x';
    p[n - 1]                       = 'y';
    const auto    seen             = AllocScope::bytes();
    volatile char sink             = static_cast<char>(p[0] + p[n - 1]);
    (void) sink;
    delete[] p;
    return seen >= n;
}

/// Heap one frame may commit. Generous next to the 4096-element reserve cap, three orders of
/// magnitude below the ~512 MB the declared counts used to buy.
constexpr std::size_t kFrameBudget = 4u * 1024u * 1024u;

TEST(RespAggregateReserve, NestedDeclaredCountsCommitNoHeap) {
    // 64 nested aggregate headers, each declaring a million elements and delivering none. Well
    // under every individual limit; rejected by max_nesting_depth on the way down.
    std::string frame;
    for (int i = 0; i < 64; ++i)
        frame += "*1000000\r\n";
    ASSERT_LT(frame.size(), std::size_t{1024}) << "the whole frame is this small";

    ParserConfig config;
    std::size_t  used = 0;
    {
        AllocScope scope;
        (void) parse(frame, config);
        used = AllocScope::bytes();
    }

    if (!allocation_counter_is_live())
        GTEST_SKIP() << "the operator new replacement is not observing allocations on this toolchain";
    EXPECT_LE(used, kFrameBudget) << frame.size() << " bytes of input caused " << used
                                  << " bytes of heap to be committed from counts the peer never delivered";
}

TEST(RespAggregateReserve, CommittedHeapDoesNotScaleWithTheDeclaredCount) {
    if (!allocation_counter_is_live())
        GTEST_SKIP() << "the operator new replacement is not observing allocations on this toolchain";

    auto measure = [](const char *header) {
        const std::string frame = header;
        ParserConfig      config;
        AllocScope        scope;
        (void) parse(frame, config);
        return AllocScope::bytes();
    };

    const std::size_t small = measure("*2\r\n");
    const std::size_t huge  = measure("*1000000\r\n");

    EXPECT_LE(huge, small + kFrameBudget) << "declaring 1000000 elements instead of 2 cost " << (huge - small)
                                          << " extra bytes without a single element arriving";
}

TEST(RespAggregateReserve, OrdinaryAggregatesStillDecodeCorrectly) {
    // The cap must not change decoding for a real aggregate, including one comfortably larger than
    // the reserve cap (it now grows geometrically instead of being pre-sized).
    constexpr int elements = 6000;
    std::string   frame    = "*" + std::to_string(elements) + "\r\n";
    for (int i = 0; i < elements; ++i)
        frame += ":" + std::to_string(i) + "\r\n";

    ParserConfig config;
    auto         result = parse(frame, config);
    ASSERT_TRUE(result.has_value()) << "a well-formed aggregate must still decode";
}

} // namespace
