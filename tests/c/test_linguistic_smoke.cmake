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

function(run_linguistic_fixture name input expected_rows)
    set(sidecar "${TEST_WORK_DIR}/${name}.sidecar.tsv")
    execute_process(
        COMMAND "${COGNATOR_CLI}" detect
            --in "${input}"
            --out "${sidecar}"
            --preset support-adaptive-bridge
            --threshold 0.70
            --bootstrap 2
            --min-pairs 1
            --seed 17
        RESULT_VARIABLE detect_result
        ERROR_VARIABLE detect_error
    )
    if(NOT detect_result EQUAL 0)
        message(FATAL_ERROR "${name}: detect returned ${detect_result}: ${detect_error}")
    endif()
    if(NOT detect_error MATCHES "${expected_rows} rows, threshold=0.700, 0 OOV types")
        message(FATAL_ERROR "${name}: detect summary mismatch: ${detect_error}")
    endif()

    execute_process(
        COMMAND "${COGNATOR_CLI}" eval --pred "${sidecar}" --truth "${input}"
        RESULT_VARIABLE eval_result
        OUTPUT_VARIABLE eval_output
        ERROR_VARIABLE eval_error
    )
    if(NOT eval_result EQUAL 0)
        message(FATAL_ERROR "${name}: eval returned ${eval_result}: ${eval_error}")
    endif()
    if(NOT eval_output MATCHES "items scored: ${expected_rows}  \\(pred=${expected_rows}  truth=${expected_rows}\\)")
        message(FATAL_ERROR "${name}: coverage mismatch: ${eval_output}")
    endif()
    if(NOT eval_output MATCHES "B-cubed Precision: 1.0000")
        message(FATAL_ERROR "${name}: precision regression: ${eval_output}")
    endif()
    if(NOT eval_output MATCHES "B-cubed Recall:    1.0000")
        message(FATAL_ERROR "${name}: recall regression: ${eval_output}")
    endif()
    if(NOT eval_output MATCHES "B-cubed F1:        1.0000")
        message(FATAL_ERROR "${name}: F1 regression: ${eval_output}")
    endif()
endfunction()

run_linguistic_fixture(
    "walworth_polynesian_smoke"
    "${FIXTURE_DIR}/walworth_polynesian_smoke.tsv"
    16
)
run_linguistic_fixture(
    "latin_romance_triplets"
    "${FIXTURE_DIR}/latin_romance_triplets.tsv"
    12
)
