if(NOT DEFINED COGNATOR_CLI)
    message(FATAL_ERROR "COGNATOR_CLI is required")
endif()
if(NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR is required")
endif()
if(NOT DEFINED FIXTURE_DIR)
    message(FATAL_ERROR "FIXTURE_DIR is required")
endif()

file(MAKE_DIRECTORY "${TEST_WORK_DIR}")

set(input "${FIXTURE_DIR}/latin_romance_extended.tsv")
set(sidecar "${TEST_WORK_DIR}/latin_romance_extended.sidecar.tsv")
set(bench_out "${TEST_WORK_DIR}/latin_romance_extended.bench.tsv")

execute_process(
    COMMAND "${COGNATOR_CLI}" detect
        --in "${input}"
        --out "${sidecar}"
        --preset support-adaptive-bridge
        --threshold 0.70
        --bootstrap 2
        --min-pairs 1
        --seed 31
    RESULT_VARIABLE detect_result
    ERROR_VARIABLE detect_error
)
if(NOT detect_result EQUAL 0)
    message(FATAL_ERROR "latin_romance_extended: detect returned ${detect_result}: ${detect_error}")
endif()
if(NOT detect_error MATCHES "24 rows, threshold=0.700, 0 OOV types")
    message(FATAL_ERROR "latin_romance_extended: detect summary mismatch: ${detect_error}")
endif()

execute_process(
    COMMAND "${COGNATOR_CLI}" eval --pred "${sidecar}" --truth "${input}"
    RESULT_VARIABLE eval_result
    OUTPUT_VARIABLE eval_output
    ERROR_VARIABLE eval_error
)
if(NOT eval_result EQUAL 0)
    message(FATAL_ERROR "latin_romance_extended: eval returned ${eval_result}: ${eval_error}")
endif()
if(NOT eval_output MATCHES "items scored: 24  \\(pred=24  truth=24\\)")
    message(FATAL_ERROR "latin_romance_extended: coverage mismatch: ${eval_output}")
endif()
if(NOT eval_output MATCHES "B-cubed Precision: 1.0000")
    message(FATAL_ERROR "latin_romance_extended: precision regression: ${eval_output}")
endif()
if(NOT eval_output MATCHES "B-cubed Recall:    0.8819")
    message(FATAL_ERROR "latin_romance_extended: recall regression: ${eval_output}")
endif()
if(NOT eval_output MATCHES "B-cubed F1:        0.9373")
    message(FATAL_ERROR "latin_romance_extended: F1 regression: ${eval_output}")
endif()

execute_process(
    COMMAND "${COGNATOR_CLI}" bench
        --in "${input}"
        --out-tsv "${bench_out}"
        --families romance
        --preset support-adaptive-bridge
        --threshold 0.70
        --bootstrap 2
        --min-pairs 1
        --seed 31
        --min-cognacy 1
    RESULT_VARIABLE bench_result
    ERROR_VARIABLE bench_error
)
if(NOT bench_result EQUAL 0)
    message(FATAL_ERROR "latin_romance_extended: bench returned ${bench_result}: ${bench_error}")
endif()
file(READ "${bench_out}" bench)
if(NOT bench MATCHES "romance\t4\t6\t24\t24\t36\t0\t0.700000\t10\t1.000000\t0.881944\t0.937269\t")
    message(FATAL_ERROR "latin_romance_extended: bench row mismatch: ${bench}")
endif()
