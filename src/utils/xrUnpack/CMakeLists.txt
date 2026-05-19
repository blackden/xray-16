add_executable(xrUnpack)

target_sources(xrUnpack PRIVATE
    xrUnpack.cpp
)

target_include_directories(xrUnpack
    PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(xrUnpack
    PRIVATE
    xrCore
)
