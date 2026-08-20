cmake_minimum_required(VERSION 3.20)
project(cognator VERSION 0.1.0 LANGUAGES C)

option(COGNATOR_BUILD_TESTS "Build native C tests" ON)
option(COGNATOR_BUILD_CLI "Build the cognator CLI" ON)
option(COGNATOR_BUILD_PYTHON_TESTS "Build and run Python wrapper tests" ON)
set(COGNATOR_PYTHON_EXECUTABLE "python3" CACHE STRING "Python executable for wrapper tests and packaging smoke checks")
set(COGNATOR_ENABLE_SANITIZER "" CACHE STRING "Sanitizer: address or undefined")
set_property(CACHE COGNATOR_ENABLE_SANITIZER PROPERTY STRINGS "" address undefined)

if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND COGNATOR_ENABLE_SANITIZER)
    message(FATAL_ERROR "Sanitizers are supported only with GCC and Clang")
endif()
if(COGNATOR_ENABLE_SANITIZER AND NOT COGNATOR_ENABLE_SANITIZER MATCHES "^(address|undefined)$")
    message(FATAL_ERROR "COGNATOR_ENABLE_SANITIZER must be address or undefined")
endif()

# merkmal comes from third_party/merkmal, pinned to the commit in its PINNED
# file. It is not a floating dependency and not a sibling checkout: cognator's
# clustering thresholds are calibrated against one segment-distance scale, and
# that scale moves with merkmal's feature inventory. See third_party/merkmal
# and docs/merkmal_pin.md.
#
# COGNATOR_MERKMAL_SOURCE_DIR still points the build at some other merkmal
# checkout, because a packager with a system merkmal has a legitimate reason to
# and because the recalibration work needs it. It is an override, not the
# normal path, and nothing in this repository's committed numbers holds under
# it.
option(COGNATOR_REQUIRE_UTF8PROC "Fail configure if libutf8proc is absent" OFF)
# Off by default: a warning in a consumer's toolchain is not a reason to deny
# them a build. scripts/check.sh and CI turn it on, which is where the gate
# belongs.
option(COGNATOR_WERROR "Treat compiler warnings as errors" OFF)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(UTF8PROC QUIET IMPORTED_TARGET libutf8proc)
endif()
if(COGNATOR_REQUIRE_UTF8PROC AND NOT UTF8PROC_FOUND)
    message(FATAL_ERROR
        "libutf8proc was not found and COGNATOR_REQUIRE_UTF8PROC is ON. "
        "Install it, or turn the option off to build merkmal's fallback "
        "normalization instead -- but note the two normalize differently.")
endif()

# Directory-scoped rather than set on the cognator target. They were on that
# target alone, which meant the CLI, the C tests and the WebAssembly shim --
# every line of C in this repository that is not the library -- compiled with
# no warnings at all.
if(MSVC)
    add_compile_options(/W4)
    if(COGNATOR_WERROR)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(COGNATOR_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

set(COGNATOR_MERKMAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/merkmal"
    CACHE PATH "merkmal C sources; defaults to the pinned copy in third_party/")

if(EXISTS "${COGNATOR_MERKMAL_SOURCE_DIR}/sources.cmake")
    include("${COGNATOR_MERKMAL_SOURCE_DIR}/sources.cmake")
elseif(EXISTS "${COGNATOR_MERKMAL_SOURCE_DIR}/CMakeLists.txt")
    # An external merkmal checkout, which brings its own build. Used by the
    # recalibration work and by packagers; see docs/merkmal_pin.md.
    set(MERKMAL_BUILD_TESTS OFF CACHE BOOL "Build merkmal tests" FORCE)
    add_subdirectory("${COGNATOR_MERKMAL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/merkmal")
    set(MERKMAL_PINNED_SOURCES "")
    set(MERKMAL_PINNED_INCLUDE_DIRS "")
else()
    message(FATAL_ERROR "no merkmal sources at ${COGNATOR_MERKMAL_SOURCE_DIR}")
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
if(MERKMAL_PINNED_SOURCES)
    set(COGNATOR_CONFIG_MERKMAL_DEPENDENCY "")
    target_sources(cognator PRIVATE ${MERKMAL_PINNED_SOURCES})
    target_include_directories(cognator PRIVATE ${MERKMAL_PINNED_INCLUDE_DIRS})
    set_source_files_properties(${MERKMAL_PINNED_SOURCES} PROPERTIES
        COMPILE_DEFINITIONS MERKMAL_BUILDING_LIBRARY)
    if(UTF8PROC_FOUND)
        set_property(SOURCE ${MERKMAL_PINNED_SOURCES}
            APPEND PROPERTY COMPILE_DEFINITIONS MK_HAVE_UTF8PROC=1)
        target_include_directories(cognator PRIVATE ${UTF8PROC_INCLUDE_DIRS})
        # The bare name, not PkgConfig::UTF8PROC. libcognator is static by
        # default, so even a PRIVATE dependency is recorded in the exported
        # link interface, and an imported target there is a name the consumer's
        # project has never defined -- which is a configure error in their
        # build, from a find_package of ours. A plain -lutf8proc resolves the
        # same way anywhere the library is installed.
        target_link_libraries(cognator PRIVATE ${UTF8PROC_LIBRARIES})
        set(COGNATOR_PC_LIBS_PRIVATE "-lutf8proc")
    endif()
else()
    set(COGNATOR_CONFIG_MERKMAL_DEPENDENCY
        "include(CMakeFindDependencyMacro)\nfind_dependency(merkmal CONFIG)")
    target_link_libraries(cognator PUBLIC merkmal::merkmal)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(cognator PRIVATE m)
endif()
set_target_properties(cognator PROPERTIES C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_compile_definitions(cognator PRIVATE COGNATOR_BUILDING_LIBRARY)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(cognator PUBLIC COGNATOR_SHARED)
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
        if(COGNATOR_BUILD_PYTHON_TESTS)
            add_test(NAME python_api
                COMMAND ${CMAKE_COMMAND}
                    -DPython3_EXECUTABLE=${COGNATOR_PYTHON_EXECUTABLE}
                    -DCOGNATOR_CLI=$<TARGET_FILE:cognator_cli>
                    -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_python_api.cmake)
            set_tests_properties(python_api PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python;COGNATOR_CLI=$<TARGET_FILE:cognator_cli>")
            add_test(NAME python_install
                COMMAND ${CMAKE_COMMAND}
                    -DPython3_EXECUTABLE=${COGNATOR_PYTHON_EXECUTABLE}
                    -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                    -DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_python_install
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_python_install.cmake)
        endif()
    endif()
endif()
