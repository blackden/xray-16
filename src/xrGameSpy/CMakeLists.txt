add_library(xrGameSpy)

target_sources_grouped(
    TARGET xrGameSpy
    NAME "Kernel"
    FILES
    stdafx.cpp
    stdafx.h
    xrGameSpy.cpp
    xrGameSpy.h
    xrGameSpy_MainDefs.h
)

target_sources_grouped(
    TARGET xrGameSpy
    NAME "Wrapper"
    FILES
    GameSpy_ATLAS.cpp
    GameSpy_ATLAS.h
    GameSpy_Available.cpp
    GameSpy_Available.h
    GameSpy_Browser.cpp
    GameSpy_Browser.h
    GameSpy_BrowsersWrapper.cpp
    GameSpy_BrowsersWrapper.h
    GameSpy_Full.cpp
    GameSpy_Full.h
    GameSpy_GCD_Client.cpp
    GameSpy_GCD_Client.h
    GameSpy_GCD_Server.cpp
    GameSpy_GCD_Server.h
    GameSpy_GP.cpp
    GameSpy_GP.h
    GameSpy_HTTP.cpp
    GameSpy_HTTP.h
    GameSpy_Keys.h
    GameSpy_QR2.cpp
    GameSpy_QR2.h
)

target_include_directories(xrGameSpy
    PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/xrServerEntities"
    "${CMAKE_SOURCE_DIR}/Externals/GameSpy/src"
    "${CMAKE_SOURCE_DIR}/Externals/ode/include"
    "${CMAKE_SOURCE_DIR}/Externals/OpenAutomate/inc"
)

target_link_libraries(xrGameSpy
    PRIVATE
    xrCore
    xrMiscMath
    GameSpy-oxr
)

target_compile_definitions(xrGameSpy
    PRIVATE
    XRGAMESPY_EXPORTS
    XRAY_DISABLE_GAMESPY_WARNINGS
)

set_target_properties(xrGameSpy PROPERTIES
    PREFIX ""
)

target_precompile_headers(xrGameSpy
    PRIVATE
    stdafx.h
)

if(APPLE)
    target_sources_grouped(
        TARGET xrGameSpy
        NAME "Platform\\macOS"
        FILES
        ghttp_worker_apple.mm
    )
    set_source_files_properties(
        ghttp_worker_apple.mm
        PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
        COMPILE_FLAGS "-fobjc-arc"
    )
    # -fobjc-arc on the .mm pulls in libobjc runtime symbols
    # (objc_storeStrong / objc_autoreleasePool*). In BUILD_SHARED_LIBS
    # mode xrGameSpy.dylib must resolve these standalone; Foundation
    # brings libobjc transitively, matching the pattern in xrEngine.
    #
    # Apple-only: xrEngine on the link list so GameSpy_HTTP.cpp can resolve
    # OpenXRay_RegisterGhttpDrainHook (defined in src/xrEngine/Engine.cpp
    # under XR_PLATFORM_APPLE). The dep direction (xrGameSpy → xrEngine)
    # is unidirectional — xrEngine does not link xrGameSpy, and the
    # function's only purpose is xrGameSpy registering its drain function
    # with the per-frame macOS hook aggregator owned by xrEngine. Static
    # builds dedup this via xr_3da's link order; BUILD_SHARED_LIBS needs
    # the explicit edge so xrGameSpy.dylib resolves standalone. See
    # GameSpy_HTTP.cpp's "Defined in src/xrEngine/Engine.cpp" comment for
    # the call-site rationale.
    target_link_libraries(xrGameSpy
        PRIVATE
        xrEngine
        "-framework Foundation"
    )
endif()

if (BUILD_SHARED_LIBS)
    install(TARGETS xrGameSpy LIBRARY
        DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
endif()
