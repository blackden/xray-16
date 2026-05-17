#pragma once

#include "xrDebug.h"
#include "log.h"

#define DEBUG_INFO {__FILE__, __LINE__, __FUNCTION__}
#define CHECK_OR_EXIT(expr, message)\
    do\
    {\
        if (!(expr))\
            xrDebug::DoExit(message);\
    } while (false)
#define R_ASSERT(expr)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr);\
    } while (false)
#define R_ASSERT2(expr, desc)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc);\
    } while (false)
#define R_ASSERT3(expr, desc, arg1)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc, arg1);\
    } while (false)
#define R_ASSERT4(expr, desc, arg1, arg2)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc, arg1, arg2);\
    } while (false)

#define R_CHK(expr)\
    do\
    {\
        static bool ignoreAlways = false;\
        HRESULT hr = expr;\
        if (!ignoreAlways && FAILED(hr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, hr);\
    } while (false)
#define R_CHK2(expr, arg1)\
    do\
    {\
        static bool ignoreAlways = false;\
        HRESULT hr = expr;\
        if (!ignoreAlways && FAILED(hr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, hr, arg1);\
    } while (false)
#define FATAL(desc) xrDebug::Fatal(DEBUG_INFO, "%s", desc)
#define FATAL_F(format, ...) xrDebug::Fatal(DEBUG_INFO, format, __VA_ARGS__)

#ifdef VERIFY
#undef VERIFY
#endif

#ifdef DEBUG
#define NODEFAULT FATAL("nodefault reached")

#define R_ASSERT1_CURE(expr, cure)\
    do\
    {\
        if (!(expr))\
        {\
            static bool ignoreAlways = false;\
            if (!ignoreAlways)\
                xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr);\
            cure;\
        }\
    } while (false)

#define R_ASSERT2_CURE(expr, desc, cure)\
    do\
    {\
        if (!(expr))\
        {\
            static bool ignoreAlways = false;\
            if (!ignoreAlways)\
                xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc);\
            cure;\
        }\
    } while (false)

#define R_ASSERT3_CURE(expr, desc, arg1, cure)\
    do\
    {\
        if (!(expr))\
        {\
            static bool ignoreAlways = false;\
            if (!ignoreAlways)\
                xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc, arg1);\
            cure;\
        }\
    } while (false)

#define R_ASSERT4_CURE(expr, cure, desc, arg1, arg2)\
    do\
    {\
        if (!(expr))\
        {\
            static bool ignoreAlways = false;\
            if (!ignoreAlways)\
                xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc, arg1, arg2);\
            cure;\
        }\
    } while (false)

#define VERIFY(expr)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr);\
    } while (false)
#define VERIFY2(expr, desc)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc);\
    } while (false)
#define VERIFY3(expr, desc, arg1)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc, arg1);\
    } while (false)
#define VERIFY4(expr, desc, arg1, arg2)\
    do\
    {\
        static bool ignoreAlways = false;\
        if (!ignoreAlways && !(expr))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, desc, arg1, arg2);\
    } while (false)
#define CHK_DX(expr)\
    do\
    {\
        static bool ignoreAlways = false;\
        HRESULT hr_ = expr;\
        if (!ignoreAlways && FAILED(hr_))\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, hr_);\
    } while (false)
