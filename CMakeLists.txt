cmake_minimum_required(VERSION 3.10)
project(CxxScript VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Options let consumers (e.g. Conan, downstream CMake projects) build just the
# library without pulling in GoogleTest or building the example binaries.
option(CXXSCRIPT_BUILD_TESTS "Build the CxxScript test suite" ON)
option(CXXSCRIPT_BUILD_EXAMPLES "Build the CxxScript example/demo executables" ON)

# Compiler flags
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -O2)
endif()

if(CXXSCRIPT_BUILD_TESTS)
    # Fetch Google Test
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )

    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# Project structure
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests)
set(EXAMPLES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/examples)
set(SCRIPTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/scripts)

# Library source files
set(LIBRARY_SOURCES
    ${SRC_DIR}/Token.cpp
    ${SRC_DIR}/DataTypes.cpp
    ${SRC_DIR}/Lexer.cpp
    ${SRC_DIR}/Parser.cpp
    ${SRC_DIR}/Interpreter.cpp
    ${SRC_DIR}/ScriptManager.cpp
)

# Library header files
set(LIBRARY_HEADERS
    ${INCLUDE_DIR}/Token.h
    ${INCLUDE_DIR}/DataTypes.h
    ${INCLUDE_DIR}/Lexer.h
    ${INCLUDE_DIR}/Parser.h
    ${INCLUDE_DIR}/AST.h
    ${INCLUDE_DIR}/Interpreter.h
    ${INCLUDE_DIR}/ScriptManager.h
)

# Create static library
add_library(CxxScript STATIC ${LIBRARY_SOURCES} ${LIBRARY_HEADERS})

