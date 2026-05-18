project(xrGamedata)

install(FILES fsgame.ltx DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/openxray)

install(DIRECTORY gamedata DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/openxray)

# Phase 4.2: lock the localization XMLs to UTF-8 on disk. Phase 4.1 already
# transcoded the 6 shipped openxray.xml files; without a guard, a future
# editor with a misconfigured save dialog could quietly write cp1251 back.
# iconv exits non-zero on the first invalid byte, so the loop fails the
# target as soon as any drift appears.
file(GLOB_RECURSE _XRAY_LOCALIZATION_XMLS
    "${CMAKE_CURRENT_SOURCE_DIR}/gamedata/configs/text/*/*.xml")

find_program(ICONV_EXECUTABLE iconv)
if (ICONV_EXECUTABLE AND _XRAY_LOCALIZATION_XMLS)
    set(_XRAY_UTF8_VALIDATE_COMMANDS)
    foreach(_xml IN LISTS _XRAY_LOCALIZATION_XMLS)
        list(APPEND _XRAY_UTF8_VALIDATE_COMMANDS
            COMMAND ${ICONV_EXECUTABLE} -f UTF-8 -t UTF-8 ${_xml} > /dev/null)
    endforeach()
    add_custom_target(validate-localization-utf8
        ${_XRAY_UTF8_VALIDATE_COMMANDS}
        COMMENT "Validating UTF-8 encoding of localization XMLs"
        VERBATIM)
endif()
