# Preserve the actual bytes for scripts/build.ps1. This avoids CMake/Ninja console
# encoding conversions that otherwise silently lose all Chinese MSVC header deps.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles/rchat-dependency-probe")
    file(MAKE_DIRECTORY "${_probe_dir}")
    file(WRITE "${_probe_dir}/rchat_dep_probe.hpp" "// dependency probe\n")
    file(WRITE "${_probe_dir}/probe.cpp" "#include \"rchat_dep_probe.hpp\"\n")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /nologo /utf-8 /showIncludes /EP "${_probe_dir}/probe.cpp"
        OUTPUT_VARIABLE _probe_out ERROR_VARIABLE _probe_err RESULT_VARIABLE _probe_result ENCODING NONE)
    string(REGEX MATCH "(^|[\r\n])([^\r\n]+):[ \t]+[A-Za-z]:[/\\]" _probe_match "${_probe_out}\n${_probe_err}")
    if(_probe_result EQUAL 0 AND _probe_match)
        file(WRITE "${_probe_dir}/prefix.bin" "${CMAKE_MATCH_2}: ")
    else()
        message(FATAL_ERROR "Cannot determine MSVC include prefix; incremental header tracking would be unreliable.")
    endif()
endif()
