if(NOT DEFINED COGNATOR_CLI)
    message(FATAL_ERROR "COGNATOR_CLI is required")
endif()
if(NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR is required")
endif()

file(MAKE_DIRECTORY "${TEST_WORK_DIR}")
file(WRITE "${TEST_WORK_DIR}/truth.tsv"
"ID\tLanguage_ID\tConcept\tSegments\tFamily\tCognacy\n"
"a\tL1\tC1\tp a\tsyn\tX\n"
"b\tL2\tC1\tp a\tsyn\tX\n"
"c\tL1\tC1\tt a\tsyn\tY\n"
"d\tL2\tC1\tt a\tsyn\tY\n")
file(WRITE "${TEST_WORK_DIR}/pred.tsv"
"ID\tCognator_ID\n"
"a\tM\n"
"b\tM\n"
"c\tM\n"
"d\tM\n")

execute_process(
    COMMAND "${COGNATOR_CLI}" eval
    RESULT_VARIABLE missing_result
    ERROR_VARIABLE missing_error
)
if(NOT missing_result EQUAL 2)
    message(FATAL_ERROR "eval without args returned ${missing_result}, expected 2")
endif()
if(NOT missing_error MATCHES "--pred and --truth are required")
    message(FATAL_ERROR "missing-args error did not mention required paths: ${missing_error}")
endif()

execute_process(
    COMMAND "${COGNATOR_CLI}" eval --pred "${TEST_WORK_DIR}/pred.tsv" --truth "${TEST_WORK_DIR}/truth.tsv"
    RESULT_VARIABLE eval_result
    OUTPUT_VARIABLE eval_output
    ERROR_VARIABLE eval_error
)
if(NOT eval_result EQUAL 0)
    message(FATAL_ERROR "eval returned ${eval_result}: ${eval_error}")
endif()
if(NOT eval_output MATCHES "items scored: 4  \\(pred=4  truth=4\\)")
    message(FATAL_ERROR "coverage line mismatch: ${eval_output}")
endif()
if(NOT eval_output MATCHES "B-cubed Precision: 0.5000")
    message(FATAL_ERROR "precision line mismatch: ${eval_output}")
endif()
if(NOT eval_output MATCHES "B-cubed Recall:    1.0000")
    message(FATAL_ERROR "recall line mismatch: ${eval_output}")
endif()
if(NOT eval_output MATCHES "B-cubed F1:        0.6667")
    message(FATAL_ERROR "F1 line mismatch: ${eval_output}")
endif()

execute_process(
    COMMAND "${COGNATOR_CLI}" eval --pred "${TEST_WORK_DIR}/pred.tsv" --truth "${TEST_WORK_DIR}/truth.tsv" --truth-col Other
    RESULT_VARIABLE bad_col_result
    ERROR_VARIABLE bad_col_error
)
if(NOT bad_col_result EQUAL 1)
    message(FATAL_ERROR "eval bad truth column returned ${bad_col_result}, expected 1")
endif()
if(NOT bad_col_error MATCHES "unsupported option")
    message(FATAL_ERROR "bad truth column error mismatch: ${bad_col_error}")
endif()
