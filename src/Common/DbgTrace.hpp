// DbgTrace — gated debug-trace macro family for non-shipping diagnostics.
//
// Single source of truth for category-tagged debug traces. Three flavours:
//
//   DBG_TRACE(cat, fmt, ...)      — Msg-route (engine log + level filter)
//   DBG_TRACE_POST(cat, fmt, ...) — POSTLOG_MARK-route (stderr, post-CloseLog)
//   DBG_ONCE(cat, fmt, ...)       — Msg-route, fires exactly once per process
//
// Gates:
//   - Compile-time: all macros expand to `((void)0)` under MASTER_GOLD, so
//     shipped builds carry zero runtime cost and no leaked format strings.
//   - Runtime: `g_dbg_mask` (cvar `dbg_mask`) is an int bitmask. A probe
//     fires only when its category bit is set. Default mask is 0 in every
//     config — opt-in per session via `dbg_on <cat>` / `dbg_off <cat>`.
//
// Categories are a fixed enum (compile-time opaque set). Sub-categorization
// is expressed via the format string prefix (e.g. "[6/7] PRE ..."), not by
// adding more enum bits — avoids per-feature macro explosion.
//
// Boundary vs POSTLOG_MARK:
//   POSTLOG_MARK is always-on instrumentation for production-visible
//   regression markers (atexit, dead-flag canaries). DBG_TRACE_POST is the
//   debug-only sibling — same stderr fallback, but masked + stripped in
//   MasterGold. Use POSTLOG_MARK for ship-grade canaries, DBG_TRACE_POST
//   for one-off shutdown investigations.
//
// House convention (mirrors PostLogMark): variadic macros require at least
// one format argument; do NOT rely on GNU ##__VA_ARGS__ — pass a literal
// "" if the message has no substitutions.
//
// See notes/conventions/debug-tracing.md for usage policy + lifecycle.

#pragma once

#include <atomic>

#include "xrCore/log.h"
#include "PostLogMark.hpp"

enum DbgCategory : int
{
    DBG_CAT_INPUT     = 0x01,
    DBG_CAT_RENDER    = 0x02,
    DBG_CAT_LIFECYCLE = 0x04,
    DBG_CAT_FS        = 0x08,
    DBG_CAT_ALIFE     = 0x10,
    DBG_CAT_AUDIO     = 0x20,
    DBG_CAT_NET       = 0x40,
    DBG_CAT_ALL       = 0x7F,
};

// g_dbg_mask lives in xrCore (log.cpp), bound to cvar `dbg_mask` registered
// from xrEngine (xr_ioc_cmd.cpp). XRCORE_API linkage so every module that
// includes this header — xrGame, xrUICore, xrRender, .mm shims, future
// xrCore consumers — can read the mask without extra extern juggling.
extern XRCORE_API int g_dbg_mask;

#if defined(MASTER_GOLD)

#define DBG_TRACE(cat, fmt, ...)      ((void)0)
#define DBG_TRACE_POST(cat, fmt, ...) ((void)0)
#define DBG_ONCE(cat, fmt, ...)       ((void)0)

#else

#define DBG_TRACE(cat, fmt, ...)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (g_dbg_mask & (cat))                                                                                        \
            Msg("[DBG] " fmt, __VA_ARGS__);                                                                            \
    } while (0)

#define DBG_TRACE_POST(cat, fmt, ...)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (g_dbg_mask & (cat))                                                                                        \
            POSTLOG_MARK_FMT("dbg " fmt, __VA_ARGS__);                                                                 \
    } while (0)

#define DBG_ONCE(cat, fmt, ...)                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        static std::atomic<bool> _dbg_fired{false};                                                                    \
        bool _dbg_expected = false;                                                                                    \
        if ((g_dbg_mask & (cat)) && _dbg_fired.compare_exchange_strong(_dbg_expected, true))                           \
            Msg("[DBG/ONCE] " fmt, __VA_ARGS__);                                                                       \
    } while (0)

#endif
