// POSTLOG_MARK — stderr instrumentation marker for phases where the engine
// log is unavailable (after Core._destroy / CloseLog) or where xrCore
// itself is being torn down. Pure C++/POSIX, no xrCore dependencies — safe
// to include from any .cpp/.mm/.h, including static destructors that run
// after main() returns and standard library globals.
//
// Output format: `==> postlog@<steady_clock_ms>: <tag>\n` to STDERR_FILENO.
// On macOS the launcher's `>> openxray.log 2>&1` redirect captures this
// to ~/Library/Logs/OpenXRay/openxray.log; grep `==> postlog@` to extract.
//
// Use cases:
//   - C++ static destructors (engine log already closed)
//   - ~CApplication tail after Core._destroy()
//   - SDL_Quit / xrDebug::Finalize / atexit / dyld finalize phases
//   - Anywhere stderr is the only viable channel
//
// Don't use this when Msg/Log are working — Msg integrates with the log
// timestamp + level filter + remote-debug visibility. POSTLOG_MARK is
// strictly the post-log-close fallback.

#pragma once

#include <chrono>
#include <cstdio>
#include <unistd.h>

inline void postlog_mark(const char* tag)
{
    const auto t = std::chrono::steady_clock::now().time_since_epoch();
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
    char buf[192];
    const int n = std::snprintf(buf, sizeof buf, "==> postlog@%lldms: %s\n", ms, tag);
    if (n > 0)
        ::write(STDERR_FILENO, buf, static_cast<size_t>(n));
}

#define POSTLOG_MARK(tag) postlog_mark(tag)
