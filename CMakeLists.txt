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
    # The pin's own record of what it is. Compiled into the library so a result
    # can name the phonology that produced it.
    file(READ "${COGNATOR_MERKMAL_SOURCE_DIR}/PINNED" _pinned)
    string(REGEX MATCH "version ([^\n]+)" _m "${_pinned}")
    set(COGNATOR_MERKMAL_VERSION "${CMAKE_MATCH_1}")
    string(REGEX MATCH "commit ([0-9a-f]+)" _m "${_pinned}")
    string(SUBSTRING "${CMAKE_MATCH_1}" 0 12 COGNATOR_MERKMAL_COMMIT)
    unset(_pinned)
    unset(_m)
elseif(EXISTS "${COGNATOR_MERKMAL_SOURCE_DIR}/CMakeLists.txt")
    # An external merkmal checkout, which brings its own build. Used by the
    # recalibration work and by packagers; see docs/merkmal_pin.md.
    set(MERKMAL_BUILD_TESTS OFF CACHE BOOL "Build merkmal tests" FORCE)
    add_subdirectory("${COGNATOR_MERKMAL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/merkmal")
    set(MERKMAL_PINNED_SOURCES "")
    set(MERKMAL_PINNED_INCLUDE_DIRS "")
    # project() sets merkmal_VERSION in the subdirectory's own scope, which
    # does not write back here, so the parent would see an empty string. Lift
    # it out explicitly.
    get_directory_property(COGNATOR_MERKMAL_VERSION
        DIRECTORY "${COGNATOR_MERKMAL_SOURCE_DIR}"
        DEFINITION merkmal_VERSION)
    execute_process(
        COMMAND git -C "${COGNATOR_MERKMAL_SOURCE_DIR}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE COGNATOR_MERKMAL_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT COGNATOR_MERKMAL_COMMIT)
        set(COGNATOR_MERKMAL_COMMIT "unknown")
    endif()
else()
    message(FATAL_ERROR "no merkmal sources at ${COGNATOR_MERKMAL_SOURCE_DIR}")
endif()

# Guessing would compile an unearned provenance into every result, and an empty
# string is how regulae's went unnoticed, so neither is allowed to build.
if(NOT COGNATOR_MERKMAL_VERSION OR NOT COGNATOR_MERKMAL_COMMIT)
    message(FATAL_ERROR
        "merkmal provenance unknown (version='${COGNATOR_MERKMAL_VERSION}' "
        "commit='${COGNATOR_MERKMAL_COMMIT}'); it is compiled into the library "
        "and reported with every result")
endif()
message(STATUS "cognator: merkmal ${COGNATOR_MERKMAL_VERSION} (${COGNATOR_MERKMAL_COMMIT})")

add_library(cognator
    src/context.c
    src/data.c
    src/detect.c
    src/pairs.c
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
target_compile_definitions(cognator PRIVATE
    COGNATOR_BUILDING_LIBRARY
    COGNATOR_MERKMAL_VERSION="${COGNATOR_MERKMAL_VERSION}"
    COGNATOR_MERKMAL_COMMIT="${COGNATOR_MERKMAL_COMMIT}")
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
    # The browser engine's C, built natively so the sanitizers can see it.
    # emcc is the only other thing that compiles this file and a browser the
    # only other thing that runs it, which left the shim with no coverage at
    # all -- see tests/c/test_wasm_shim.c.
    add_executable(test_wasm_shim tests/c/test_wasm_shim.c web/cognator_wasm.c)
    target_link_libraries(test_wasm_shim PRIVATE cognator::cognator)
    target_include_directories(test_wasm_shim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME wasm_shim COMMAND test_wasm_shim)

    # Text-level, so it needs no build: fails when web/app.js offers a control
    # that apply_options in the shim does not read.
    add_test(NAME wasm_options
        COMMAND ${CMAKE_COMMAND}
            -DAPP_JS=${CMAKE_CURRENT_SOURCE_DIR}/web/app.js
            -DSHIM_C=${CMAKE_CURRENT_SOURCE_DIR}/web/cognator_wasm.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/test_wasm_options.cmake)

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
    add_executable(test_pairs tests/c/test_pairs.c)
    target_link_libraries(test_pairs PRIVATE cognator::cognator)
    add_test(NAME pairs COMMAND test_pairs
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/linguistic/partial_cognacy_compounds.tsv)
    add_executable(test_eval tests/c/test_eval.c)
    target_link_libraries(test_eval PRIVATE cognator::cognator)
    add_test(NAME eval COMMAND test_eval)
    if(COGNATOR_BUILD_CLI)
        add_test(NAME cli_version COMMAND cognator_cli version)
        set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "cognator 0.1.0")
        # Compares what the binary reports against third_party/merkmal/PINNED.
        # A test inside the library would assert the compiled-in string against
        # itself and pass on any value, the empty one included.
        add_test(NAME cli_provenance
            COMMAND ${CMAKE_COMMAND}
                -DCOGNATOR_CLI=$<TARGET_FILE:cognator_cli>
                -DPINNED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/third_party/merkmal/PINNED
                -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/linguistic/latin_romance_triplets.tsv
                -DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_cli_provenance
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/test_cli_provenance.cmake)
        # The page's curated examples, run through the CLI. Nothing else reads
        # them, so a mistyped grapheme sits in web/guide-content.js until a
        # visitor picks it from the dropdown.
        add_test(NAME web_examples
            COMMAND ${CMAKE_COMMAND}
                -DCOGNATOR_CLI=$<TARGET_FILE:cognator_cli>
                -DGUIDE_JS=${CMAKE_CURRENT_SOURCE_DIR}/web/guide-content.js
                -DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_web_examples
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/test_web_examples.cmake)

        add_test(NAME cli_detect
            COMMAND ${CMAKE_COMMAND}
                -DCOGNATOR_CLI=$<TARGET_FILE:cognator_cli>
                -DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_cli_detect
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/test_cli_detect.cmake)
        add_test(NAME cli_pairs
            COMMAND ${CMAKE_COMMAND}
                -DCOGNATOR_CLI=$<TARGET_FILE:cognator_cli>
                -DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/linguistic/partial_cognacy_compounds.tsv
                -DTEST_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_cli_pairs
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/c/test_cli_pairs.cmake)
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
        # Both Python cases build the extension through setuptools -- one with
        # `setup.py build_ext`, one with `pip install --no-build-isolation`,
        # which by design stops pip fetching the build backend itself. An
        # interpreter without setuptools therefore fails them both, and does it
        # with a BackendUnavailable traceback that says nothing about what is
        # actually missing. Python 3.12 dropped setuptools from the standard
        # install, so this is the default state of a fresh Ubuntu runner, not a
        # corner case: it is how these two tests failed on the first CI run.
        #
        # Skipping rather than failing, because a C contributor with no
        # setuptools is not doing anything wrong. CI installs it explicitly so
        # the tests run there -- a skip that silently drops coverage in CI
        # would be the worse failure.
        if(COGNATOR_BUILD_PYTHON_TESTS)
            execute_process(
                COMMAND ${COGNATOR_PYTHON_EXECUTABLE} -c "import setuptools, packaging.licenses"
                RESULT_VARIABLE _setuptools_missing
                OUTPUT_QUIET ERROR_QUIET)
            if(_setuptools_missing)
                message(WARNING
                    "${COGNATOR_PYTHON_EXECUTABLE} cannot import setuptools and "
                    "packaging.licenses, which reading pyproject.toml needs; "
                    "skipping the python_api and python_install tests. "
                    "Install them with: ${COGNATOR_PYTHON_EXECUTABLE} -m pip install "
                    "'setuptools>=77.0.0' 'packaging>=24.2' wheel")
                set(COGNATOR_BUILD_PYTHON_TESTS OFF)
            endif()
        endif()

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

            # Both of these use the source tree as scratch space: python_api
            # runs `setup.py build_ext --inplace`, which writes the extension
            # into python/cognator, and python_install pip-installs from that
            # same directory. Run concurrently -- and ctest is run with -j
            # here and in CI -- one imports the .so the other is halfway
            # through writing, which surfaces as an ImportError with no
            # message. It failed under gcc while clang and the fallback
            # profile passed the same commit, which is what a race looks like
            # from the outside.
            set_tests_properties(python_api python_install PROPERTIES
                RESOURCE_LOCK cognator_source_tree)
        endif()
    endif()
endif()
