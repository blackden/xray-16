// Characterization test for xr_decode_utf8 (StringConversion.cpp).
//
// Mirrors the production decoder exactly. This is the primitive that the
// Phase 1 font renderer migration leans on: each call advances the pointer
// past one UTF-8 codepoint and returns the codepoint in u32 form. Strict per
// RFC 3629 — overlong / surrogate / out-of-range sequences yield U+FFFD and
// the pointer advances one byte so the caller keeps making progress.
//
// Build:
//   clang++ -std=c++17 -Wall -Wextra -Werror tests/utf8_decode_test.cpp \
//       -o build-tests/utf8_decode_test
// Run:
//   ./build-tests/utf8_decode_test

#include <cstdint>
#include <cstring>
#include <iostream>

using xr_codepoint = uint32_t;
inline constexpr xr_codepoint XR_UNICODE_REPLACEMENT = 0xFFFD;

// ----------- production-mirror: StringConversion.cpp ------------------------

static bool xr_decode_utf8(const char*& p, xr_codepoint& cp)
{
    cp = 0;
    if (!p)
        return false;
    const unsigned char* up = reinterpret_cast<const unsigned char*>(p);
    if (*up == 0)
        return false;

    if (*up < 0x80)
    {
        cp = *up;
        p += 1;
        return true;
    }
    if ((*up & 0xE0) == 0xC0)
    {
        if (*up < 0xC2 || (up[1] & 0xC0) != 0x80)
        {
            cp = XR_UNICODE_REPLACEMENT;
            p += 1;
            return false;
        }
        cp = (xr_codepoint(*up & 0x1F) << 6) | xr_codepoint(up[1] & 0x3F);
        p += 2;
        return true;
    }
    if ((*up & 0xF0) == 0xE0)
    {
        if ((up[1] & 0xC0) != 0x80 || (up[2] & 0xC0) != 0x80
            || (*up == 0xE0 && up[1] < 0xA0)
            || (*up == 0xED && up[1] >= 0xA0))
        {
            cp = XR_UNICODE_REPLACEMENT;
            p += 1;
            return false;
        }
        cp = (xr_codepoint(*up & 0x0F) << 12)
           | (xr_codepoint(up[1] & 0x3F) << 6)
           | xr_codepoint(up[2] & 0x3F);
        p += 3;
        return true;
    }
    if ((*up & 0xF8) == 0xF0)
    {
        if (*up > 0xF4
            || (up[1] & 0xC0) != 0x80 || (up[2] & 0xC0) != 0x80 || (up[3] & 0xC0) != 0x80
            || (*up == 0xF0 && up[1] < 0x90)
            || (*up == 0xF4 && up[1] >= 0x90))
        {
            cp = XR_UNICODE_REPLACEMENT;
            p += 1;
            return false;
        }
        cp = (xr_codepoint(*up & 0x07) << 18)
           | (xr_codepoint(up[1] & 0x3F) << 12)
           | (xr_codepoint(up[2] & 0x3F) << 6)
           | xr_codepoint(up[3] & 0x3F);
        p += 4;
        return true;
    }

    cp = XR_UNICODE_REPLACEMENT;
    p += 1;
    return false;
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

// Convenience: decode all codepoints from a string and assert position +
// codepoint sequence match expectations.
static void decode_all(const char* input, xr_codepoint* out_cp, size_t* out_n,
                       const char** end_p)
{
    *out_n = 0;
    const char* p = input;
    xr_codepoint cp;
    while (xr_decode_utf8(p, cp))
        out_cp[(*out_n)++] = cp;
    *end_p = p;
}

// --- null / empty / boundary -----------------------------------------------

static void test_null_pointer()
{
    const char* p = nullptr;
    xr_codepoint cp = 99;
    CHECK(xr_decode_utf8(p, cp) == false);
    CHECK(cp == 0);
}

static void test_empty_string()
{
    const char* p = "";
    xr_codepoint cp = 99;
    CHECK(xr_decode_utf8(p, cp) == false);
    CHECK(cp == 0);
    CHECK(p == nullptr || *p == 0); // pointer left at terminator
}

// --- ASCII pass-through ----------------------------------------------------

static void test_ascii_single()
{
    const char* p = "A";
    xr_codepoint cp;
    CHECK(xr_decode_utf8(p, cp));
    CHECK(cp == 'A');
    CHECK(*p == 0); // advanced to terminator
}

static void test_ascii_sequence()
{
    xr_codepoint out[16];
    size_t n;
    const char* end;
    decode_all("hello", out, &n, &end);
    CHECK(n == 5);
    CHECK(out[0] == 'h');
    CHECK(out[1] == 'e');
    CHECK(out[2] == 'l');
    CHECK(out[3] == 'l');
    CHECK(out[4] == 'o');
}

// --- 2-byte sequences ------------------------------------------------------

static void test_cyrillic_two_byte()
{
    // "Привет" — 6 cyrillic codepoints.
    const char* p = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    xr_codepoint cp;
    CHECK(xr_decode_utf8(p, cp)); CHECK(cp == 0x041F); // П
    CHECK(xr_decode_utf8(p, cp)); CHECK(cp == 0x0440); // р
    CHECK(xr_decode_utf8(p, cp)); CHECK(cp == 0x0438); // и
    CHECK(xr_decode_utf8(p, cp)); CHECK(cp == 0x0432); // в
    CHECK(xr_decode_utf8(p, cp)); CHECK(cp == 0x0435); // е
    CHECK(xr_decode_utf8(p, cp)); CHECK(cp == 0x0442); // т
    CHECK(xr_decode_utf8(p, cp) == false); // EOS
}

static void test_overlong_two_byte_rejected()
{
    // C0 80 is overlong U+0000 — forbidden.
    const char* p = "\xC0\x80";
    xr_codepoint cp;
    CHECK(xr_decode_utf8(p, cp) == false);
    CHECK(cp == XR_UNICODE_REPLACEMENT);
    CHECK(p == &"\xC0\x80"[1]); // advanced by 1 byte after rejection
}

// --- 3-byte sequences ------------------------------------------------------

static void test_three_byte_euro()
{
    const char* p = "\xE2\x82\xAC"; // € U+20AC
    xr_codepoint cp;
    CHECK(xr_decode_utf8(p, cp));
    CHECK(cp == 0x20AC);
    CHECK(*p == 0);
}

static void test_three_byte_overlong_rejected()
{
    // E0 9F BF is overlong for U+07FF (fits in 2 bytes).
    const char* p = "\xE0\x9F\xBF";
    xr_codepoint cp;
    CHECK(xr_decode_utf8(p, cp) == false);
    CHECK(cp == XR_UNICODE_REPLACEMENT);
}

static void test_surrogate_rejected()
{
    // U+D800 (high surrogate) encoded as ED A0 80 — forbidden.
    const char* p = "\xED\xA0\x80";
    xr_codepoint cp;
    CHECK(xr_decode_utf8(p, cp) == false);
    CHECK(cp == XR_UNICODE_REPLACEMENT);
}

// --- 4-byte sequences ------------------------------------------------------

static void test_four_byte_emoji()
{
    const char* p = "\xF0\x9F\x98\x80"; // 😀 U+1F600
    xr_codepoint cp;
    CHECK(xr_decode_utf8(p, cp));
    CHECK(cp == 0x1F600);
}

static void test_four_byte_max_codepoint()
{
    const char* p = "\xF4\x8F\xBF\xBF"; // U+10FFFF (max valid)
    xr_codepoint cp;
    CHECK(xr_decode_utf8(p, cp));
    CHECK(cp == 0x10FFFF);
}

static void test_four_byte_out_of_range_rejected()
{
    // F4 90 .. encodes > U+10FFFF — forbidden.
    const char* p = "\xF4\x90\x80\x80";
    xr_codepoint cp;
    CHECK(xr_decode_utf8(p, cp) == false);
    CHECK(cp == XR_UNICODE_REPLACEMENT);
}

// --- malformed input recovery ----------------------------------------------

static void test_lone_continuation_advances_one_byte()
{
    // Bare 0x80 (continuation byte without lead) — decoder emits replacement
    // and advances 1 byte so the caller makes progress.
    const char* p = "\x80""hello";
    xr_codepoint cp;
    CHECK(xr_decode_utf8(p, cp) == false);
    CHECK(cp == XR_UNICODE_REPLACEMENT);
    // Pointer must have advanced past the bad byte.
    CHECK(*p == 'h');
    // Subsequent decodes work normally.
    CHECK(xr_decode_utf8(p, cp)); CHECK(cp == 'h');
}

static void test_truncated_sequence_advances_one_byte()
{
    // E0 alone (3-byte lead with no continuations) — emit replacement, advance 1.
    const char* p = "\xE0X";
    xr_codepoint cp;
    CHECK(xr_decode_utf8(p, cp) == false);
    CHECK(*p == 'X');
}

// --- mixed real-world strings ---------------------------------------------

static void test_mixed_ascii_cyrillic()
{
    // The autosave name pattern after our cp1251→UTF-8 fix:
    // "ragnar - П"
    xr_codepoint out[16];
    size_t n;
    const char* end;
    decode_all("ragnar - \xD0\x9F", out, &n, &end);
    // "ragnar - П" → r(0) a(1) g(2) n(3) a(4) r(5) space(6) -(7) space(8) П(9)
    CHECK(n == 10);
    CHECK(out[0] == 'r');
    CHECK(out[5] == 'r');
    CHECK(out[6] == ' ');
    CHECK(out[7] == '-');
    CHECK(out[8] == ' ');
    CHECK(out[9] == 0x041F); // last codepoint is П (single, not split)
}

static void test_full_skadovsk_label()
{
    // "прибытие на Скадовск" — 20 codepoints, 38 UTF-8 bytes.
    const char input[] =
        "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB1\xD1\x8B\xD1\x82\xD0\xB8\xD0\xB5"
        " "
        "\xD0\xBD\xD0\xB0"
        " "
        "\xD0\xA1\xD0\xBA\xD0\xB0\xD0\xB4\xD0\xBE\xD0\xB2\xD1\x81\xD0\xBA";
    xr_codepoint out[32];
    size_t n;
    const char* end;
    decode_all(input, out, &n, &end);
    CHECK(n == 20);
    CHECK(out[0] == 0x043F); // п
    CHECK(out[8] == 0x0020); // space
    CHECK(out[12] == 0x0421); // С (capital)
    CHECK(out[19] == 0x043A); // к (last codepoint)
}

static void test_advance_position_per_codepoint()
{
    // Verify pointer position math: after decoding "Привет" (6 cyrillic
    // codepoints, 12 UTF-8 bytes), pointer should be at byte 12.
    const char input[] = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    const char* p = input;
    xr_codepoint cp;
    int decoded = 0;
    while (xr_decode_utf8(p, cp))
        ++decoded;
    CHECK(decoded == 6);
    CHECK((p - input) == 12);
}

static void test_cp1251_bytes_yield_replacements()
{
    // cp1251 high bytes look like UTF-8 leads but lack continuations →
    // every byte emits a replacement char in our strict decoder, position
    // advances by 1 each time. That's the correct behavior — strict UTF-8
    // can't recover cp1251 semantics; the FS layer/XML shim does that
    // conversion upstream.
    const char input[] = "\xEF\xF0\xE8\xE1"; // "приб" in cp1251
    const char* p = input;
    xr_codepoint cp;
    int replacements = 0;
    int total = 0;
    while (*p) {
        bool ok = xr_decode_utf8(p, cp);
        ++total;
        if (!ok && cp == XR_UNICODE_REPLACEMENT)
            ++replacements;
        if (total > 16) break; // sanity
    }
    CHECK(replacements == 4);
    CHECK((p - input) == 4); // pointer walked all 4 bytes
}

int main()
{
    std::cout << "== xr_decode_utf8 characterization tests ==\n";
    test_null_pointer();
    test_empty_string();
    test_ascii_single();
    test_ascii_sequence();
    test_cyrillic_two_byte();
    test_overlong_two_byte_rejected();
    test_three_byte_euro();
    test_three_byte_overlong_rejected();
    test_surrogate_rejected();
    test_four_byte_emoji();
    test_four_byte_max_codepoint();
    test_four_byte_out_of_range_rejected();
    test_lone_continuation_advances_one_byte();
    test_truncated_sequence_advances_one_byte();
    test_mixed_ascii_cyrillic();
    test_full_skadovsk_label();
    test_advance_position_per_codepoint();
    test_cp1251_bytes_yield_replacements();
    std::cout << "Summary: " << g_passes << " passed, "
              << g_failures << " failed\n";
    return g_failures == 0 ? 0 : 1;
}
