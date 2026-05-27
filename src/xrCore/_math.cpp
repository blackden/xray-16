#include "stdafx.h"

#include <thread>
#include <SDL.h>

#if defined(XR_PLATFORM_APPLE)
#include <mach/mach_time.h>
#endif

// Initialized on startup
XRCORE_API Fmatrix Fidentity;
XRCORE_API CRandom Random;

#if defined(XR_PLATFORM_APPLE)
// Cvar `native_timing` (0/1). 1 = `CPU::QPC()` / `CPU::GetTicks()` route
// through Apple-native `mach_absolute_time`; 0 = legacy SDL2 timing path.
// External linkage on purpose: the console command lives in a different TU
// (`Layers/xrRender/xrRender_console.cpp`) and writes straight into this
// storage via `extern "C" int g_native_timing`. Default 0 in this commit
// (A.4.1) — A.4.2 flips it to 1 after microbench parity is confirmed.
// Definition without `extern "C"` mirrors the A.3 `g_nsEventInputCvar`
// pattern (declared C-linkage in the console TU, defined as plain C++
// global here — the symbol resolves either way for a POD int).
int g_native_timing = 0;

namespace
{
// `mach_timebase_info` is a constant for the lifetime of the process. We
// cache it on first use; init runs single-threaded before any worker
// threads spin up (`_initialize_cpu` happens during xrCore boot), so a
// plain bool guard is sufficient — no `std::call_once` needed.
mach_timebase_info_data_t g_timebase{};
bool g_timebase_inited = false;

void ensure_timebase()
{
    if (!g_timebase_inited)
    {
        mach_timebase_info(&g_timebase);
        g_timebase_inited = true;
    }
}

// Returns nanoseconds since arbitrary epoch (Darwin uptime). NOT including
// suspended time — `mach_absolute_time` semantics. We pick this over
// `mach_continuous_time` because frame-delta math must NOT see an 8h jump
// after wake from overnight sleep. A.1 sleep-recovery (NSWorkspace
// observer in `xrEngine/Engine.cpp`) handles wake side-effects
// independently of timer values.
u64 apple_qpc_ns()
{
    ensure_timebase();
    return mach_absolute_time() * g_timebase.numer / g_timebase.denom;
}

u32 apple_get_ticks_ms()
{
    return u32(apple_qpc_ns() / 1'000'000ull);
}
} // namespace
#endif // XR_PLATFORM_APPLE

namespace CPU
{
XRCORE_API bool HasSSE     = SDL_HasSSE();
XRCORE_API bool HasSSE2    = SDL_HasSSE2();
XRCORE_API bool HasSSE42   = SDL_HasSSE42();

XRCORE_API bool HasAVX     = SDL_HasAVX();

XRCORE_API bool HasAVX2    = SDL_HasAVX2();

XRCORE_API bool HasAVX512F = SDL_HasAVX512F();

// On Apple we lock `qpc_freq` to 1e9 (one tick == one nanosecond) regardless
// of cvar state — `apple_qpc_ns()` already returns nanoseconds, so downstream
// `cycles / qpc_freq = seconds` math stays correct. The legacy SDL path on
// Apple also reads this freq; since SDL_GetPerformanceCounter on Darwin is
// itself layered over `mach_absolute_time` and reports frequency of 1e9
// (verified on macOS 14/15), the constant is consistent across both paths.
XRCORE_API u64 qpc_freq =
#if defined(XR_PLATFORM_APPLE)
    1'000'000'000ull;
#else
    SDL_GetPerformanceFrequency();
#endif

XRCORE_API u32 qpc_counter = 0;

XRCORE_API u64 QPC() noexcept
{
#if defined(XR_PLATFORM_APPLE)
    u64 _dest = g_native_timing ? apple_qpc_ns() : SDL_GetPerformanceCounter();
#else
    u64 _dest = SDL_GetPerformanceCounter();
#endif
    qpc_counter++;
    return _dest;
}

XRCORE_API u32 GetTicks()
{
#if defined(XR_PLATFORM_APPLE)
    return g_native_timing ? apple_get_ticks_ms() : SDL_GetTicks();
#else
    return SDL_GetTicks();
#endif
}
} // namespace CPU

//------------------------------------------------------------------------------------
void _initialize_cpu()
{
    ZoneScoped;

    // General CPU identification
    string256 features{};

    const auto listFeature = [&](pcstr featureName, bool hasFeature)
    {
        if (hasFeature)
        {
            if (!features[0])
                xr_strcpy(features, featureName);
            else
            {
                xr_strcat(features, ", ");
                xr_strcat(features, featureName);
            }
        }
    };

    // x86
    listFeature("RDTSC",   SDL_HasRDTSC());
    listFeature("MMX",     SDL_HasMMX());
    listFeature("3DNow!",  SDL_Has3DNow());
    listFeature("SSE",     SDL_HasSSE());
    listFeature("SSE2",    CPU::HasSSE2);
    listFeature("SSE3",    SDL_HasSSE3());
    listFeature("SSE41",   SDL_HasSSE41());
    listFeature("SSE42",   CPU::HasSSE42);
    listFeature("AVX",     CPU::HasAVX);
    listFeature("AVX2",    CPU::HasAVX2);
    listFeature("AVX512F", CPU::HasAVX512F);

    // Other architectures
    listFeature("AltiVec", SDL_HasAltiVec());
    listFeature("ARMSIMD", SDL_HasARMSIMD());
    listFeature("NEON",    SDL_HasNEON());
#if SDL_VERSION_ATLEAST(2, 24, 0)
    listFeature("LSX",     SDL_HasLSX());
    listFeature("LASX",    SDL_HasLASX());
#endif

    Msg("* CPU features: %s", features);
    Msg("* CPU threads: %d", std::thread::hardware_concurrency());

    Log("");
    Fidentity.identity(); // Identity matrix
    Random.seed(u32(CPU::QPC() % (s64(1) << s32(32))));

    pvInitializeStatics(); // Lookup table for compressed normals

    _initialize_cpu_thread();
}

// per-thread initialization
#if defined(XR_ARCHITECTURE_ARM) || defined(XR_ARCHITECTURE_ARM64) || defined(XR_ARCHITECTURE_PPC64)
#define _MM_SET_FLUSH_ZERO_MODE(mode)
#define _MM_SET_DENORMALS_ZERO_MODE(mode)
#else
#include <xmmintrin.h>
#endif

static BOOL _denormals_are_zero_supported = TRUE;
extern void __cdecl _terminate();

void _initialize_cpu_thread()
{
    xrDebug::OnThreadSpawn();

    if (CPU::HasSSE)
    {
        //_mm_setcsr ( _mm_getcsr() | (_MM_FLUSH_ZERO_ON+_MM_DENORMALS_ZERO_ON) );
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        if (_denormals_are_zero_supported)
        {
#if defined(XR_PLATFORM_WINDOWS)
            __try
            {
                _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                _denormals_are_zero_supported = FALSE;
            }
#else
            try
            {
                _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
            }
            catch (...)
            {
                _denormals_are_zero_supported = FALSE;
            }
#endif
        }

    }
}
