// Shader hot-reload watcher (macOS FSEvents). Renderer playground v2.
//
// Watches $game_shaders$ recursively. On any file change, captures the path
// into a ring buffer the Hot Reload tab reads on each draw. We do NOT
// auto-recompile in-session yet — that requires rewriting half of
// CRender::shader_compile to swap GLuint program objects atomically. For
// now the UI surface is enough: the user sees which files were touched,
// optionally clears the disk shader cache (so the next launch picks up
// the changes), and restarts the engine. Live in-session swap stays as
// a follow-up commit; FSEvents infrastructure here is reusable.
//
// FSEvents threading: we set NoDefer + UseCFRunLoop with the main run
// loop, but CFRunLoop isn't pumped by SDL's event loop on its own. We
// avoid the issue by polling: FSEventStreamFlushSync() inside the
// playground's on_tool_frame() forces any pending events to fire on the
// main thread. No callback marshalling required.

#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

extern "C"
{

constexpr unsigned kHotReloadRingCapacity = 64;

struct HotReloadEntry
{
    char     path[512];
    uint64_t timestamp; // seconds since epoch
};

struct HotReloadRingC
{
    HotReloadEntry entries[kHotReloadRingCapacity];
    unsigned       head{};
    unsigned       total{};
};

static HotReloadRingC s_ring{};
static FSEventStreamRef s_stream = nullptr;

unsigned RendererPlayground_HotReload_Total(void)
{
    return s_ring.total;
}

unsigned RendererPlayground_HotReload_Capacity(void)
{
    return kHotReloadRingCapacity;
}

void RendererPlayground_HotReload_Entry(unsigned i, const char** outPath, unsigned long long* outTs)
{
    const unsigned count = s_ring.total < kHotReloadRingCapacity ? s_ring.total : kHotReloadRingCapacity;
    if (i >= count)
    {
        if (outPath) *outPath = "";
        if (outTs)   *outTs   = 0;
        return;
    }
    const unsigned start = (s_ring.head + kHotReloadRingCapacity - count) % kHotReloadRingCapacity;
    const unsigned idx   = (start + i) % kHotReloadRingCapacity;
    if (outPath) *outPath = s_ring.entries[idx].path;
    if (outTs)   *outTs   = s_ring.entries[idx].timestamp;
}

void RendererPlayground_HotReload_Clear(void)
{
    s_ring.head  = 0;
    s_ring.total = 0;
}

static void HotReloadCallback(ConstFSEventStreamRef /*stream*/,
                              void* /*userData*/,
                              size_t numEvents,
                              void* eventPaths,
                              const FSEventStreamEventFlags* /*flags*/,
                              const FSEventStreamEventId* /*ids*/)
{
    char** paths = (char**)eventPaths;
    const uint64_t now = (uint64_t)time(nullptr);
    for (size_t i = 0; i < numEvents; ++i)
    {
        const unsigned idx = s_ring.head % kHotReloadRingCapacity;
        strncpy(s_ring.entries[idx].path, paths[i], sizeof(s_ring.entries[idx].path) - 1);
        s_ring.entries[idx].path[sizeof(s_ring.entries[idx].path) - 1] = '\0';
        s_ring.entries[idx].timestamp = now;
        ++s_ring.head;
        ++s_ring.total;
    }
}

// Returns true if the stream was started fresh, false if it was already
// running (idempotent). watchPath should be an absolute, slash-normalized
// directory; engine paths are backslashed so the caller (xrEngine .cpp)
// converts first.
bool RendererPlayground_HotReload_Start(const char* watchPath)
{
    if (s_stream || !watchPath || !watchPath[0])
        return false;

    @autoreleasepool
    {
        NSString* nsPath = [NSString stringWithUTF8String:watchPath];
        NSArray* pathsToWatch = @[nsPath];

        FSEventStreamContext ctx{};
        s_stream = FSEventStreamCreate(kCFAllocatorDefault,
                                        &HotReloadCallback,
                                        &ctx,
                                        (__bridge CFArrayRef)pathsToWatch,
                                        kFSEventStreamEventIdSinceNow,
                                        0.5, // latency seconds
                                        kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
        if (!s_stream)
            return false;

        FSEventStreamScheduleWithRunLoop(s_stream, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
        FSEventStreamStart(s_stream);
        return true;
    }
}

void RendererPlayground_HotReload_Poll(void)
{
    if (s_stream)
        FSEventStreamFlushSync(s_stream);
}

void RendererPlayground_HotReload_Stop(void)
{
    if (!s_stream)
        return;
    FSEventStreamStop(s_stream);
    FSEventStreamInvalidate(s_stream);
    FSEventStreamRelease(s_stream);
    s_stream = nullptr;
}

} // extern "C"
