if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()
if(NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR is required")
endif()

set(install_dir "${TEST_WORK_DIR}/install")
set(source_dir "${TEST_WORK_DIR}/consumer")
set(build_dir "${TEST_WORK_DIR}/consumer-build")

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${source_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${install_dir}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "install failed: ${install_output}\n${install_error}")
endif()

file(WRITE "${source_dir}/CMakeLists.txt"
"cmake_minimum_required(VERSION 3.20)\n"
"project(cognator_consumer LANGUAGES C)\n"
"find_package(cognator CONFIG REQUIRED)\n"
"add_executable(consumer main.c)\n"
"target_link_libraries(consumer PRIVATE cognator::cognator)\n")
file(WRITE "${source_dir}/main.c"
"#include <cognator.h>\n"
"#include <stdio.h>\n"
"int main(void) {\n"
"    cg_options options;\n"
"    cg_options_init_defaults(&options);\n"
"    if (options.iterations != 3) return 1;\n"
"    puts(cg_version_string());\n"
"    return 0;\n"
"}\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${source_dir}" -B "${build_dir}" -DCMAKE_PREFIX_PATH=${install_dir}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "consumer configure failed: ${configure_output}\n${configure_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${build_dir}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "consumer build failed: ${build_output}\n${build_error}")
endif()

execute_process(
    COMMAND "${build_dir}/consumer"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_output
    ERROR_VARIABLE run_error
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "consumer run failed: ${run_output}\n${run_error}")
endif()
if(NOT run_output MATCHES "^0\\.1\\.0")
    message(FATAL_ERROR "consumer output mismatch: ${run_output}")
endif()

if(NOT EXISTS "${install_dir}/bin/cognator")
    message(FATAL_ERROR "installed CLI not found at ${install_dir}/bin/cognator")
endif()
