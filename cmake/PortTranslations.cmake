# The port's menu translations (translations/<language>.json, see
# platform/include/port_translate.h) built into the executable as byte arrays,
# so that every platform reads them the same way, with no file to copy.

file(GLOB MP_TRANSLATIONS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/translations/*.json")
list(SORT MP_TRANSLATIONS)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MP_TRANSLATIONS})

set(_data "")
set(_table "")
set(_count 0)
foreach(_file IN LISTS MP_TRANSLATIONS)
    get_filename_component(_language "${_file}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${_language}" _name)
    file(READ "${_file}" _hex HEX)
    string(LENGTH "${_hex}" _length)
    math(EXPR _size "${_length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
    string(APPEND _data "const unsigned char k_${_name}[] = {${_bytes}0x00};\n")
    string(APPEND _table "    {\"${_language}\", k_${_name}, ${_size}u},\n")
    math(EXPR _count "${_count} + 1")
endforeach()
if(_count EQUAL 0)
    set(_table "    {\"\", nullptr, 0u},\n")
endif()

set(_source "// Generated from translations/*.json by cmake/PortTranslations.cmake; do not edit.
#include \"port_translate.h\"

namespace port {
namespace {
${_data}} // namespace

const TranslationFile kTranslationFiles[] = {
${_table}};
const unsigned kTranslationFileCount = ${_count}u;

} // namespace port
")
set(MP_TRANSLATIONS_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/port_translations_data.cpp")
set(_previous "")
if(EXISTS "${MP_TRANSLATIONS_SOURCE}")
    file(READ "${MP_TRANSLATIONS_SOURCE}" _previous)
endif()
if(NOT _previous STREQUAL _source)
    file(WRITE "${MP_TRANSLATIONS_SOURCE}" "${_source}")
endif()
target_sources(metroid_prime_port PRIVATE "${MP_TRANSLATIONS_SOURCE}")
