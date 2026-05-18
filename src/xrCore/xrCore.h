#pragma once

#define MACRO_TO_STRING_HELPER(a) #a
#define MACRO_TO_STRING(a) MACRO_TO_STRING_HELPER(a)

#define CONCATENIZE_HELPER(a, b) a##b
#define CONCATENIZE(a, b) CONCATENIZE_HELPER(a, b)

// Warnings
#pragma warning(disable : 4127) // conditional expression is constant
#pragma warning(disable : 4345)

#ifdef XR_ARCHITECTURE_X64
#pragma warning(disable : 4512)
#endif

#pragma warning(disable : 4714) // __forceinline not inlined

#ifndef DEBUG
#pragma warning(disable : 4189) // local variable is initialized but not referenced
#endif // frequently in release code due to large amount of VERIFY

// Our headers
#ifdef XRAY_STATIC_BUILD
#   define XRCORE_API
#else
#   ifdef XRCORE_EXPORTS
#      define XRCORE_API XR_EXPORT
#   else
#      define XRCORE_API XR_IMPORT
#      define TRACY_IMPORTS
#   endif
#endif

#include <tracy/Tracy.hpp>

#include "xrDebug.h"
//#include "vector.h"

#include "clsid.h"
//#include "Threading/Lock.hpp"
#include "xrMemory.h"

//#include "_stl_extensions.h"
#include "_std_extensions.h"
#include "_rect.h"
#include "_matrix.h"
#include "xrCommon/xr_vector.h"
#include "xrCommon/xr_set.h"
#include "xrsharedmem.h"
#include "xrstring.h"
#include "xr_resource.h"
#include "Compression/rt_compressor.h"
#include "xr_shared.h"
#include "string_concatenations.h"
#include "_flags.h"

// stl ext
struct XRCORE_API xr_rtoken
{
    shared_str name;
    int id;

    xr_rtoken(pcstr _nm, int _id)
        : name(_nm), id(_id) {}

    void rename(pcstr _nm) { name = _nm; }
    bool equal(pcstr _nm) const { return (0 == xr_strcmp(name.c_str(), _nm)); }
};

#include "xr_shortcut.h"

#include "FS.h"
#include "log.h"
#include "xr_trims.h"
#include "xr_ini.h"
#ifdef NO_FS_SCAN
#include "ELocatorAPI.h"
#else
#include "LocatorAPI.h"
#endif
#include "FileSystem.h"
#include "FTimer.h"
#include "fastdelegate.h"
#ifdef XR_PLATFORM_WINDOWS
#include "intrusive_ptr.h"
#endif

#include "net_utils.h"
#include "Threading/ThreadUtil.h"

// destructor
template <class T>
class destructor
{
    T* ptr;

public:
    destructor(T* p) { ptr = p; }
    ~destructor() { xr_delete(ptr); }
    T& operator()() { return *ptr; }
};

// ***** The Core definition *****
class XRCORE_API xrCore
{
    u32 buildId;
    static const pcstr buildDate;
    static const pcstr buildCommit;
    static const pcstr buildBranch;

public:
    xrCore();

    string64 ApplicationName;
    string64 ApplicationTitle;
    string_path ApplicationPath;
    string_path WorkingPath;
    string64 UserName;
    string64 CompName;
    char* Params;
    u32 dwFrame;
    bool PluginMode;

    void Initialize(
        pcstr ApplicationName, pcstr commandLine = nullptr, bool init_fs = true, pcstr fs_fname = nullptr, bool plugin = false);
    void _destroy();

    u32 GetBuildId() const { return buildId; }
    static pcstr GetBuildDate() { return buildDate; }
    static pcstr GetBuildCommit() { return buildCommit; }
    static pcstr GetBuildBranch() { return buildBranch; }

private:
    void CalculateBuildId();
    void PrintBuildInfo();
};

extern XRCORE_API xrCore Core;

// Convert a UTF-8 string in-place to cp1251. Used to bridge POSIX UTF-8
// strings (filesystem, getpwuid) into the engine's cp1251 font tables for
// display. On Windows this is a no-op since OS APIs already return cp1251 in
// ANSI mode. Characters outside cp1251 are transliterated (//TRANSLIT). On
// failure the buffer is left unchanged.
XRCORE_API void xr_utf8_to_cp1251(char* buf, size_t buf_size);

// Convert a cp1251 string in-place to UTF-8. The inverse of the above; used
// to bridge engine-internal cp1251 strings (localization XML, Lua) into APIs
// that require UTF-8 (POSIX filesystem on macOS). No-op on Windows.
XRCORE_API void xr_cp1251_to_utf8(char* buf, size_t buf_size);

// Generic legacy -> UTF-8 in-place. `codepage` is an iconv name, e.g.
// "CP1251" / "CP1250" / "CP1252". Used by the XML/INI read shim (Phase 2)
// which needs to swap between cp1251 for rus/ukr and cp1250 for pol. The
// in-place variant is intended for fixed-size buffers (filenames); use
// xr_legacy_to_utf8_alloc for file bodies that may inflate past `buf_size`.
XRCORE_API void xr_legacy_to_utf8(char* buf, size_t buf_size, pcstr codepage);

// Allocating variant. Reads `src_len` bytes from `src` interpreted as
// `codepage` and writes the UTF-8 result into `out`. Returns false (and
// leaves `out` empty) on iconv failure. UTF-8 size headroom is 4x the
// source length; cp1251 -> UTF-8 caps at 3x so this is safe.
XRCORE_API bool xr_legacy_to_utf8_alloc(pcstr src, size_t src_len, pcstr codepage, xr_string& out);

// Diagnostic flag, exposed as the `r__trace_encoding` console var. When
// non-zero, every XML / INI file the Phase 2 read shim has to transcode
// from cp1251/cp1250 logs one Msg() line with its name -- useful when
// debugging mod packs to confirm what's legacy-encoded and what's
// already UTF-8. Default off; toggling it costs nothing at the call
// site (one branch).
XRCORE_API extern int g_r__trace_encoding;

// Master switch for the Phase 2 read shim. Default ON because vanilla
// CoP gamedata.db archives still ship cp1251 XMLs and the shim is the
// only thing keeping the menu / dialogs readable. Modders shipping a
// fully UTF-8-native pack can set this to 0 to skip the per-line
// xr_is_valid_utf8 check; cost when on is one branch + a strlen-style
// scan per load, so the saving is small. Exposed as r__legacy_encoding.
XRCORE_API extern int g_r__legacy_encoding;

// Returns true if the given byte sequence is well-formed UTF-8 (or ASCII,
// which is a subset). Used to decide whether a filename came from a UTF-8
// source (POSIX dir-scan, user-typed text) and should be left alone, or
// from a cp1251 source (script localization) and needs conversion before
// hitting a UTF-8-only FS like APFS.
XRCORE_API bool xr_is_valid_utf8(pcstr buf);

// Windows-1251 -> Unicode codepoint lookup table. Indexed by the cp1251
// byte value (0..255). All cp1251 codepoints lie inside the BMP so u16 is
// enough. 0xFFFD marks the single unassigned byte (0x98).
//
// Used by the renderer (Phase 1) to build codepoint -> atlas-slot maps for
// single-byte fonts whose ini sections were authored as cp1251 indices,
// and reusable by Phase 2 INI/XML shims that need an in-process cp1251
// decoder without iconv overhead.
extern XRCORE_API const u16 xr_cp1251_to_unicode[256];
