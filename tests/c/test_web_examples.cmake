# Every example the browser page offers must actually run.
#
# The examples are static CSV embedded in web/guide-content.js, so nothing
# compiles them and nothing else reads them: a mistyped grapheme, a short row
# or a stray comma sits there until a visitor picks that entry from the
# dropdown and gets an error instead of a result. The page is the first thing
# anyone sees, and the examples are the first thing they click.
#
# Two assertions per example. detect has to succeed, and the scan has to report
# no out-of-vocabulary graphemes -- OOV is not fatal to a run, but in a curated
# example it means a transcription the pinned merkmal does not recognise, which
# is a mistake in the data rather than a property of it.

if(NOT DEFINED COGNATOR_CLI)
    message(FATAL_ERROR "COGNATOR_CLI is required")
endif()
if(NOT DEFINED GUIDE_JS)
    message(FATAL_ERROR "GUIDE_JS is required")
endif()
if(NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR is required")
endif()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")
file(READ "${GUIDE_JS}" guide)

# const NAME = `...csv...`;  -- the backtick literal is the example.
string(REGEX MATCHALL "const [A-Z_]+ = `[^`]*`" blocks "${guide}")
list(LENGTH blocks count)
if(count LESS 4)
    message(FATAL_ERROR "found ${count} examples in ${GUIDE_JS}; the regex is wrong")
endif()

foreach(block IN LISTS blocks)
    string(REGEX MATCH "const ([A-Z_]+) = " _ "${block}")
    set(name "${CMAKE_MATCH_1}")
    string(REGEX REPLACE "^const [A-Z_]+ = `" "" csv "${block}")
    string(REGEX REPLACE "`$" "" csv "${csv}")

    set(path "${TEST_WORK_DIR}/${name}.csv")
    file(WRITE "${path}" "${csv}")

    execute_process(
        COMMAND "${COGNATOR_CLI}" detect
            --in "${path}" --out "${TEST_WORK_DIR}/${name}.sidecar.tsv"
            --preset support-adaptive-bridge
        RESULT_VARIABLE detect_result
        ERROR_VARIABLE detect_error
    )
    if(NOT detect_result EQUAL 0)
        message(FATAL_ERROR "${name}: detect returned ${detect_result}: ${detect_error}")
    endif()
    if(NOT detect_error MATCHES "0 OOV types")
        message(FATAL_ERROR
            "${name}: the example contains graphemes the pinned merkmal does not "
            "recognise. Run:\n  cognator oovscan --data ${path}\n${detect_error}")
    endif()

    # Every row scored means every row parsed: a short or malformed row would
    # be dropped by the reader rather than reported.
    string(REGEX MATCHALL "\n" newlines "${csv}")
    list(LENGTH newlines line_count)
    math(EXPR expected "${line_count} - 1")
    if(NOT detect_error MATCHES "${expected} rows")
        message(FATAL_ERROR
            "${name}: expected ${expected} rows from the file, detect reported "
            "otherwise -- a malformed row?\n${detect_error}")
    endif()

    message(STATUS "web example ${name}: ${expected} rows, no OOV")
endforeach()
