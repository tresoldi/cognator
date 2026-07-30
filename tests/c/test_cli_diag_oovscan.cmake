if(NOT DEFINED COGNATOR_CLI)
    message(FATAL_ERROR "COGNATOR_CLI is required")
endif()
if(NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR is required")
endif()

file(MAKE_DIRECTORY "${TEST_WORK_DIR}")
file(WRITE "${TEST_WORK_DIR}/diag.tsv"
"ID\tLanguage_ID\tConcept\tSegments\tFamily\tCognacy\n"
"a1\tL1\tC1\tp a zzz_invalid\tfamA\t1\n"
"b1\tL2\tC1\tp a\tfamA\t1\n"
"c1\tL3\tC2\tm a aaa_invalid\tfamA\t2\n"
"x1\tX1\tC1\tm a\tfamB\t1\n")

execute_process(
    COMMAND "${COGNATOR_CLI}" diag
        --data "${TEST_WORK_DIR}/diag.tsv"
        --family famA
        --top 2
    RESULT_VARIABLE diag_result
    OUTPUT_VARIABLE diag_output
    ERROR_VARIABLE diag_error
)
if(NOT diag_result EQUAL 0)
    message(FATAL_ERROR "diag returned ${diag_result}: ${diag_error}")
endif()
if(NOT diag_output MATCHES "rows: 3  system: descriptive")
    message(FATAL_ERROR "diag rows/system mismatch: ${diag_output}")
endif()
if(NOT diag_output MATCHES "lects: 3  concepts: 2  expert-cognate-sets: 3")
    message(FATAL_ERROR "diag summary mismatch: ${diag_output}")
endif()
if(NOT diag_output MATCHES "segments: 8  oov types: 2")
    message(FATAL_ERROR "diag segment/OOV mismatch: ${diag_output}")
endif()
if(NOT diag_output MATCHES "aaa_invalid[ ]+1")
    message(FATAL_ERROR "diag missing sorted OOV row: ${diag_output}")
endif()
if(NOT diag_output MATCHES "zzz_invalid[ ]+1")
    message(FATAL_ERROR "diag missing second OOV row: ${diag_output}")
endif()

execute_process(
    COMMAND "${COGNATOR_CLI}" oovscan
        --in "${TEST_WORK_DIR}/diag.tsv"
        --out "${TEST_WORK_DIR}/oov.md"
        --top 1
    RESULT_VARIABLE oovscan_result
    ERROR_VARIABLE oovscan_error
)
if(NOT oovscan_result EQUAL 0)
    message(FATAL_ERROR "oovscan returned ${oovscan_result}: ${oovscan_error}")
endif()
file(READ "${TEST_WORK_DIR}/oov.md" oovscan_output)
if(NOT oovscan_output MATCHES "# OOV graphemes \\(system: descriptive\\)")
    message(FATAL_ERROR "oovscan header mismatch: ${oovscan_output}")
endif()
if(NOT oovscan_output MATCHES "Segments scanned: 10  Distinct OOV types: 2")
    message(FATAL_ERROR "oovscan summary mismatch: ${oovscan_output}")
endif()
if(NOT oovscan_output MATCHES "\\| `aaa_invalid` \\| 1 \\|")
    message(FATAL_ERROR "oovscan top row mismatch: ${oovscan_output}")
endif()
