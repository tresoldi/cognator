# Every control the browser page offers must be one the engine reads.
#
# The page builds its request from KNOB_IDS in web/app.js; the engine reads
# whatever apply_options in web/cognator_wasm.c names. Nothing connected the
# two, and they drifted until twenty-seven of the thirty-six controls on the
# published page did nothing: the request carried them and the parser ignored
# them, so a visitor could set the graph policy or the scorer, press Run and
# get byte-identical output back. No test could see it, because both halves
# were individually correct.
#
# Deliberately a text comparison rather than a behavioural one. A knob that is
# parsed but has no effect is a different bug and one the C tests can catch;
# this catches the case where the two files simply stopped agreeing on what
# exists, which is cheap to check and was expensive to miss.

if(NOT DEFINED APP_JS)
    message(FATAL_ERROR "APP_JS is required")
endif()
if(NOT DEFINED SHIM_C)
    message(FATAL_ERROR "SHIM_C is required")
endif()

file(READ "${APP_JS}" app_js)
file(READ "${SHIM_C}" shim_c)

string(REGEX MATCH "KNOB_IDS[ \t\r\n]*=[ \t\r\n]*\\[([^]]*)\\]" _ "${app_js}")
set(knob_block "${CMAKE_MATCH_1}")
if(knob_block STREQUAL "")
    message(FATAL_ERROR "could not find KNOB_IDS in ${APP_JS}")
endif()
string(REGEX MATCHALL "'([^']+)'" knob_matches "${knob_block}")

# The page sends these two outside KNOB_IDS, so check them by name.
set(knobs "preset" "seed")
foreach(match IN LISTS knob_matches)
    string(REGEX REPLACE "'" "" knob "${match}")
    list(APPEND knobs "${knob}")
endforeach()
list(REMOVE_DUPLICATES knobs)

list(LENGTH knobs knob_count)
if(knob_count LESS 30)
    message(FATAL_ERROR "only ${knob_count} knobs parsed from ${APP_JS}; the regex is wrong")
endif()

# apply_options only -- a name appearing elsewhere in the shim, in the result
# writer for instance, is not evidence that the option is read.
string(FIND "${shim_c}" "static const char *apply_options" start)
string(FIND "${shim_c}" "static char *error_json" stop)
if(start EQUAL -1 OR stop EQUAL -1 OR NOT stop GREATER start)
    message(FATAL_ERROR "could not isolate apply_options in ${SHIM_C}")
endif()
math(EXPR length "${stop} - ${start}")
string(SUBSTRING "${shim_c}" ${start} ${length} parser)

set(unread "")
foreach(knob IN LISTS knobs)
    string(FIND "${parser}" "\"${knob}\"" at)
    if(at EQUAL -1)
        list(APPEND unread "${knob}")
    endif()
endforeach()

if(unread)
    string(REPLACE ";" "\n  " pretty "${unread}")
    message(FATAL_ERROR
        "the page offers controls the engine never reads:\n  ${pretty}\n"
        "Add them to apply_options in web/cognator_wasm.c, or remove the "
        "controls from web/index.html and KNOB_IDS in web/app.js. A control "
        "that changes nothing is worse than one that is absent.")
endif()

message(STATUS "wasm options: ${knob_count} knobs, all read by apply_options")
