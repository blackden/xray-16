#pragma once

#if defined(XR_PLATFORM_LINUX) || defined(XR_PLATFORM_BSD) || defined(XR_PLATFORM_APPLE)
// XXX: remove or cleanup
#define EXCEPTION_STACK_OVERFLOW ((u32) 0xC00000FD)
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0

int _cdecl XRCORE_API _resetstkoflw(void);
#endif

// Tries to match C11 strcpy_s behaviour
// First source string pointer is allowed to be equal to dest string pointer,
// behaviour for other cases of string overlapping is undefined.
template <typename... Args>
pstr strconcat(size_t dest_sz, const pstr dest, const Args... sources)
{
    R_ASSERT1_CURE(dest, return nullptr);

    R_ASSERT1_CURE(dest_sz > 0 && dest_sz < RSIZE_MAX,
    {
        dest[0] = '\0';
        return dest;
    });

    cpcstr dest_end = dest + dest_sz - 1;
    pstr current = dest;

    for (pcstr source : { static_cast<cpcstr>(sources)... })
    {
        R_ASSERT1_CURE(source,
        {
            dest[0] = '\0';
            return dest;
        });

        while (*source)
        {
            R_ASSERT1_CURE(current != dest_end,
            {
                dest[0] = '\0';
                return dest;
            });
            *current = *source;
            ++current;
            ++source;
        }
    }

    *current = '\0';
    return dest;
}

template <size_t Size, typename... Args>
pstr strconcat(char (&dest)[Size], const Args... sources)
{
    strconcat(Size, dest, std::forward<const Args>(sources)...);
    return dest;
}

// warning: do not comment this macro, as stack overflow check is very light
// (consumes ~1% performance of STRCONCAT macro)
#define STRCONCAT_STACKOVERFLOW_CHECK
#ifdef STRCONCAT_STACKOVERFLOW_CHECK

#define STRCONCAT(dest, ...)                                                \
    do                                                                      \
    {                                                                       \
        xray::core::detail::string_tupples tupplesUniqueId(__VA_ARGS__);    \
        size_t bufferSize = tupplesUniqueId.size();                         \
        xray::core::detail::check_stack_overflow(bufferSize);               \
        (dest) = static_cast<pstr>(xr_alloca(bufferSize));                  \
        tupplesUniqueId.concat(dest);                                       \
    } while (0)

#else //#ifdef STRCONCAT_STACKOVERFLOW_CHECK

#define STRCONCAT(dest, ...)                                                \
    do                                                                      \
    {                                                                       \
        xray::core::detail::string_tupples tupplesUniqueId(__VA_ARGS__);    \
        (dest) = static_cast<pstr>(xr_alloca(tupplesUniqueId.size()));      \
        tupplesUniqueId.concat(dest);                                       \
    } while (0)

#endif //#ifdef STRCONCAT_STACKOVERFLOW_CHECK

#include "string_concatenations_inline.h"

