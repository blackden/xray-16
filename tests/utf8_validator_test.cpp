// Characterization test for xr_is_valid_utf8 (src/xrCore/xrCore.cpp:89-129).
//
// Standalone — no xrCore linkage. Mirror the algorithm here, exercise it
// against the canonical RFC 3629 corner cases plus a handful of fixtures that
// crop up in OpenXRay (cp1251 byte sequences masquerading as UTF-8, the
// Денис Федоров save filenames written under the old pw_gecos path, ImGui
// debug-overlay glyph ranges).
//
// Build:
//   clang++ -std=c++17 -Wall -Wextra -Werror tests/utf8_validator_test.cpp \
//       -o build-tests/utf8_validator_test
// Run:
//   ./build-tests/utf8_validator_test
//
// Exit 0 on success, non-zero on any failed assertion.

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>

// ----------- production-mirror: keep in sync with xrCore.cpp -----------------

static bool xr_is_valid_utf8(const char* buf)
{
    if (!buf)
        return true;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(buf);
    while (*p)
    {
        if (*p < 0x80)
        {
            ++p;
        }
        else if ((*p & 0xE0) == 0xC0)
        {
            if ((p[1] & 0xC0) != 0x80) return false;
            if (*p < 0xC2) return false; // overlong
            p += 2;
        }
        else if ((*p & 0xF0) == 0xE0)
        {
            if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return false;
            if (*p == 0xE0 && p[1] < 0xA0) return false; // overlong
            if (*p == 0xED && p[1] >= 0xA0) return false; // surrogate
            p += 3;
        }
        else if ((*p & 0xF8) == 0xF0)
        {
            if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return false;
            if (*p == 0xF0 && p[1] < 0x90) return false; // overlong
            if (*p == 0xF4 && p[1] >= 0x90) return false; // > U+10FFFF
            if (*p > 0xF4) return false;
            p += 4;
        }
        else
        {
            return false;
        }
    }
    return true;
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

#define CHECK_VALID(s)   CHECK(xr_is_valid_utf8(s) == true)
#define CHECK_INVALID(s) CHECK(xr_is_valid_utf8(s) == false)

// --- pure ASCII / boundaries ------------------------------------------------

static void test_null_and_empty()
{
    CHECK(xr_is_valid_utf8(nullptr) == true); // null tolerated as valid
    CHECK_VALID("");
}

static void test_ascii_passes()
{
    CHECK_VALID("hello world");
    CHECK_VALID("ragnar - quicksave.scop");
    CHECK_VALID("qwefqwefr");
    CHECK_VALID("ABCDEFG abcdefg 0123456789 !@#$%^&*()");
}

// --- 2-byte sequences (cyrillic U+0400-U+04FF lives here) -------------------

static void test_valid_two_byte()
{
    // "Привет" = U+041F U+0440 U+0438 U+0432 U+0435 U+0442
    // UTF-8: D0 9F D0 A0 D0 B8 D0 B2 D0 B5 D0 B2
    CHECK_VALID("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"); // Привет
    CHECK_VALID("\xC2\x80");     // U+0080 — smallest valid 2-byte
    CHECK_VALID("\xDF\xBF");     // U+07FF — largest 2-byte
}

static void test_invalid_two_byte_overlong()
{
    // "Encoding U+0000 as C0 80" — overlong null, forbidden by RFC 3629.
    CHECK_INVALID("\xC0\x80");
    // U+007F as C1 BF — overlong representation of ASCII.
    CHECK_INVALID("\xC1\xBF");
    // C2 followed by continuation-less byte — truncated.
    CHECK_INVALID("\xC2");
    // C2 followed by non-continuation byte. String-literal concat keeps the
    // hex escape bounded to two digits — `"\xC2A"` would be read as `\xC2A`
    // (three hex digits, > 0xFF) by clang.
    CHECK_INVALID("\xC2" "A");
}

// --- 3-byte sequences (rest of BMP) -----------------------------------------

static void test_valid_three_byte()
{
    // U+20AC € (Euro sign) — common BMP test point.
    CHECK_VALID("\xE2\x82\xAC");
    CHECK_VALID("\xE0\xA0\x80");     // U+0800 — smallest valid 3-byte
    CHECK_VALID("\xEF\xBF\xBD");     // U+FFFD — replacement char
}

static void test_invalid_three_byte_overlong()
{
    // E0 80 80 would be U+0000 — forbidden overlong.
    CHECK_INVALID("\xE0\x80\x80");
    // E0 9F BF — overlong representation of U+07FF (which fits in 2 bytes).
    CHECK_INVALID("\xE0\x9F\xBF");
}

static void test_invalid_three_byte_surrogates()
{
    // U+D800 — high surrogate, forbidden in UTF-8 per RFC 3629.
    CHECK_INVALID("\xED\xA0\x80");
    // U+DFFF — low surrogate, also forbidden.
    CHECK_INVALID("\xED\xBF\xBF");
}

// --- 4-byte sequences (supplementary planes) --------------------------------

static void test_valid_four_byte()
{
    // U+1F600 😀 — common emoji range.
    CHECK_VALID("\xF0\x9F\x98\x80");
    CHECK_VALID("\xF0\x90\x80\x80");     // U+10000 — smallest valid 4-byte
    CHECK_VALID("\xF4\x8F\xBF\xBF");     // U+10FFFF — largest valid codepoint
}

static void test_invalid_four_byte_out_of_range()
{
    // F5 .. F7 prefix — would encode > U+10FFFF; forbidden.
    CHECK_INVALID("\xF5\x80\x80\x80");
    // F4 90 .. — > U+10FFFF.
    CHECK_INVALID("\xF4\x90\x80\x80");
    // F0 8F overlong (representing < U+10000 in 4 bytes).
    CHECK_INVALID("\xF0\x8F\xBF\xBF");
}

// --- malformed structural ---------------------------------------------------

static void test_invalid_lone_continuation()
{
    // 0x80-0xBF as standalone byte is a continuation byte without start.
    CHECK_INVALID("\x80");
    CHECK_INVALID("A" "\x80" "B");
}

static void test_invalid_truncated_sequences()
{
    // E0 then end-of-string — needs 2 continuation bytes.
    CHECK_INVALID("\xE0\xA0");
    // F0 then 1 continuation — needs 3 continuation bytes.
    CHECK_INVALID("\xF0\x90");
    // F0 then 2 continuation — still needs 3rd.
    CHECK_INVALID("\xF0\x90\x80");
}

// --- the actual production scenarios that motivated the function ------------

static void test_cp1251_filename_bytes_rejected()
{
    // "ragnar - прибытие на Скадовск" as cp1251 bytes (high-byte cyrillic).
    // The exact pattern that hit EILSEQ on APFS.
    // cp1251: п=0xEF, р=0xF0, и=0xE8, б=0xE1, ы=0xFB, т=0xF2, и=0xE8, е=0xE5
    const char cp1251_bytes[] =
        "ragnar - \xEF\xF0\xE8\xE1\xFB\xF2\xE8\xE5"; // "ragnar - прибытие" in cp1251
    CHECK_INVALID(cp1251_bytes);
}

static void test_legacy_utf8_save_name_accepted()
{
    // The old "Денис Федоров - arrival at the skadovsk.scop" filenames written
    // when Core.UserName came from pw_gecos as UTF-8.
    // "Денис" = D0 94 D0 B5 D0 BD D0 B8 D1 81
    const char utf8_bytes[] =
        "\xD0\x94\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x81 \xD0\xA4\xD0\xB5\xD0\xB4\xD0\xBE\xD1\x80\xD0\xBE\xD0\xB2"
        " - arrival at the skadovsk.scop";
    CHECK_VALID(utf8_bytes);
}

static void test_mixed_ascii_and_cyrillic_utf8_accepted()
{
    // Today's autosave names after our fix: ASCII username + UTF-8 cyrillic event.
    const char utf8_mixed[] =
        "ragnar - \xD0\xBF\xD1\x80\xD0\xB8\xD0\xB1\xD1\x8B\xD1\x82\xD0\xB8\xD0\xB5"
        " \xD0\xBD\xD0\xB0 \xC2\xAB\xD0\xA1\xD0\xBA\xD0\xB0\xD0\xB4\xD0\xBE\xD0\xB2\xD1\x81\xD0\xBA\xC2\xBB.scop";
    CHECK_VALID(utf8_mixed);
}

static void test_bom_accepted()
{
    // EF BB BF is the UTF-8 BOM; valid as a leading codepoint U+FEFF.
    CHECK_VALID("\xEF\xBB\xBF" "hello");
}

int main()
{
    std::cout << "== xr_is_valid_utf8 characterization tests ==\n";
    test_null_and_empty();
    test_ascii_passes();
    test_valid_two_byte();
    test_invalid_two_byte_overlong();
    test_valid_three_byte();
    test_invalid_three_byte_overlong();
    test_invalid_three_byte_surrogates();
    test_valid_four_byte();
    test_invalid_four_byte_out_of_range();
    test_invalid_lone_continuation();
    test_invalid_truncated_sequences();
    test_cp1251_filename_bytes_rejected();
    test_legacy_utf8_save_name_accepted();
    test_mixed_ascii_and_cyrillic_utf8_accepted();
    test_bom_accepted();
    std::cout << "Summary: " << g_passes << " passed, "
              << g_failures << " failed\n";
    return g_failures == 0 ? 0 : 1;
}
