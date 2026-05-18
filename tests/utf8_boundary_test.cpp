// Characterization test for the UTF-8 cursor boundary primitives used by
// line_edit_control (Phase 1.6 of the utf8 migration). The actual cursor
// lives behind the SDL/Device singletons and is hard to fixture, so we
// test the pure helpers in StringConversion.hpp directly and replay the
// same boundary rules the cursor follows.
//
// Build:
//   clang++ -std=c++17 -Wall -Wextra -Werror tests/utf8_boundary_test.cpp \
//       -o build-tests/utf8_boundary_test
// Run:
//   ./build-tests/utf8_boundary_test

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

// --- DUT mirrors xrCore/Text/StringConversion.hpp (no link to xrCore) -----

static inline bool xr_utf8_is_continuation(unsigned char b) { return (b & 0xC0) == 0x80; }

static inline size_t xr_utf8_lead_size(unsigned char b)
{
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

// --- The four cursor ops the line-edit needs ------------------------------

// move_left: step back at least one byte, then skip any continuation bytes.
static size_t move_left(const char* str, size_t pos)
{
    if (pos == 0) return 0;
    --pos;
    while (pos > 0 && xr_utf8_is_continuation(static_cast<unsigned char>(str[pos])))
        --pos;
    return pos;
}

// move_right: advance by the codepoint width at the current position.
static size_t move_right(const char* str, size_t pos, size_t len)
{
    if (pos >= len) return len;
    const size_t step = xr_utf8_lead_size(static_cast<unsigned char>(str[pos]));
    if (pos + step > len) return len;
    return pos + step;
}

// snap_to_boundary: if pos lands on a continuation byte, back up to the
// start of that codepoint. Mirrors clamp_cur_pos's safety net.
static size_t snap_to_boundary(const char* str, size_t pos, size_t len)
{
    if (pos > len) pos = len;
    while (pos > 0 && pos < len && xr_utf8_is_continuation(static_cast<unsigned char>(str[pos])))
        --pos;
    return pos;
}

// --- harness --------------------------------------------------------------

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

// --- lead-size & continuation predicates ----------------------------------

static void test_lead_size_ascii()
{
    CHECK(xr_utf8_lead_size('a') == 1);
    CHECK(xr_utf8_lead_size('\0') == 1);
    CHECK(xr_utf8_lead_size(0x7F) == 1);
}

static void test_lead_size_2_3_4_byte()
{
    CHECK(xr_utf8_lead_size(0xC2) == 2); // smallest valid 2-byte lead
    CHECK(xr_utf8_lead_size(0xDF) == 2); // largest 2-byte lead
    CHECK(xr_utf8_lead_size(0xE0) == 3);
    CHECK(xr_utf8_lead_size(0xEF) == 3);
    CHECK(xr_utf8_lead_size(0xF0) == 4);
    CHECK(xr_utf8_lead_size(0xF4) == 4);
}

static void test_lead_size_progress_on_bogus_byte()
{
    // Continuation byte as "lead" or forbidden 5-byte lead: return 1 so
    // a tight loop keeps making progress past corruption.
    CHECK(xr_utf8_lead_size(0x80) == 1);
    CHECK(xr_utf8_lead_size(0xBF) == 1);
    CHECK(xr_utf8_lead_size(0xF8) == 1);
}

static void test_continuation_predicate()
{
    CHECK(!xr_utf8_is_continuation(0x00));
    CHECK(!xr_utf8_is_continuation(0x7F));
    CHECK(xr_utf8_is_continuation(0x80));
    CHECK(xr_utf8_is_continuation(0xBF));
    CHECK(!xr_utf8_is_continuation(0xC0));
    CHECK(!xr_utf8_is_continuation(0xF0));
}

// --- cursor on the canonical "привет" line ("П"=D0 9F, "р"=D1 80, ...) ----
//
// Byte layout of "привет" (cyrillic 6 letters, 12 bytes):
//   D0 9F  D1 80  D0 B8  D0 B2  D0 B5  D1 82
//   0  1   2  3   4  5   6  7   8  9   10 11
// Codepoint boundaries: 0, 2, 4, 6, 8, 10, 12.

static const char* PRIVET = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
static const size_t PRIVET_LEN = 12;

static void test_move_right_through_cyrillic()
{
    size_t pos = 0;
    pos = move_right(PRIVET, pos, PRIVET_LEN); CHECK(pos == 2);
    pos = move_right(PRIVET, pos, PRIVET_LEN); CHECK(pos == 4);
    pos = move_right(PRIVET, pos, PRIVET_LEN); CHECK(pos == 6);
    pos = move_right(PRIVET, pos, PRIVET_LEN); CHECK(pos == 8);
    pos = move_right(PRIVET, pos, PRIVET_LEN); CHECK(pos == 10);
    pos = move_right(PRIVET, pos, PRIVET_LEN); CHECK(pos == 12);
    pos = move_right(PRIVET, pos, PRIVET_LEN); CHECK(pos == 12); // EOL clamp
}

static void test_move_left_through_cyrillic()
{
    size_t pos = PRIVET_LEN;
    pos = move_left(PRIVET, pos); CHECK(pos == 10);
    pos = move_left(PRIVET, pos); CHECK(pos == 8);
    pos = move_left(PRIVET, pos); CHECK(pos == 6);
    pos = move_left(PRIVET, pos); CHECK(pos == 4);
    pos = move_left(PRIVET, pos); CHECK(pos == 2);
    pos = move_left(PRIVET, pos); CHECK(pos == 0);
    pos = move_left(PRIVET, pos); CHECK(pos == 0); // BOL clamp
}

static void test_snap_to_boundary_from_continuation()
{
    // Any odd byte index inside a 2-byte codepoint must snap back to the lead.
    CHECK(snap_to_boundary(PRIVET, 1, PRIVET_LEN) == 0);
    CHECK(snap_to_boundary(PRIVET, 3, PRIVET_LEN) == 2);
    CHECK(snap_to_boundary(PRIVET, 5, PRIVET_LEN) == 4);
    // Already on a boundary -> identity.
    CHECK(snap_to_boundary(PRIVET, 0, PRIVET_LEN) == 0);
    CHECK(snap_to_boundary(PRIVET, 2, PRIVET_LEN) == 2);
    CHECK(snap_to_boundary(PRIVET, 12, PRIVET_LEN) == 12);
}

// --- mixed ASCII + cyrillic (real save-name shape) ------------------------

static const char* MIXED = "ragnar - \xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
//                          r a g n a r   -   П П р р и и в в е е т т
//                          0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20
static const size_t MIXED_LEN = 21;

static void test_move_right_through_mixed()
{
    size_t pos = 0;
    // Walk the ASCII prefix "ragnar - " (9 bytes, each step = 1).
    for (size_t i = 1; i <= 9; ++i)
    {
        pos = move_right(MIXED, pos, MIXED_LEN);
        CHECK(pos == i);
    }
    // Now the cyrillic suffix, 2 bytes per codepoint.
    pos = move_right(MIXED, pos, MIXED_LEN); CHECK(pos == 11);
    pos = move_right(MIXED, pos, MIXED_LEN); CHECK(pos == 13);
    pos = move_right(MIXED, pos, MIXED_LEN); CHECK(pos == 15);
    pos = move_right(MIXED, pos, MIXED_LEN); CHECK(pos == 17);
    pos = move_right(MIXED, pos, MIXED_LEN); CHECK(pos == 19);
    pos = move_right(MIXED, pos, MIXED_LEN); CHECK(pos == 21);
    pos = move_right(MIXED, pos, MIXED_LEN); CHECK(pos == 21);
}

static void test_move_left_through_mixed()
{
    size_t pos = MIXED_LEN;
    pos = move_left(MIXED, pos); CHECK(pos == 19); // back over "т"
    pos = move_left(MIXED, pos); CHECK(pos == 17);
    pos = move_left(MIXED, pos); CHECK(pos == 15);
    pos = move_left(MIXED, pos); CHECK(pos == 13);
    pos = move_left(MIXED, pos); CHECK(pos == 11);
    pos = move_left(MIXED, pos); CHECK(pos == 9);
    pos = move_left(MIXED, pos); CHECK(pos == 8);  // back over ' '
    pos = move_left(MIXED, pos); CHECK(pos == 7);  // back over '-'
}

// --- 3-byte and 4-byte sequences ------------------------------------------

static void test_move_right_three_byte()
{
    // U+20AC EURO SIGN = E2 82 AC
    const char* s = "\xE2\x82\xAC";
    size_t pos = 0;
    pos = move_right(s, pos, 3); CHECK(pos == 3);
}

static void test_move_right_four_byte()
{
    // U+1F600 grinning face = F0 9F 98 80
    const char* s = "\xF0\x9F\x98\x80";
    size_t pos = 0;
    pos = move_right(s, pos, 4); CHECK(pos == 4);
    pos = move_left(s, pos);     CHECK(pos == 0);
}

// --- snap on truncated input ----------------------------------------------

static void test_move_right_truncated_does_not_overrun()
{
    // A 4-byte lead with only 2 bytes available: clamp to end.
    const char* s = "\xF0\x9F";
    size_t pos = 0;
    pos = move_right(s, pos, 2);
    CHECK(pos == 2);
}

int main()
{
    std::cout << "== UTF-8 cursor boundary characterization tests ==\n";

    test_lead_size_ascii();
    test_lead_size_2_3_4_byte();
    test_lead_size_progress_on_bogus_byte();
    test_continuation_predicate();
    test_move_right_through_cyrillic();
    test_move_left_through_cyrillic();
    test_snap_to_boundary_from_continuation();
    test_move_right_through_mixed();
    test_move_left_through_mixed();
    test_move_right_three_byte();
    test_move_right_four_byte();
    test_move_right_truncated_does_not_overrun();

    std::cout << "Summary: " << g_passes << " passed, "
              << g_failures << " failed\n";
    return g_failures == 0 ? 0 : 1;
}
