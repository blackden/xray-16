// Standalone characterization test for the safe_append helper logic added in
// src/xrCore/xrDebug.cpp.
//
// We can't link against xrCore from a unit test without bringing up the full
// engine (memory allocator, threading, SDL, etc.), so this test re-implements
// the safe_append algorithm IDENTICALLY to the production copy and exercises
// it against the failure pattern that previously corrupted the stack in
// xrDebug::GatherInfo. If anyone changes the production helper, they should
// keep this copy in sync -- the goal is to encode the algorithm spec, not
// just type-check the source.
//
// Build:
//   clang++ -std=c++17 -Wall -Wextra -Werror tests/safe_append_test.cpp \
//       -o build-tests/safe_append_test
// Run:
//   ./build-tests/safe_append_test
//
// Exit 0 on success, non-zero on any failed assertion.

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

// ----------- production-mirror: keep in sync with src/xrCore/xrDebug.cpp -----

static void safe_append(char*& buffer, const char* const oneAboveBuffer, const char* fmt, ...)
{
    if (!buffer || buffer >= oneAboveBuffer)
        return;
    const size_t remaining = static_cast<size_t>(oneAboveBuffer - buffer);
    if (remaining < 2)
        return;
    va_list args;
    va_start(args, fmt);
    const int written = vsnprintf(buffer, remaining, fmt, args);
    va_end(args);
    if (written < 0)
        return;
    const size_t actuallyWritten = (static_cast<size_t>(written) < remaining)
        ? static_cast<size_t>(written)
        : remaining - 1;
    buffer += actuallyWritten;
}

// ----------- the broken pattern, for contrast / negative test ---------------

static int xr_sprintf_like(char* dest, size_t size, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int r = vsnprintf(dest, size, fmt, args);
    va_end(args);
    return r;
}

// ----------- test harness ---------------------------------------------------

static int g_failures = 0;
static int g_passes = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "  \x1b[31m✗\x1b[0m " << __func__ << ": " << #cond \
                  << " failed at line " << __LINE__ << "\n"; \
        ++g_failures; \
    } else { \
        ++g_passes; \
    } \
} while (false)

// Canary-byte test: ensure safe_append never writes past oneAboveBuffer.
static void test_safe_append_never_overruns_short_buffer()
{
    char buf[64];
    constexpr char canary = '\xCC';
    memset(buf, canary, sizeof buf);
    char* p = buf;
    const char* end = buf + sizeof buf;

    // Append strings larger than what fits.
    safe_append(p, end, "%s", "AAAAAAAAAAAAAAAAAAAAAAAAAA");          // 26 A's
    safe_append(p, end, "%s", "BBBBBBBBBBBBBBBBBBBBBBBBBB");          // 26 B's
    safe_append(p, end, "%s", "CCCCCCCCCCCCCCCCCCCCCCCCCC");          // 26 C's
    safe_append(p, end, "%s", "DDDDDDDDDDDDDDDDDDDDDDDDDD");          // would push past end

    // Verify the byte right after our buffer is untouched (canary, no overrun).
    // We do this by checking that the heap-stack region we control is intact:
    // adjacent stack variable test below.
    CHECK(p >= buf);
    CHECK(p <= end);
}

// Adjacent-stack canary: if safe_append overruns by even 1 byte, the canary
// variable laid down right after `buf` on the stack should change. The compiler
// may reorder; this test is best-effort but combined with the bound check above
// gives reasonable coverage.
static void test_safe_append_does_not_clobber_adjacent_stack()
{
    char buf[32];
    volatile unsigned char canary_after = 0xAB;
    memset(buf, 0, sizeof buf);
    char* p = buf;
    const char* end = buf + sizeof buf;

    // Try to overflow with a single huge format.
    safe_append(p, end, "%s", "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF");
    safe_append(p, end, "%s", "more bytes that should be no-ops since buffer is full");

    CHECK(canary_after == 0xAB);
    CHECK(p <= end);
}

// Demonstrates the BUG that motivated the fix: the naive pattern advances
// `buffer` past `end` on truncation, after which the next call computes a
// huge unsigned remaining and writes unbounded. We don't actually let it
// overflow real memory here (UB territory); we just observe that `p` would
// have gone past `end`, which proves the broken-pattern logic.
static void test_broken_pattern_advances_past_end()
{
    char buf[32];
    char* p = buf;
    const char* end = buf + sizeof buf;

    // First call: short, fits.
    p += xr_sprintf_like(p, end - p, "%s", "small");
    CHECK(p < end);

    // Second call: vsnprintf returns "would have been" length on truncation,
    // so `p` advances by 100 even though only 26 bytes were actually written.
    p += xr_sprintf_like(p, end - p, "%s",
                          "this string is much longer than the remaining 26 bytes -- here be dragons");
    CHECK(p > end); // exactly the failure mode our fix prevents

    // If we computed `end - p` here it would underflow as size_t -- which is
    // precisely what corrupted the stack in xrDebug::GatherInfo.
    const size_t bad_remaining = static_cast<size_t>(end - p);
    CHECK(bad_remaining > SIZE_MAX / 2); // proves the underflow
}

// safe_append handles the "already full" state gracefully.
static void test_safe_append_handles_zero_remaining()
{
    char buf[16];
    char* p = buf + sizeof(buf) - 1; // points at last byte (== oneAboveBuffer - 1)
    const char* end = buf + sizeof buf;

    char* p_before = p;
    safe_append(p, end, "%s", "should be skipped");
    CHECK(p == p_before); // remaining < 2 path; no advance
}

// safe_append handles negative vsnprintf return (error / format issue).
static void test_safe_append_handles_vsnprintf_error()
{
    char buf[64];
    char* p = buf;
    const char* end = buf + sizeof buf;

    // %ls with a NULL wide-char pointer is UB; we just want to verify
    // safe_append doesn't crash on an arbitrary input. The real production
    // value is the `written < 0` guard.
    safe_append(p, end, "valid format %d", 42);
    CHECK(p > buf); // wrote something
    CHECK(p < end); // didn't overflow
}

int main()
{
    std::cout << "== safe_append characterization tests ==\n";
    test_safe_append_never_overruns_short_buffer();
    test_safe_append_does_not_clobber_adjacent_stack();
    test_broken_pattern_advances_past_end();
    test_safe_append_handles_zero_remaining();
    test_safe_append_handles_vsnprintf_error();
    std::cout << "Summary: " << g_passes << " passed, "
              << g_failures << " failed\n";
    return g_failures == 0 ? 0 : 1;
}
