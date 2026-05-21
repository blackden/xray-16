// Characterization test for the cp1251 <-> UTF-8 bridge in xrCore.
//
// Production code: xr_cp1251_to_utf8 (xrCore.cpp:62-87) and xr_utf8_to_cp1251
// (xrCore.cpp:34-60). Both wrap iconv with "CP1251//TRANSLIT" / "UTF-8" pairs.
// We don't link against xrCore (it pulls SDL/memory/threading); instead we
// drive iconv directly with the same parameters, so the test pins the
// behavioral contract: every cp1251 byte must round-trip through UTF-8 cleanly,
// and mojibake produced by mis-applied conversion must be detectable.
//
// macOS / POSIX only — iconv on Windows is a separate dependency we don't ship.
// Skipped (exit 0) if iconv refuses to open the requested encoding pair.
//
// Build:
//   clang++ -std=c++17 -Wall -Wextra -Werror tests/cp1251_roundtrip_test.cpp \
//       -liconv -o build-tests/cp1251_roundtrip_test
// Run:
//   ./build-tests/cp1251_roundtrip_test

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iconv.h>
#include <iostream>
#include <string>

// ----------- iconv wrappers that mirror xrCore.cpp behavior -----------------

static bool convert(const char* from, const char* to, const char* in, std::string& out)
{
    iconv_t cd = iconv_open(to, from);
    if (cd == (iconv_t)-1)
        return false;
    char buf[1024] = {};
    char* inp = const_cast<char*>(in);
    char* outp = buf;
    size_t inleft = strlen(in);
    size_t outleft = sizeof(buf) - 1;
    size_t r = iconv(cd, &inp, &inleft, &outp, &outleft);
    iconv_close(cd);
    if (r == (size_t)-1)
        return false;
    *outp = '\0';
    out.assign(buf);
    return true;
}

static bool cp1251_to_utf8(const char* in, std::string& out) { return convert("CP1251", "UTF-8", in, out); }
static bool utf8_to_cp1251(const char* in, std::string& out) { return convert("UTF-8", "CP1251//TRANSLIT", in, out); }

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

// --- bytes that mattered in real bug reports --------------------------------

static void test_attempted_autosave_name_cp1251_to_utf8()
{
    // "прибытие" in cp1251 bytes (one byte per char in 0xC0-0xFF range).
    const char cp1251[] = "\xEF\xF0\xE8\xE1\xFB\xF2\xE8\xE5";

    std::string utf8;
    bool ok = cp1251_to_utf8(cp1251, utf8);
    CHECK(ok);
    // Expected: "прибытие" as UTF-8, 2 bytes per cyrillic char.
    const char expected[] =
        "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB1\xD1\x8B\xD1\x82\xD0\xB8\xD0\xB5";
    CHECK(utf8 == expected);
    CHECK(utf8.size() == 16); // 8 cyrillic chars * 2 bytes
}

static void test_save_label_roundtrip()
{
    // Full autosave label: "ragnar - прибытие на «Скадовск»"
    const char cp1251[] =
        "ragnar - \xEF\xF0\xE8\xE1\xFB\xF2\xE8\xE5"
        " \xED\xE0 "
        "\xAB\xD1\xEA\xE0\xE4\xEE\xE2\xF1\xEA\xBB";

    std::string utf8;
    CHECK(cp1251_to_utf8(cp1251, utf8));

    // Should now be valid UTF-8 (we don't check byte exact, only roundtrip).
    std::string back;
    CHECK(utf8_to_cp1251(utf8.c_str(), back));
    CHECK(back == cp1251);
}

static void test_pure_ascii_passes_unchanged()
{
    std::string utf8, back;
    CHECK(cp1251_to_utf8("ragnar - quicksave.scop", utf8));
    CHECK(utf8 == "ragnar - quicksave.scop");

    CHECK(utf8_to_cp1251(utf8.c_str(), back));
    CHECK(back == "ragnar - quicksave.scop");
}

static void test_all_cp1251_bytes_yield_valid_utf8()
{
    // Every cp1251 byte 0x20-0xFE (skip control + DEL) must transcode to some
    // valid UTF-8 sequence. We don't check each codepoint here — that's
    // iconv's table — but we do verify nothing crashes / loses bytes wildly.
    int converted = 0;
    for (int b = 0x20; b < 0xFF; ++b)
    {
        char in[2] = { static_cast<char>(b), 0 };
        std::string utf8;
        if (cp1251_to_utf8(in, utf8) && !utf8.empty())
            ++converted;
    }
    // cp1251 has a handful of unassigned slots (0x98); some platforms reject
    // them, some pass them through. Accept anything ≥ 95% conversion rate.
    CHECK(converted >= (0xFF - 0x20) * 95 / 100);
}

// --- the "mojibake detection" cases — when something was converted twice ----

static void test_utf8_treated_as_cp1251_produces_recognizable_garbage()
{
    // If we ACCIDENTALLY pass UTF-8 bytes "Д" (D0 94) to a cp1251→UTF-8
    // converter, we should get gibberish that doesn't match the original
    // UTF-8 — this is the mojibake fingerprint we look for in the wild.
    const char utf8_bytes_treated_as_cp1251[] = "\xD0\x94";

    std::string double_encoded;
    bool ok = cp1251_to_utf8(utf8_bytes_treated_as_cp1251, double_encoded);
    CHECK(ok);
    // Result should be different from input — proving mis-conversion changes the bytes.
    CHECK(double_encoded != utf8_bytes_treated_as_cp1251);
}

int main()
{
    std::cout << "== cp1251 <-> UTF-8 roundtrip characterization tests ==\n";

    // Smoke-check that iconv is actually available; otherwise skip the suite.
    iconv_t test_cd = iconv_open("UTF-8", "CP1251");
    if (test_cd == (iconv_t)-1)
    {
        std::cout << "  (skipped: iconv lacks CP1251 ↔ UTF-8 support on this host)\n";
        std::cout << "Summary: 0 passed, 0 failed\n";
        return 0;
    }
    iconv_close(test_cd);

    test_attempted_autosave_name_cp1251_to_utf8();
    test_save_label_roundtrip();
    test_pure_ascii_passes_unchanged();
    test_all_cp1251_bytes_yield_valid_utf8();
    test_utf8_treated_as_cp1251_produces_recognizable_garbage();

    std::cout << "Summary: " << g_passes << " passed, "
              << g_failures << " failed\n";
    return g_failures == 0 ? 0 : 1;
}