#if defined(XR_PLATFORM_APPLE)
// On Apple GL (Metal-backed OpenGL 4.1) many operations raise expected errors
// that the engine has no workaround for (compressed 3D textures, certain
// framebuffer attachments, MSAA RT targets marked TODO in code). Falling into
// xrDebug::Fail here also exercises a buffer-overflow path in the formatter
// that triggers a PAC trap inside Cocoa's modal dialog setup. Log and keep
// rendering instead — visual artifacts beat a fatal crash on macOS.
#define CHK_GL(expr)\
    do\
    {\
        expr;\
        GLenum err = glGetError();\
        if (err != GL_NO_ERROR)\
            Msg("! OpenGL: %s -> 0x%X", #expr, (unsigned)err);\
    } while (false)
#else
#define CHK_GL(expr)\
    do\
    {\
        static bool ignoreAlways = false;\
        expr;\
        GLenum err = glGetError();\
        if (!ignoreAlways && err != GL_NO_ERROR)\
            xrDebug::Fail(ignoreAlways, DEBUG_INFO, #expr, (long)err);\
    } while (false)
#endif
#else // DEBUG
#define R_ASSERT1_CURE(expr, cure)\
    do\
    {\
        if (!(expr))\
        {\
            cure;\
        }\
    } while (false)

#define R_ASSERT2_CURE(expr, desc, cure)\
    do\
    {\
        if (!(expr))\
        {\
            cure;\
        }\
    } while (false)

#define R_ASSERT3_CURE(expr, desc, arg1, cure)\
    do\
    {\
        if (!(expr))\
        {\
            cure;\
        }\
    } while (false)

#define R_ASSERT4_CURE(expr, cure, desc, arg1, arg2)\
    do\
    {\
        if (!(expr))\
        {\
            cure;\
        }\
    } while (false)

#define NODEFAULT XR_ASSUME(0)
#define VERIFY(expr) do {} while (false)
#define VERIFY2(expr, desc) do {} while (false)
#define VERIFY3(expr, desc, arg1) do {} while (false)
#define VERIFY4(expr, desc, arg1, arg2) do {} while (false)
#define CHK_DX(expr) expr
#if defined(XR_PLATFORM_APPLE)
// On Apple GL we keep GL error reporting even in MasterGold. Apple's
// Metal-backed GL 4.1 silently mis-handles several operations (compressed
// 3D textures, MSAA framebuffers, certain blits); a no-op CHK_GL hides the
// state where the driver enters its recovery / TX path. Logging is cheap
// and gives us the only post-mortem we have on a hung machine.
#define CHK_GL(expr)\
    do\
    {\
        expr;\
        GLenum err = glGetError();\
        if (err != GL_NO_ERROR)\
            Msg("! OpenGL: %s -> 0x%X", #expr, (unsigned)err);\
    } while (false)
#else
#define CHK_GL(expr) expr
#endif
#endif // DEBUG

#if XRAY_EXCEPTIONS
#define THROW3(expr, msg0, msg1)\
    do\
    {\
        if (!(expr))\
        {\
            string4096 assertionInfo;\
            xrDebug::GatherInfo(assertionInfo, sizeof(assertionInfo), DEBUG_INFO, #expr, msg0, msg1, nullptr);\
            throw assertionInfo;\
        }\
    }\
    while (false)

#define THROW(expr) THROW3(expr, nullptr, nullptr)
#define THROW2(expr, msg0) THROW3(expr, msg0, nullptr)

#else
#define THROW VERIFY
#define THROW2 VERIFY2
#define THROW3 VERIFY3
#endif

//---------------------------------------------------------------------------------------------
// FIXMEs / TODOs / NOTE macros
//---------------------------------------------------------------------------------------------
#define _QUOTE(x) #x
#define QUOTE(x) _QUOTE(x)
#define __FILE__LINE__ __FILE__ "(" QUOTE(__LINE__) ") : "

#define NOTE(x) message(x)
#define FILE_LINE message(__FILE__LINE__)

#define TODO(x) message(__FILE__LINE__"\n"\
    " ------------------------------------------------\n"\
    "| TODO : " #x "\n"\
    " -------------------------------------------------\n")
#define FIXME(x) message(__FILE__LINE__"\n"\
    " ------------------------------------------------\n"\
    "| FIXME : " #x "\n"\
    " -------------------------------------------------\n")
#define todo(x) message(__FILE__LINE__" TODO : " #x "\n")
#define fixme(x) message(__FILE__LINE__" FIXME: " #x "\n")