# Include directories for the library
target_include_directories(CxxScript PUBLIC
    $<BUILD_INTERFACE:${INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# Set library output directory
set_target_properties(CxxScript PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

if(CXXSCRIPT_BUILD_EXAMPLES)
# Examples
add_executable(example_usage ${EXAMPLES_DIR}/example_usage.cpp)
target_link_libraries(example_usage PRIVATE CxxScript)
set_target_properties(example_usage PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Demo executables
add_executable(demo_error_detection ${EXAMPLES_DIR}/demo_error_detection.cpp)
target_link_libraries(demo_error_detection PRIVATE CxxScript)
set_target_properties(demo_error_detection PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_executable(demo_whitespace ${EXAMPLES_DIR}/demo_whitespace.cpp)
target_link_libraries(demo_whitespace PRIVATE CxxScript)
set_target_properties(demo_whitespace PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_executable(demo_uninitialized ${EXAMPLES_DIR}/demo_uninitialized.cpp)
target_link_libraries(demo_uninitialized PRIVATE CxxScript)
set_target_properties(demo_uninitialized PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Custom target to run example
add_custom_target(run_example
    COMMAND ${CMAKE_BINARY_DIR}/bin/example_usage
    DEPENDS example_usage
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
endif()

if(CXXSCRIPT_BUILD_TESTS)
# Enable testing
enable_testing()

# Test executables with Google Test
add_executable(test_lexer ${TESTS_DIR}/test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_lexer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(test_parser ${TESTS_DIR}/test_parser.cpp)
target_link_libraries(test_parser PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_parser PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(test_interpreter ${TESTS_DIR}/test_interpreter.cpp)
target_link_libraries(test_interpreter PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_interpreter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(test_error_handling ${TESTS_DIR}/test_error_handling.cpp)
target_link_libraries(test_error_handling PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_error_handling PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(test_comprehensive ${TESTS_DIR}/test_comprehensive.cpp)
target_link_libraries(test_comprehensive PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_comprehensive PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(test_external_functions ${TESTS_DIR}/test_external_functions.cpp)
target_link_libraries(test_external_functions PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_external_functions PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(test_multi_file ${TESTS_DIR}/test_multi_file.cpp)
target_link_libraries(test_multi_file PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_multi_file PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(test_string_concat ${TESTS_DIR}/test_string_concat.cpp)
target_link_libraries(test_string_concat PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_string_concat PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(test_real_world_app ${TESTS_DIR}/test_real_world_app.cpp)
target_link_libraries(test_real_world_app PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_real_world_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(test_escape_sequences ${TESTS_DIR}/test_escape_sequences.cpp)
target_link_libraries(test_escape_sequences PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_escape_sequences PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(test_control_flow ${TESTS_DIR}/test_control_flow.cpp)
target_link_libraries(test_control_flow PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_control_flow PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(test_bitwise ${TESTS_DIR}/test_bitwise.cpp)
target_link_libraries(test_bitwise PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_bitwise PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(test_arrays ${TESTS_DIR}/test_arrays.cpp)
target_link_libraries(test_arrays PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_arrays PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

add_executable(test_external_variables ${TESTS_DIR}/test_external_variables.cpp)
target_link_libraries(test_external_variables PRIVATE CxxScript GTest::gtest_main)
set_target_properties(test_external_variables PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# Google Test integration
include(GoogleTest)
gtest_discover_tests(test_lexer WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
gtest_discover_tests(test_parser WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
gtest_discover_tests(test_interpreter WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
gtest_discover_tests(test_error_handling WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
gtest_discover_tests(test_comprehensive WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
gtest_discover_tests(test_external_functions WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
gtest_discover_tests(test_multi_file WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
gtest_discover_tests(test_string_concat WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
gtest_discover_tests(test_real_world_app WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
gtest_discover_tests(test_escape_sequences WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
gtest_discover_tests(test_control_flow WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
gtest_discover_tests(test_bitwise WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
gtest_discover_tests(test_arrays WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
gtest_discover_tests(test_external_variables WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Custom target to run all tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS test_lexer test_parser test_interpreter test_error_handling
    test_comprehensive test_external_functions test_multi_file
    test_string_concat test_real_world_app test_escape_sequences
    test_control_flow test_bitwise test_arrays test_external_variables
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
endif()

# Installation rules
set(CXXSCRIPT_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/CxxScript")
set(CXXSCRIPT_INSTALL_SCRIPTDIR "${CMAKE_INSTALL_DATADIR}/CxxScript/scripts")
set(CXXSCRIPT_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/CxxScript")

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CxxScriptConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CxxScriptConfig.cmake
    INSTALL_DESTINATION ${CXXSCRIPT_INSTALL_CMAKEDIR}
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_DATADIR
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/CxxScriptConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(TARGETS CxxScript
    EXPORT CxxScriptTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${LIBRARY_HEADERS}
    DESTINATION ${CXXSCRIPT_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${SCRIPTS_DIR}/
    DESTINATION ${CXXSCRIPT_INSTALL_SCRIPTDIR}
    FILES_MATCHING PATTERN "*.script"
)

# Export targets
install(EXPORT CxxScriptTargets
    FILE CxxScriptTargets.cmake
    NAMESPACE CxxScript::
    DESTINATION ${CXXSCRIPT_INSTALL_CMAKEDIR}
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CxxScriptConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CxxScriptConfigVersion.cmake
    DESTINATION ${CXXSCRIPT_INSTALL_CMAKEDIR}
)

set(CPACK_PACKAGE_NAME "CxxScript")
set(CPACK_PACKAGE_VENDOR "CxxScript Project")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "CxxScript Maintainers")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")

include(CPack)

# Print configuration
message(STATUS "")
message(STATUS "╔════════════════════════════════════════════════════════╗")
message(STATUS "║              CxxScript Configuration                  ║")
message(STATUS "╚════════════════════════════════════════════════════════╝")
message(STATUS "  C++ Standard:        ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type:          ${CMAKE_BUILD_TYPE}")
message(STATUS "  Source directory:    ${SRC_DIR}")
message(STATUS "  Include directory:   ${INCLUDE_DIR}")
message(STATUS "  Tests directory:     ${TESTS_DIR}")
message(STATUS "  Examples directory:  ${EXAMPLES_DIR}")
message(STATUS "  Scripts directory:   ${SCRIPTS_DIR}")
message(STATUS "  Library output:      ${CMAKE_BINARY_DIR}/lib")
message(STATUS "  Binary output:       ${CMAKE_BINARY_DIR}/bin")
message(STATUS "  Tests output:        ${CMAKE_BINARY_DIR}/tests")
message(STATUS "")
