add_executable(xrCompress)

target_sources(xrCompress PRIVATE
    main.cpp
    xrCompress.cpp
    xrCompressDifference.cpp
)

target_include_directories(xrCompress
    PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(xrCompress
    PRIVATE
    xrCore
    LZO::LZO
)
