if(NOT DEFINED Python3_EXECUTABLE)
    message(FATAL_ERROR "Python3_EXECUTABLE is required")
endif()
if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED COGNATOR_CLI)
    message(FATAL_ERROR "COGNATOR_CLI is required")
endif()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" setup.py build_ext --inplace
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE build_result
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Python extension build returned ${build_result}: ${build_error}")
endif()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s tests/python
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_output
    ERROR_VARIABLE test_error
    COMMAND_ECHO STDOUT
)
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR "Python tests returned ${test_result}:\n${test_output}\n${test_error}")
endif()
message(STATUS "${test_output}")
