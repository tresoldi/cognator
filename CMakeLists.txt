cmake_minimum_required(VERSION 3.20)
project(cognator VERSION 0.1.0 LANGUAGES C)

option(COGNATOR_BUILD_TESTS "Build native C tests" ON)
option(COGNATOR_BUILD_CLI "Build the cognator CLI" ON)
option(COGNATOR_ENABLE_GO_C_PARITY "Run Go/C parity tests; requires a Go-compatible ../merkmal/go checkout" OFF)
set(COGNATOR_GO_MERKMAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../merkmal/go" CACHE PATH "Go-compatible merkmal module for Go/C parity tests")
set(COGNATOR_ENABLE_SANITIZER "" CACHE STRING "Sanitizer: address or undefined")
set_property(CACHE COGNATOR_ENABLE_SANITIZER PROPERTY STRINGS "" address undefined)

if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND COGNATOR_ENABLE_SANITIZER)
    message(FATAL_ERROR "Sanitizers are supported only with GCC and Clang")
endif()
if(COGNATOR_ENABLE_SANITIZER AND NOT COGNATOR_ENABLE_SANITIZER MATCHES "^(address|undefined)$")
    message(FATAL_ERROR "COGNATOR_ENABLE_SANITIZER must be address or undefined")
endif()

find_package(merkmal CONFIG QUIET)
if(NOT TARGET merkmal::merkmal)
    set(COGNATOR_MERKMAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../merkmal" CACHE PATH "Merkmal C source checkout")
    if(EXISTS "${COGNATOR_MERKMAL_SOURCE_DIR}/CMakeLists.txt")
        set(MERKMAL_BUILD_TESTS OFF CACHE BOOL "Build merkmal tests" FORCE)
        add_subdirectory("${COGNATOR_MERKMAL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/merkmal")
    else()
        message(FATAL_ERROR "merkmal::merkmal not found; install merkmal or set COGNATOR_MERKMAL_SOURCE_DIR")
    endif()
endif()

add_library(cognator
    src/context.c
    src/data.c
    src/detect.c
    src/eval.c
    src/align.c
    src/graph.c
    src/oov.c
    src/options.c
    src/posterior.c
    src/segment.c
    src/wordlist.c
)
add_library(cognator::cognator ALIAS cognator)
target_compile_features(cognator PUBLIC c_std_99)
target_include_directories(cognator PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cognator PUBLIC merkmal::merkmal)
if(UNIX AND NOT APPLE)
    target_link_libraries(cognator PRIVATE m)
endif()
set_target_properties(cognator PROPERTIES C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_compile_definitions(cognator PRIVATE COGNATOR_BUILDING_LIBRARY)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(cognator PUBLIC COGNATOR_SHARED)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cognator PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
if(COGNATOR_ENABLE_SANITIZER)
    target_compile_options(cognator PUBLIC -fsanitize=${COGNATOR_ENABLE_SANITIZER} -fno-omit-frame-pointer)
    target_link_options(cognator PUBLIC -fsanitize=${COGNATOR_ENABLE_SANITIZER})
endif()

if(COGNATOR_BUILD_CLI)
    add_executable(cognator_cli cmd/cognator-c/main.c)
    set_target_properties(cognator_cli PROPERTIES OUTPUT_NAME cognator)
    target_link_libraries(cognator_cli PRIVATE cognator::cognator)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/cognatorConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/cognatorConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cognator")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cognatorConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
configure_file(cmake/cognator.pc.in "${CMAKE_CURRENT_BINARY_DIR}/cognator.pc" @ONLY)

install(TARGETS cognator EXPORT cognatorTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(COGNATOR_BUILD_CLI)
    install(TARGETS cognator_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(FILES include/cognator.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT cognatorTargets NAMESPACE cognator:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cognator)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cognatorConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cognatorConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cognator")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cognator.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

if(COGNATOR_BUILD_TESTS)
    include(CTest)
    add_executable(test_options tests/c/test_options.c)
    target_link_libraries(test_options PRIVATE cognator::cognator)
    add_test(NAME options COMMAND test_options)
    add_executable(test_context tests/c/test_context.c)
    target_link_libraries(test_context PRIVATE cognator::cognator)
    add_test(NAME context COMMAND test_context)
    add_executable(test_wordlist tests/c/test_wordlist.c)
    target_link_libraries(test_wordlist PRIVATE cognator::cognator)
    add_test(NAME wordlist COMMAND test_wordlist)
    add_executable(test_segment tests/c/test_segment.c)
    target_link_libraries(test_segment PRIVATE cognator::cognator)
    add_test(NAME segment COMMAND test_segment)
    add_executable(test_align tests/c/test_align.c)
    target_link_libraries(test_align PRIVATE cognator::cognator)
    add_test(NAME align COMMAND test_align)
    add_executable(test_posterior tests/c/test_posterior.c)
    target_link_libraries(test_posterior PRIVATE cognator::cognator)
    add_test(NAME posterior COMMAND test_posterior)
    add_executable(test_graph tests/c/test_graph.c)
    target_link_libraries(test_graph PRIVATE cognator::cognator)
    add_test(NAME graph COMMAND test_graph)
    add_executable(test_detect tests/c/test_detect.c)
    target_link_libraries(test_detect PRIVATE cognator::cognator)
    add_test(NAME detect COMMAND test_detect)
    add_executable(test_eval tests/c/test_eval.c)
    target_link_libraries(test_eval PRIVATE cognator::cognator)
    add_test(NAME eval COMMAND test_eval)
    if(COGNATOR_BUILD_CLI)
        add_test(NAME cli_version COMMAND cognator_cli version)
        set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "cognator 0.1.0")
        add_test(NAME cli_detect
            COMMAND ${CMAKE_COMMAND}
                -DCOGNATOR_CLI=$<TARGET_FILE:cognator_cli>
                -DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_cli_detect
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/test_cli_detect.cmake)
        add_test(NAME cli_eval
            COMMAND ${CMAKE_COMMAND}
                -DCOGNATOR_CLI=$<TARGET_FILE:cognator_cli>
                -DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_cli_eval
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/test_cli_eval.cmake)
        add_test(NAME linguistic_smoke
            COMMAND ${CMAKE_COMMAND}
                -DCOGNATOR_CLI=$<TARGET_FILE:cognator_cli>
                -DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_linguistic_smoke
                -DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/linguistic
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/test_linguistic_smoke.cmake)
        add_test(NAME linguistic_validation
            COMMAND ${CMAKE_COMMAND}
                -DCOGNATOR_CLI=$<TARGET_FILE:cognator_cli>
                -DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_linguistic_validation
                -DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/linguistic
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/test_linguistic_validation.cmake)
        add_test(NAME cli_sweep_bench
            COMMAND ${CMAKE_COMMAND}
                -DCOGNATOR_CLI=$<TARGET_FILE:cognator_cli>
                -DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_cli_sweep_bench
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/test_cli_sweep_bench.cmake)
        add_test(NAME cli_diag_oovscan
            COMMAND ${CMAKE_COMMAND}
                -DCOGNATOR_CLI=$<TARGET_FILE:cognator_cli>
                -DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_cli_diag_oovscan
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/test_cli_diag_oovscan.cmake)
        add_test(NAME package_consumer
            COMMAND ${CMAKE_COMMAND}
                -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_package_consumer
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/test_package_consumer.cmake)
        if(COGNATOR_ENABLE_GO_C_PARITY)
            find_package(Python3 COMPONENTS Interpreter REQUIRED)
            add_test(NAME go_c_parity
                COMMAND ${CMAKE_COMMAND}
                    -DCOGNATOR_CLI=$<TARGET_FILE:cognator_cli>
                    -DPython3_EXECUTABLE=${Python3_EXECUTABLE}
                    -DGO_MERKMAL_GO_DIR=${COGNATOR_GO_MERKMAL_DIR}
                    -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                    -DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_go_c_parity
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/test_go_c_parity.cmake)
        endif()
    endif()
endif()
