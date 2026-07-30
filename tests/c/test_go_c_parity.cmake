if(NOT DEFINED COGNATOR_CLI)
    message(FATAL_ERROR "COGNATOR_CLI is required")
endif()
if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR is required")
endif()
if(NOT DEFINED GO_MERKMAL_GO_DIR)
    set(GO_MERKMAL_GO_DIR "${SOURCE_DIR}/../merkmal/go")
endif()

find_program(GO_EXECUTABLE go)
if(NOT GO_EXECUTABLE)
    message(FATAL_ERROR "go executable not found; Go/C parity requires the old Go CLI")
endif()
if(NOT EXISTS "${GO_MERKMAL_GO_DIR}/go.mod")
    message(FATAL_ERROR "${GO_MERKMAL_GO_DIR}/go.mod is missing; set COGNATOR_GO_MERKMAL_DIR to a Go-compatible merkmal checkout")
endif()

file(MAKE_DIRECTORY "${TEST_WORK_DIR}")
set(go_modfile "${TEST_WORK_DIR}/go-parity.mod")
file(READ "${SOURCE_DIR}/go.mod" go_mod)
string(REGEX REPLACE
    "replace github.com/tresoldi/merkmal/go => [^\n]+"
    "replace github.com/tresoldi/merkmal/go => ${GO_MERKMAL_GO_DIR}"
    go_mod
    "${go_mod}")
file(WRITE "${go_modfile}" "${go_mod}")
execute_process(
    COMMAND "${GO_EXECUTABLE}" mod tidy "-modfile=${go_modfile}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE tidy_result
    ERROR_VARIABLE tidy_error
)
if(NOT tidy_result EQUAL 0)
    message(FATAL_ERROR "go mod tidy for parity returned ${tidy_result}: ${tidy_error}")
endif()

function(run_parity name input seed min_pair_agreement max_f1_delta)
    set(go_sidecar "${TEST_WORK_DIR}/${name}.go.sidecar.tsv")
    set(c_sidecar "${TEST_WORK_DIR}/${name}.c.sidecar.tsv")

    execute_process(
        COMMAND "${GO_EXECUTABLE}" run "-modfile=${go_modfile}" ./cmd/cognator detect
            --in "${input}"
            --out "${go_sidecar}"
            --preset support-adaptive-bridge
            --threshold 0.70
            --bootstrap 0
            --scorer posterior
            --seed "${seed}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE go_result
        ERROR_VARIABLE go_error
    )
    if(NOT go_result EQUAL 0)
        message(FATAL_ERROR "${name}: Go detect returned ${go_result}: ${go_error}")
    endif()

    execute_process(
        COMMAND "${COGNATOR_CLI}" detect
            --in "${input}"
            --out "${c_sidecar}"
            --preset support-adaptive-bridge
            --threshold 0.70
            --bootstrap 0
            --scorer posterior
            --seed "${seed}"
        RESULT_VARIABLE c_result
        ERROR_VARIABLE c_error
    )
    if(NOT c_result EQUAL 0)
        message(FATAL_ERROR "${name}: C detect returned ${c_result}: ${c_error}")
    endif()

    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${SOURCE_DIR}/scripts/compare_sidecars.py"
            --left "${go_sidecar}"
            --right "${c_sidecar}"
            --truth "${input}"
            --left-label go
            --right-label c
            --min-pair-agreement "${min_pair_agreement}"
            --max-f1-delta "${max_f1_delta}"
        RESULT_VARIABLE compare_result
        OUTPUT_VARIABLE compare_output
        ERROR_VARIABLE compare_error
    )
    if(NOT compare_result EQUAL 0)
        message(FATAL_ERROR "${name}: parity mismatch:\n${compare_output}\n${compare_error}")
    endif()
    message(STATUS "${name} parity:\n${compare_output}")
endfunction()

run_parity("latin_romance_triplets" "${SOURCE_DIR}/tests/fixtures/linguistic/latin_romance_triplets.tsv" 17 0.833332 0.058825)
run_parity("walworth_polynesian_smoke" "${SOURCE_DIR}/tests/fixtures/linguistic/walworth_polynesian_smoke.tsv" 17 0.833332 0.066668)
run_parity("latin_romance_extended" "${SOURCE_DIR}/tests/fixtures/linguistic/latin_romance_extended.tsv" 31 0.916666 0.036507)
