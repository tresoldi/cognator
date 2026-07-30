if(NOT DEFINED Python3_EXECUTABLE)
    message(FATAL_ERROR "Python3_EXECUTABLE is required")
endif()
if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR is required")
endif()

set(venv_dir "${TEST_WORK_DIR}/venv")
file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -m venv --system-site-packages "${venv_dir}"
    RESULT_VARIABLE venv_result
    OUTPUT_VARIABLE venv_output
    ERROR_VARIABLE venv_error
)
if(NOT venv_result EQUAL 0)
    message(FATAL_ERROR "venv failed: ${venv_output}\n${venv_error}")
endif()

if(WIN32)
    set(venv_python "${venv_dir}/Scripts/python.exe")
    set(venv_cognator "${venv_dir}/Scripts/cognator.exe")
else()
    set(venv_python "${venv_dir}/bin/python")
    set(venv_cognator "${venv_dir}/bin/cognator")
endif()

execute_process(
    COMMAND "${venv_python}" -m pip install --no-deps --no-build-isolation "${SOURCE_DIR}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "pip install failed: ${install_output}\n${install_error}")
endif()

execute_process(
    COMMAND "${venv_python}" -c
            "import cognator; rows=[{'ID':'a','Language_ID':'A','Concept':'HAND','Segments':'p a','Family':'fam','Cognacy':'1'},{'ID':'b','Language_ID':'B','Concept':'HAND','Segments':'p a','Family':'fam','Cognacy':'1'}]; result=cognator.detect(rows, threshold=0.70, min_pairs=1); assert cognator.abi_version()==1; assert len(result)==2; assert abs(result.resolved_threshold-0.70)<1e-9"
    RESULT_VARIABLE import_result
    OUTPUT_VARIABLE import_output
    ERROR_VARIABLE import_error
)
if(NOT import_result EQUAL 0)
    message(FATAL_ERROR "installed package smoke failed: ${import_output}\n${import_error}")
endif()

execute_process(
    COMMAND "${venv_cognator}" --help
    RESULT_VARIABLE cli_result
    OUTPUT_VARIABLE cli_output
    ERROR_VARIABLE cli_error
)
if(NOT cli_result EQUAL 0)
    message(FATAL_ERROR "installed console script failed: ${cli_output}\n${cli_error}")
endif()
if(NOT cli_output MATCHES "usage: cognator")
    message(FATAL_ERROR "installed console script help mismatch: ${cli_output}")
endif()
