// Characterization test for mbhMulti2Wide (src/xrCore/Text/StringConversion.cpp:70-159).
//
// This UTF-8 → u16 wide-char decoder is the engine's existing multibyte font
// path. The Phase 1 migration unifies the single-byte font path with this one,
// so we pin its current behavior here as a regression net. In particular:
//   - ASCII byte → wide char with same value
//   - Valid 2-byte UTF-8 → one BMP codepoint
//   - Valid 3-byte UTF-8 → one BMP codepoint
//   - Malformed bytes (cp1251 high-byte fed as if UTF-8) → DUMB fallback
//     where each byte becomes one wide char unchanged
//   - WidePos array maps wide-char index → starting byte offset
//
// Mirror the algorithm IDENTICALLY to the production copy so this test pins
// the spec, not the linkage.
//
// Build:
//   clang++ -std=c++17 -Wall -Wextra -Werror tests/mb_decode_test.cpp \
//       -o build-tests/mb_decode_test
// Run:
//   ./build-tests/mb_decode_test

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

using u8 = uint8_t;
using u16 = uint16_t;
using xr_wide_char = u16;

#define BITS1_MASK 0x80
#define BITS2_MASK 0xC0
#define BITS3_MASK 0xE0
#define BITS4_MASK 0xF0

#define BITS1_EXP 0x00
#define BITS2_EXP 0x80
#define BITS3_EXP 0xC0
#define BITS4_EXP 0xE0

// ----------- production-mirror: StringConversion.cpp -------------------------

static u16 mbhMulti2WideDumb(xr_wide_char* WideStr, xr_wide_char* WidePos,
                              u16 WideStrSize, const char* MultiStr)
{
    u16 spos = 0, dpos = 0;
    u8 b1;
    xr_wide_char wc = 0;

    if (!MultiStr[0])
        return 0;

    (void)WideStrSize;

    while ((b1 = MultiStr[spos++]) != 0x00)
    {
        if (WidePos)
            WidePos[dpos] = spos;
        dpos++;
        wc = b1;
        if (WideStr)
            WideStr[dpos] = wc;
    }
    if (WidePos)
        WidePos[dpos] = spos;
    if (WideStr)
    {
        WideStr[dpos + 1] = 0x0000;
        WideStr[0] = dpos;
    }
    return dpos;
}

