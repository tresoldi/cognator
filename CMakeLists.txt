cmake_minimum_required(VERSION 3.20)
project(cognator VERSION 0.1.0 LANGUAGES C)

option(COGNATOR_BUILD_TESTS "Build native C tests" ON)
option(COGNATOR_BUILD_CLI "Build the cognator CLI" ON)
set(COGNATOR_ENABLE_SANITIZER "" CACHE STRING "Sanitizer: address or undefined")
set_property(CACHE COGNATOR_ENABLE_SANITIZER PROPERTY STRINGS "" address undefined)

if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND COGNATOR_ENABLE_SANITIZER)
    message(FATAL_ERROR "Sanitizers are supported only with GCC and Clang")
endif()
if(COGNATOR_ENABLE_SANITIZER AND NOT COGNATOR_ENABLE_SANITIZER MATCHES "^(address|undefined)$")
    message(FATAL_ERROR "COGNATOR_ENABLE_SANITIZER must be address or undefined")
endif()

add_library(cognator src/options.c)
add_library(cognator::cognator ALIAS cognator)
target_compile_features(cognator PUBLIC c_std_99)
target_include_directories(cognator PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
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
    if(COGNATOR_BUILD_CLI)
        add_test(NAME cli_version COMMAND cognator_cli version)
        set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "cognator 0.1.0")
    endif()
endif()
