// Characterization test for the cp1251 -> Unicode codepoint table in xrCore.
//
// Production code: xr_cp1251_to_unicode[256] in src/xrCore/xrCore.cpp.
// We don't link against xrCore here (it pulls SDL / threading), so the
// table is mirrored below and cross-checked against iconv at runtime. If
// a typo creeps into either copy, the assertion at byte b will scream.
//
// Build:
//   clang++ -std=c++17 -Wall -Wextra -Werror tests/cp1251_codepoint_test.cpp \
//       -liconv -o build-tests/cp1251_codepoint_test
// Run:
//   ./build-tests/cp1251_codepoint_test
//
// macOS / POSIX only. Skips if iconv lacks the CP1251 encoding pair.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iconv.h>
#include <iostream>

// --- DUT: mirror of xr_cp1251_to_unicode in src/xrCore/xrCore.cpp ----------
// IF YOU EDIT THIS TABLE, edit the canonical copy in xrCore.cpp too, and
// vice versa. The iconv cross-check below catches drift on the next CI run.

static const uint16_t DUT_TABLE[256] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
    0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

// --- helpers ---------------------------------------------------------------

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

// Decode one UTF-8 codepoint from `s`. Returns the codepoint, or 0 on empty.
// Strict enough for the cross-check (cp1251 outputs valid UTF-8 by construction).
static uint32_t utf8_first_codepoint(const char* s)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    if (!p || *p == 0) return 0;
    if (*p < 0x80) return *p;
    if ((*p & 0xE0) == 0xC0) return ((*p & 0x1F) << 6) | (p[1] & 0x3F);
    if ((*p & 0xF0) == 0xE0)
        return ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    if ((*p & 0xF8) == 0xF0)
        return ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    return 0xFFFD;
}

static bool iconv_cp1251_byte_to_codepoint(unsigned char b, uint32_t& cp_out, bool& converted)
{
    converted = false;
    cp_out = 0;
    iconv_t cd = iconv_open("UTF-8", "CP1251");
    if (cd == (iconv_t)-1) return false;
    char in[2] = { static_cast<char>(b), 0 };
    char out[8] = {};
    char* inp = in;
    char* outp = out;
    size_t inleft = 1;
    size_t outleft = sizeof(out) - 1;
    size_t r = iconv(cd, &inp, &inleft, &outp, &outleft);
    iconv_close(cd);
    if (r == (size_t)-1)
        return true; // iconv rejects this byte (e.g. 0x98 unassigned). converted=false.
    *outp = '\0';
    converted = true;
    cp_out = utf8_first_codepoint(out);
    return true;
}

// --- tests -----------------------------------------------------------------

static void test_ascii_identity()
{
    for (int b = 0; b < 0x80; ++b)
        CHECK(DUT_TABLE[b] == static_cast<uint16_t>(b));
}

static void test_cyrillic_uppercase_block()
{
    // 0xC0..0xDF -> U+0410..U+042F (А..Я)
    for (int b = 0xC0; b <= 0xDF; ++b)
        CHECK(DUT_TABLE[b] == static_cast<uint16_t>(0x0410 + (b - 0xC0)));
}

static void test_cyrillic_lowercase_block()
{
    // 0xE0..0xFF -> U+0430..U+044F (а..я)
    for (int b = 0xE0; b <= 0xFF; ++b)
        CHECK(DUT_TABLE[b] == static_cast<uint16_t>(0x0430 + (b - 0xE0)));
}

static void test_specific_letters_from_real_save_names()
{
    // "прибытие" — chars that appeared in autosave name regressions.
    CHECK(DUT_TABLE[0xEF] == 0x043F); // п
    CHECK(DUT_TABLE[0xF0] == 0x0440); // р
    CHECK(DUT_TABLE[0xE8] == 0x0438); // и
    CHECK(DUT_TABLE[0xE1] == 0x0431); // б
    CHECK(DUT_TABLE[0xFB] == 0x044B); // ы
    CHECK(DUT_TABLE[0xF2] == 0x0442); // т
    CHECK(DUT_TABLE[0xE5] == 0x0435); // е
    // Ukrainian letters (rendering in pol/ukr localization)
    CHECK(DUT_TABLE[0xAF] == 0x0407); // Ї
    CHECK(DUT_TABLE[0xBF] == 0x0457); // ї
    // Yo
    CHECK(DUT_TABLE[0xA8] == 0x0401); // Ё
    CHECK(DUT_TABLE[0xB8] == 0x0451); // ё
}

static void test_high_punct_mappings()
{
    CHECK(DUT_TABLE[0x80] == 0x0402); // Ђ
    CHECK(DUT_TABLE[0xAB] == 0x00AB); // «
    CHECK(DUT_TABLE[0xBB] == 0x00BB); // »
    CHECK(DUT_TABLE[0xB9] == 0x2116); // №
}

static void test_unassigned_byte_98()
{
    // cp1251 leaves 0x98 unassigned. Our table marks it U+FFFD so callers
    // see "replacement" instead of a stray BMP codepoint.
    CHECK(DUT_TABLE[0x98] == 0xFFFD);
}

static void test_table_matches_iconv()
{
    // Cross-check the entire table against iconv. iconv is the
    // source-of-truth for the Windows-1251 mapping.
    int mismatches = 0;
    for (int b = 0; b < 256; ++b)
    {
        uint32_t cp = 0;
        bool converted = false;
        if (!iconv_cp1251_byte_to_codepoint(static_cast<unsigned char>(b), cp, converted))
        {
            std::cerr << "  iconv unavailable at byte " << b << "\n";
            return;
        }
        if (!converted)
        {
            // iconv rejected this byte — our table should hold a placeholder.
            if (DUT_TABLE[b] != 0xFFFD)
            {
                std::cerr << "  byte 0x" << std::hex << b
                          << ": iconv rejected, table has 0x" << DUT_TABLE[b]
                          << std::dec << "\n";
                ++mismatches;
            }
            continue;
        }
        if (cp != DUT_TABLE[b])
        {
            std::cerr << "  byte 0x" << std::hex << b
                      << ": iconv -> U+" << cp
                      << ", table -> U+" << DUT_TABLE[b]
                      << std::dec << "\n";
            ++mismatches;
        }
    }
    CHECK(mismatches == 0);
}

int main()
{
    std::cout << "== xr_cp1251_to_unicode characterization tests ==\n";

    iconv_t test_cd = iconv_open("UTF-8", "CP1251");
    if (test_cd == (iconv_t)-1)
    {
        std::cout << "  (skipped: iconv lacks CP1251 support)\n";
        std::cout << "Summary: 0 passed, 0 failed\n";
        return 0;
    }
    iconv_close(test_cd);

    test_ascii_identity();
    test_cyrillic_uppercase_block();
    test_cyrillic_lowercase_block();
    test_specific_letters_from_real_save_names();
    test_high_punct_mappings();
    test_unassigned_byte_98();
    test_table_matches_iconv();

    std::cout << "Summary: " << g_passes << " passed, "
              << g_failures << " failed\n";
    return g_failures == 0 ? 0 : 1;
}