static u16 mbhMulti2Wide(xr_wide_char* WideStr, xr_wide_char* WidePos,
                         u16 WideStrSize, const char* MultiStr)
{
    u16 spos = 0;
    u16 dpos = 0;
    u8 b1, b2, b3;
    xr_wide_char wc = 0;

    if (!MultiStr[0])
        return 0;

    while ((b1 = MultiStr[spos]) != 0x00)
    {
        if (WidePos)
            WidePos[dpos] = spos;
        spos++;

        if ((b1 & BITS1_MASK) == BITS1_EXP)
        {
            wc = b1;
        }
        else if ((b1 & BITS3_MASK) == BITS3_EXP)
        {
            b2 = MultiStr[spos++];
            if (!(b2 && ((b2 & BITS2_MASK) == BITS2_EXP)))
                return mbhMulti2WideDumb(WideStr, WidePos, WideStrSize, MultiStr);
            wc = ((b1 & ~BITS3_MASK) << 6) | (b2 & ~BITS2_MASK);
        }
        else if ((b1 & BITS4_MASK) == BITS4_EXP)
        {
            b2 = MultiStr[spos++];
            if (!(b2 && ((b2 & BITS2_MASK) == BITS2_EXP)))
                return mbhMulti2WideDumb(WideStr, WidePos, WideStrSize, MultiStr);
            b3 = MultiStr[spos++];
            if (!(b3 && ((b3 & BITS2_MASK) == BITS2_EXP)))
                return mbhMulti2WideDumb(WideStr, WidePos, WideStrSize, MultiStr);
            wc = ((b1 & ~BITS4_MASK) << 12) | ((b2 & ~BITS2_MASK) << 6) | (b3 & ~BITS2_MASK);
        }
        else
        {
            return mbhMulti2WideDumb(WideStr, WidePos, WideStrSize, MultiStr);
        }

        dpos++;
        if (WideStr)
            WideStr[dpos] = wc;
    }

    if (WidePos)
        WidePos[dpos] = spos;
    if (WideStr)
    {
        WideStr[dpos + 1] = 0x0000;
        WideStr[0] = dpos;
    }
    return dpos;
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

// --- pure ASCII -------------------------------------------------------------

static void test_ascii_passes_through()
{
    xr_wide_char wide[64] = {};
    xr_wide_char pos[64] = {};
    u16 n = mbhMulti2Wide(wide, pos, 64, "hello");
    CHECK(n == 5);
    CHECK(wide[1] == 'h');
    CHECK(wide[2] == 'e');
    CHECK(wide[3] == 'l');
    CHECK(wide[4] == 'l');
    CHECK(wide[5] == 'o');
    CHECK(pos[0] == 0); // first wide char starts at byte 0
    CHECK(pos[1] == 1); // second at byte 1
    CHECK(pos[5] == 5); // terminator slot points at end
}

// --- 2-byte cyrillic UTF-8 -------------------------------------------------

static void test_cyrillic_utf8_decodes_to_one_wide_per_letter()
{
    // "Привет" — 6 cyrillic chars, 12 UTF-8 bytes.
    const char input[] =
        "\xD0\x9F"  // П  U+041F
        "\xD1\x80"  // р  U+0440
        "\xD0\xB8"  // и  U+0438
        "\xD0\xB2"  // в  U+0432
        "\xD0\xB5"  // е  U+0435
        "\xD1\x82"; // т  U+0442

    xr_wide_char wide[32] = {};
    xr_wide_char pos[32] = {};
    u16 n = mbhMulti2Wide(wide, pos, 32, input);
    CHECK(n == 6);
    CHECK(wide[1] == 0x041F);
    CHECK(wide[2] == 0x0440);
    CHECK(wide[3] == 0x0438);
    CHECK(wide[4] == 0x0432);
    CHECK(wide[5] == 0x0435);
    CHECK(wide[6] == 0x0442);
    // WidePos: wide char i sits at byte i*2.
    CHECK(pos[0] == 0);
    CHECK(pos[1] == 2);
    CHECK(pos[2] == 4);
    CHECK(pos[5] == 10);
}

// --- 3-byte UTF-8 (rest of BMP) --------------------------------------------

static void test_three_byte_utf8_decodes_to_one_wide()
{
    // U+20AC € (Euro sign) — 3 bytes E2 82 AC.
    xr_wide_char wide[16] = {};
    xr_wide_char pos[16] = {};
    u16 n = mbhMulti2Wide(wide, pos, 16, "\xE2\x82\xAC");
    CHECK(n == 1);
    CHECK(wide[1] == 0x20AC);
    CHECK(pos[0] == 0);
    CHECK(pos[1] == 3); // terminator at byte 3
}

// --- mixed ASCII + cyrillic — the autosave-name pattern --------------------

static void test_mixed_ascii_cyrillic()
{
    // "ragnar - П"
    const char input[] = "ragnar - \xD0\x9F";
    xr_wide_char wide[32] = {};
    xr_wide_char pos[32] = {};
    u16 n = mbhMulti2Wide(wide, pos, 32, input);
    CHECK(n == 10);  // 9 ASCII + 1 cyrillic
    CHECK(wide[1] == 'r');
    CHECK(wide[9] == ' ');
    CHECK(wide[10] == 0x041F);
    CHECK(pos[8] == 8);   // space at byte 8
    CHECK(pos[9] == 9);   // П starts at byte 9
    CHECK(pos[10] == 11); // terminator at byte 11 (after 2-byte sequence)
}

// --- cp1251 bytes fed as UTF-8 → DUMB fallback -----------------------------

static void test_cp1251_bytes_trigger_dumb_fallback()
{
    // "прибытие" in cp1251 — 8 high bytes that don't form valid UTF-8
    // because they each start with binary 11... but lack continuation.
    const char input[] = "\xEF\xF0\xE8\xE1\xFB\xF2\xE8\xE5";

    xr_wide_char wide[32] = {};
    xr_wide_char pos[32] = {};
    u16 n = mbhMulti2Wide(wide, pos, 32, input);

    // DUMB fallback: each byte → one wide char, value == byte.
    CHECK(n == 8);
    CHECK(wide[1] == 0xEF);
    CHECK(wide[2] == 0xF0);
    CHECK(wide[3] == 0xE8);
    CHECK(wide[8] == 0xE5);
    // WidePos under DUMB: each wide char position == byte index + 1.
    CHECK(pos[0] == 1);
    CHECK(pos[7] == 8);
}

// --- empty string ----------------------------------------------------------

static void test_empty_string()
{
    xr_wide_char wide[8] = {};
    xr_wide_char pos[8] = {};
    u16 n = mbhMulti2Wide(wide, pos, 8, "");
    CHECK(n == 0);
}

// --- the legacy save name pattern (UTF-8 "Денис Федоров" prefix) -----------

static void test_legacy_save_name_decodes_clean()
{
    // "Денис" = 5 cyrillic chars, 10 UTF-8 bytes.
    const char input[] = "\xD0\x94\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x81";
    xr_wide_char wide[16] = {};
    xr_wide_char pos[16] = {};
    u16 n = mbhMulti2Wide(wide, pos, 16, input);
    CHECK(n == 5);
    CHECK(wide[1] == 0x0414);  // Д
    CHECK(wide[2] == 0x0435);  // е
    CHECK(wide[3] == 0x043D);  // н
    CHECK(wide[4] == 0x0438);  // и
    CHECK(wide[5] == 0x0441);  // с
}

int main()
{
    std::cout << "== mbhMulti2Wide characterization tests ==\n";
    test_ascii_passes_through();
    test_cyrillic_utf8_decodes_to_one_wide_per_letter();
    test_three_byte_utf8_decodes_to_one_wide();
    test_mixed_ascii_cyrillic();
    test_cp1251_bytes_trigger_dumb_fallback();
    test_empty_string();
    test_legacy_save_name_decodes_clean();
    std::cout << "Summary: " << g_passes << " passed, "
              << g_failures << " failed\n";
    return g_failures == 0 ? 0 : 1;
}
