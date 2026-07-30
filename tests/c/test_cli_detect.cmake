if(NOT DEFINED COGNATOR_CLI)
    message(FATAL_ERROR "COGNATOR_CLI is required")
endif()
if(NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR is required")
endif()

file(MAKE_DIRECTORY "${TEST_WORK_DIR}")
file(WRITE "${TEST_WORK_DIR}/fixture.tsv"
"ID\tLanguage_ID\tConcept\tSegments\tFamily\tCognacy\n"
"a1\tlectA\tC1\tp a k a\tsyn\t1\n"
"a2\tlectA\tC2\tt u k i\tsyn\t2\n"
"b1\tlectB\tC1\tp a g a\tsyn\t1\n"
"b2\tlectB\tC2\tt u g i\tsyn\t2\n"
"c1\tlectC\tC1\tb a k a\tsyn\t1\n"
"c2\tlectC\tC2\td u k i\tsyn\t2\n"
"z1\tlectZ\tC2\tzzz_invalid a\tsyn\t3\n")

execute_process(
    COMMAND "${COGNATOR_CLI}" detect
    RESULT_VARIABLE missing_result
    ERROR_VARIABLE missing_error
)
if(NOT missing_result EQUAL 2)
    message(FATAL_ERROR "detect without args returned ${missing_result}, expected 2")
endif()
if(NOT missing_error MATCHES "--in and --out are required")
    message(FATAL_ERROR "missing-args error did not mention required paths: ${missing_error}")
endif()

execute_process(
    COMMAND "${COGNATOR_CLI}" detect --in "${TEST_WORK_DIR}/missing.tsv" --out "${TEST_WORK_DIR}/missing_sidecar.tsv"
    RESULT_VARIABLE bad_input_result
    ERROR_VARIABLE bad_input_error
)
if(NOT bad_input_result EQUAL 1)
    message(FATAL_ERROR "detect with missing input returned ${bad_input_result}, expected 1")
endif()
if(NOT bad_input_error MATCHES "read .*missing.tsv")
    message(FATAL_ERROR "bad-input error did not mention input path: ${bad_input_error}")
endif()

execute_process(
    COMMAND "${COGNATOR_CLI}" detect
        --in "${TEST_WORK_DIR}/fixture.tsv"
        --out "${TEST_WORK_DIR}/sidecar.tsv"
        --oov "${TEST_WORK_DIR}/oov.tsv"
        --preset support-adaptive-bridge
        --threshold 0.70
        --bootstrap 2
        --bootstrap-sigma 0.01
        --graph-top-k 2
        --scorer posterior
        --min-pairs 1
        --seed 11
    RESULT_VARIABLE detect_result
    ERROR_VARIABLE detect_error
)
if(NOT detect_result EQUAL 0)
    message(FATAL_ERROR "detect returned ${detect_result}: ${detect_error}")
endif()
if(NOT detect_error MATCHES "7 rows, threshold=0.700, 1 OOV types")
    message(FATAL_ERROR "detect summary mismatch: ${detect_error}")
endif()

file(READ "${TEST_WORK_DIR}/sidecar.tsv" sidecar)
if(NOT sidecar MATCHES "^ID\tCognator_ID\tCognator_Confidence")
    message(FATAL_ERROR "sidecar header mismatch: ${sidecar}")
endif()
if(NOT sidecar MATCHES "a1\tsyn:C1:" OR NOT sidecar MATCHES "z1\tsyn:C2:")
    message(FATAL_ERROR "sidecar missing expected rows: ${sidecar}")
endif()

file(READ "${TEST_WORK_DIR}/oov.tsv" oov)
if(NOT oov STREQUAL "grapheme\tcount\nzzz_invalid\t1\n")
    message(FATAL_ERROR "OOV report mismatch: ${oov}")
endif()

execute_process(
    COMMAND "${COGNATOR_CLI}" detect
        --in "${TEST_WORK_DIR}/fixture.tsv"
        --out "${TEST_WORK_DIR}/sidecar_2.tsv"
        --oov-out "${TEST_WORK_DIR}/oov_2.tsv"
        --preset support-adaptive-bridge
        --threshold=0.70
        --bootstrap=2
        --bootstrap-sigma=0.01
        --graph-top-k=2
        --scorer=posterior
        --min-pairs=1
        --seed=11
    RESULT_VARIABLE detect_result_2
    ERROR_VARIABLE detect_error_2
)
if(NOT detect_result_2 EQUAL 0)
    message(FATAL_ERROR "second detect returned ${detect_result_2}: ${detect_error_2}")
endif()
file(READ "${TEST_WORK_DIR}/sidecar_2.tsv" sidecar_2)
if(NOT sidecar STREQUAL sidecar_2)
    message(FATAL_ERROR "detect output is not deterministic across repeated runs")
endif()
