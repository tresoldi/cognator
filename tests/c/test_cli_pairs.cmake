# The pairwise export through the CLI: --pairs-out (TSV and q_pairs JSON),
# --pairs-unit morphemes and --morpheme-ids, on the `+`-segmented fixture.
if(NOT DEFINED COGNATOR_CLI)
    message(FATAL_ERROR "COGNATOR_CLI is required")
endif()
if(NOT DEFINED FIXTURE)
    message(FATAL_ERROR "FIXTURE is required")
endif()
if(NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR is required")
endif()
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")

set(common --preset support-adaptive-bridge --threshold 0 --min-pairs 1 --scorer posterior --seed 3)

function(run_detect out_var)
    execute_process(
        COMMAND "${COGNATOR_CLI}" detect --in "${FIXTURE}" ${common} ${ARGN}
        RESULT_VARIABLE result
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "detect ${ARGN} returned ${result}: ${error}")
    endif()
    set(${out_var} "${error}" PARENT_SCOPE)
endfunction()

# Without the new options: the reference sidecar.
run_detect(plain_log --out "${TEST_WORK_DIR}/plain.tsv")
if(plain_log MATCHES "pairs")
    message(FATAL_ERROR "a plain run mentioned pairs: ${plain_log}")
endif()

# Word pairs, TSV by extension. The sidecar must not move.
run_detect(words_log --out "${TEST_WORK_DIR}/words_sidecar.tsv" --pairs-out "${TEST_WORK_DIR}/words.tsv")
file(READ "${TEST_WORK_DIR}/plain.tsv" plain)
file(READ "${TEST_WORK_DIR}/words_sidecar.tsv" words_sidecar)
if(NOT plain STREQUAL words_sidecar)
    message(FATAL_ERROR "--pairs-out changed the sidecar")
endif()
if(NOT words_log MATCHES "42 word pairs")
    message(FATAL_ERROR "word pair summary mismatch: ${words_log}")
endif()
file(READ "${TEST_WORK_DIR}/words.tsv" words)
if(NOT words MATCHES "^form_a\tform_b\tweight\tdistance\tglobal\tlocal\toverlap\tnull_n\tnull_scope\n")
    message(FATAL_ERROR "word pairs header mismatch: ${words}")
endif()

# Morpheme pairs, TSV, with morpheme clusters in the sidecar.
run_detect(morph_log --out "${TEST_WORK_DIR}/morph_sidecar.tsv"
    --pairs-out "${TEST_WORK_DIR}/morph.tsv" --pairs-unit morphemes --morpheme-ids)
if(NOT morph_log MATCHES "150 morpheme pairs")
    message(FATAL_ERROR "morpheme pair summary mismatch: ${morph_log}")
endif()
file(READ "${TEST_WORK_DIR}/morph.tsv" morph)
if(NOT morph MATCHES "^form_a\tmorph_a\tform_b\tmorph_b\tweight\tdistance\t")
    message(FATAL_ERROR "morpheme pairs header mismatch")
endif()
# The shared root is strong; the replaced second morpheme is not.
if(NOT morph MATCHES "\na_pebble\t0\tb_pebble\t0\t(1\\.000000|0\\.9[0-9]+)\t")
    message(FATAL_ERROR "a_pebble#0 ~ b_pebble#0 is not strong")
endif()
if(NOT morph MATCHES "\na_pebble\t1\tc_pebble\t1\t0\\.0[0-9]+\t")
    message(FATAL_ERROR "a_pebble#1 ~ c_pebble#1 (keris ~ wona) is not weak")
endif()
# Equal distance, unequal weight: the identical suffix against the identical root.
if(NOT morph MATCHES "\na_water\t1\tb_water\t1\t0\\.0[0-9]+\t0\\.000000\t")
    message(FATAL_ERROR "the shared suffix n a was not penalised")
endif()
if(NOT morph MATCHES "\na_water\t0\tb_water\t0\t1\\.000000\t0\\.000000\t")
    message(FATAL_ERROR "the shared root m o r e v was not strong")
endif()
# Never a morpheme against another morpheme of its own word.
file(STRINGS "${TEST_WORK_DIR}/morph.tsv" morph_lines)
list(REMOVE_AT morph_lines 0)
foreach(line IN LISTS morph_lines)
    string(REPLACE "\t" ";" fields "${line}")
    list(GET fields 0 form_a)
    list(GET fields 2 form_b)
    if(form_a STREQUAL form_b)
        message(FATAL_ERROR "a same-word morpheme pair was written: ${line}")
    endif()
endforeach()

file(READ "${TEST_WORK_DIR}/morph_sidecar.tsv" morph_sidecar)
if(NOT morph_sidecar MATCHES "^ID\t[^\n]*\tCognator_Bootstrap_High\tCognator_Morpheme_IDs\n")
    message(FATAL_ERROR "morpheme sidecar header mismatch")
endif()
if(NOT morph_sidecar MATCHES "\nc_pebble\t[^\n]*\ttoy:PEBBLE:m[0-9]+ -\n")
    message(FATAL_ERROR "c_pebble's replaced morpheme is not '-'")
endif()
# Strip the added column: what remains is the plain sidecar.
string(REGEX REPLACE "\t[^\t\n]*\n" "\n" morph_stripped "${morph_sidecar}")
if(NOT morph_stripped STREQUAL plain)
    message(FATAL_ERROR "--morpheme-ids changed the sidecar beyond its added column")
endif()

# q_pairs JSON, by extension and by flag.
run_detect(json_log --out "${TEST_WORK_DIR}/json_sidecar.tsv"
    --pairs-out "${TEST_WORK_DIR}/morph.json" --pairs-unit morphemes)
file(READ "${TEST_WORK_DIR}/morph.json" json)
foreach(needle "\"role\": \"q\"" "\"schema_version\": \"0.1.0\"" "\"producer\": \"cognator@0.1.0+merkmal."
               "\"form_obs_a\": \"a_pebble#0\"" "\"diagnostics\"" "\"calibration\"" "\"threshold\"")
    string(FIND "${json}" "${needle}" at)
    if(at LESS 0)
        message(FATAL_ERROR "q_pairs JSON lacks ${needle}")
    endif()
endforeach()
run_detect(flag_log --out "${TEST_WORK_DIR}/flag_sidecar.tsv"
    --pairs-out "${TEST_WORK_DIR}/morph_json.txt" --pairs-format json --pairs-unit morphemes)
file(READ "${TEST_WORK_DIR}/morph_json.txt" json_flag)
if(NOT json STREQUAL json_flag)
    message(FATAL_ERROR "--pairs-format json differs from the .json extension")
endif()

# Deterministic across runs.
run_detect(again_log --out "${TEST_WORK_DIR}/again_sidecar.tsv"
    --pairs-out "${TEST_WORK_DIR}/morph_again.tsv" --pairs-unit morphemes --morpheme-ids)
file(READ "${TEST_WORK_DIR}/morph_again.tsv" morph_again)
if(NOT morph STREQUAL morph_again)
    message(FATAL_ERROR "morpheme pairs are not deterministic")
endif()

# Refusals.
execute_process(
    COMMAND "${COGNATOR_CLI}" detect --in "${FIXTURE}" --out "${TEST_WORK_DIR}/x.tsv" --pairs-unit words --morpheme-ids
    RESULT_VARIABLE bad_result ERROR_VARIABLE bad_error)
if(NOT bad_result EQUAL 2 OR NOT bad_error MATCHES "needs --pairs-unit morphemes")
    message(FATAL_ERROR "--morpheme-ids with word pairs was not refused: ${bad_result} ${bad_error}")
endif()
execute_process(
    COMMAND "${COGNATOR_CLI}" detect --in "${FIXTURE}" --out "${TEST_WORK_DIR}/x.tsv" --pairs-out "${TEST_WORK_DIR}/x.p" --pairs-format xml
    RESULT_VARIABLE bad_result ERROR_VARIABLE bad_error)
if(NOT bad_result EQUAL 2 OR NOT bad_error MATCHES "tsv or json")
    message(FATAL_ERROR "a bad --pairs-format was not refused: ${bad_result} ${bad_error}")
endif()
execute_process(
    COMMAND "${COGNATOR_CLI}" detect --in "${FIXTURE}" --out "${TEST_WORK_DIR}/x.tsv" --pairs-unit morphemes
    RESULT_VARIABLE bad_result ERROR_VARIABLE bad_error)
if(NOT bad_result EQUAL 2 OR NOT bad_error MATCHES "need --pairs-out")
    message(FATAL_ERROR "--pairs-unit without --pairs-out was not refused: ${bad_result} ${bad_error}")
endif()
