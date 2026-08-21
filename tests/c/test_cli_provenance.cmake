# The provenance the binary reports must be the pin the tree actually carries.
#
# The C API cannot check this itself: it returns whatever the build compiled
# in, so a test written against the library would assert a string against
# itself and pass on any value at all -- including the empty one this guards
# against. The comparison has to come from outside, and PINNED is the outside.

if(NOT DEFINED COGNATOR_CLI)
    message(FATAL_ERROR "COGNATOR_CLI is required")
endif()
if(NOT DEFINED PINNED_FILE)
    message(FATAL_ERROR "PINNED_FILE is required")
endif()

file(READ "${PINNED_FILE}" pinned)
string(REGEX MATCH "version ([^\n]+)" _m "${pinned}")
set(expected_version "${CMAKE_MATCH_1}")
string(REGEX MATCH "commit ([0-9a-f]+)" _m "${pinned}")
string(SUBSTRING "${CMAKE_MATCH_1}" 0 12 expected_commit)

if(expected_version STREQUAL "" OR expected_commit STREQUAL "")
    message(FATAL_ERROR "${PINNED_FILE} names no version and commit")
endif()

execute_process(
    COMMAND "${COGNATOR_CLI}" version
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "version returned ${result}: ${error}")
endif()

if(NOT output MATCHES "merkmal ${expected_version} \\(${expected_commit}\\)")
    message(FATAL_ERROR
        "version reports a different merkmal than third_party/merkmal/PINNED.\n"
        "  expected: merkmal ${expected_version} (${expected_commit})\n"
        "  got: ${output}")
endif()

# detect carries it too, beside the resolved threshold: a log with one and not
# the other cannot be used to reproduce the run, because the threshold is a
# value on the scale the merkmal build defines.
if(NOT DEFINED FIXTURE)
    message(FATAL_ERROR "FIXTURE is required")
endif()
if(NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR is required")
endif()
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")

execute_process(
    COMMAND "${COGNATOR_CLI}" detect
        --in "${FIXTURE}"
        --out "${TEST_WORK_DIR}/provenance.sidecar.tsv"
        --threshold 0.70
        --min-pairs 1
        --seed 17
    RESULT_VARIABLE detect_result
    ERROR_VARIABLE detect_error
)
if(NOT detect_result EQUAL 0)
    message(FATAL_ERROR "detect returned ${detect_result}: ${detect_error}")
endif()
if(NOT detect_error MATCHES "merkmal ${expected_version} \\(${expected_commit}\\)")
    message(FATAL_ERROR "detect summary omits the merkmal build: ${detect_error}")
endif()
