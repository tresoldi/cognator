if(NOT DEFINED COGNATOR_CLI)
    message(FATAL_ERROR "COGNATOR_CLI is required")
endif()
if(NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR is required")
endif()

file(MAKE_DIRECTORY "${TEST_WORK_DIR}")
file(WRITE "${TEST_WORK_DIR}/bench.tsv"
"ID\tLanguage_ID\tConcept\tSegments\tFamily\tCognacy\n"
"a1\tL1\tC1\tp a k a\tfamA\t1\n"
"b1\tL2\tC1\tp a g a\tfamA\t1\n"
"c1\tL3\tC1\tb a k a\tfamA\t1\n"
"a2\tL1\tC2\tt u k i\tfamA\t2\n"
"b2\tL2\tC2\tt u g i\tfamA\t2\n"
"c2\tL3\tC2\td u k i\tfamA\t2\n"
"x1\tX1\tC1\tm a\tfamB\t1\n"
"x2\tX2\tC1\tm a\tfamB\t1\n")

execute_process(
    COMMAND "${COGNATOR_CLI}" sweep
        --in "${TEST_WORK_DIR}/bench.tsv"
        --out-tsv "${TEST_WORK_DIR}/sweep.tsv"
        --thresholds auto,0.70
        --preset support-adaptive-bridge
        --bootstrap 1
        --min-pairs 1
        --seed 19
    RESULT_VARIABLE sweep_result
    ERROR_VARIABLE sweep_error
)
if(NOT sweep_result EQUAL 0)
    message(FATAL_ERROR "sweep returned ${sweep_result}: ${sweep_error}")
endif()
file(READ "${TEST_WORK_DIR}/sweep.tsv" sweep)
if(NOT sweep MATCHES "^threshold_label\tforms\tcognacy\tresolved_threshold\tclusters\tprecision\trecall\tf1\telapsed_ms\n")
    message(FATAL_ERROR "sweep header mismatch: ${sweep}")
endif()
if(NOT sweep MATCHES "auto\t8\t8\t")
    message(FATAL_ERROR "sweep auto row missing: ${sweep}")
endif()
if(NOT sweep MATCHES "0.70\t8\t8\t0.700000\t")
    message(FATAL_ERROR "sweep explicit threshold row missing: ${sweep}")
endif()

execute_process(
    COMMAND "${COGNATOR_CLI}" bench
        --in "${TEST_WORK_DIR}/bench.tsv"
        --out-tsv "${TEST_WORK_DIR}/bench_out.tsv"
        --families famA,famB
        --preset support-adaptive-bridge
        --threshold 0.70
        --bootstrap 1
        --min-pairs 1
        --seed 23
        --min-cognacy 3
    RESULT_VARIABLE bench_result
    ERROR_VARIABLE bench_error
)
if(NOT bench_result EQUAL 0)
    message(FATAL_ERROR "bench returned ${bench_result}: ${bench_error}")
endif()
file(READ "${TEST_WORK_DIR}/bench_out.tsv" bench)
if(NOT bench MATCHES "^family\tlects\tconcepts\tforms\tcognacy\testimated_pairs\toov\tthreshold\tclusters\tprecision\trecall\tf1\telapsed_ms\tskipped\n")
    message(FATAL_ERROR "bench header mismatch: ${bench}")
endif()
if(NOT bench MATCHES "famA\t3\t2\t6\t6\t6\t0\t0.700000\t")
    message(FATAL_ERROR "bench famA row mismatch: ${bench}")
endif()
if(NOT bench MATCHES "famB\t2\t1\t2\t2\t1\t0\t0.000000\t0\t0.000000\t0.000000\t0.000000\t0\ttoo_few_cognacy")
    message(FATAL_ERROR "bench skip row mismatch: ${bench}")
endif()

execute_process(
    COMMAND "${COGNATOR_CLI}" bench
        --in "${TEST_WORK_DIR}/bench.tsv"
        --out-tsv "${TEST_WORK_DIR}/bench_pair_guard.tsv"
        --families famA
        --preset support-adaptive-bridge
        --max-pairs 5
        --min-cognacy 3
    RESULT_VARIABLE pair_guard_result
    ERROR_VARIABLE pair_guard_error
)
if(NOT pair_guard_result EQUAL 0)
    message(FATAL_ERROR "bench pair guard returned ${pair_guard_result}: ${pair_guard_error}")
endif()
file(READ "${TEST_WORK_DIR}/bench_pair_guard.tsv" pair_guard)
if(NOT pair_guard MATCHES "famA\t3\t2\t6\t6\t6\t0\t0.000000\t0\t0.000000\t0.000000\t0.000000\t0\ttoo_many_pairs")
    message(FATAL_ERROR "bench pair guard row mismatch: ${pair_guard}")
endif()

execute_process(
    COMMAND "${COGNATOR_CLI}" bench
    RESULT_VARIABLE missing_result
    ERROR_VARIABLE missing_error
)
if(NOT missing_result EQUAL 2)
    message(FATAL_ERROR "bench without args returned ${missing_result}, expected 2")
endif()
if(NOT missing_error MATCHES "--in and --out-tsv are required")
    message(FATAL_ERROR "bench missing-args error mismatch: ${missing_error}")
endif()
