cmake_minimum_required(VERSION 3.10)
project(CxxScript VERSION 0.1.5 LANGUAGES CXX)

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
    add_compile_options(/W4 /sdl /guard:cf)
else()
    add_compile_options(-Wall -Wextra -fstack-protector-strong)
    add_compile_options(
        "$<$<CONFIG:Debug>:-O0;-g3>"
        "$<$<CONFIG:RelWithDebInfo>:-O2;-g>"
        "$<$<CONFIG:Release>:-O3>"
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Harden against common memory-corruption exploitation techniques:
        # buffer-overflow checks (_FORTIFY_SOURCE), full RELRO, and PIE.
        # _FORTIFY_SOURCE needs an optimization level > 0, which every
        # non-Debug config above already provides.
        add_compile_definitions("$<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>")
        add_compile_options(-fPIE)
        add_link_options(-pie -Wl,-z,relro,-z,now)
    endif()
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

set(CXXSCRIPT_TEST_SOURCES
    test_lexer
    test_parser
    test_interpreter
    test_error_handling
    test_comprehensive
    test_external_functions
    test_multi_file
    test_string_concat
    test_real_world_app
    test_escape_sequences
    test_control_flow
    test_bitwise
    test_arrays
    test_external_variables
    test_float
    test_char
)

set(CXXSCRIPT_TEST_TARGETS "")
foreach(test_name IN LISTS CXXSCRIPT_TEST_SOURCES)
    add_executable(${test_name} ${TESTS_DIR}/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE CxxScript GTest::gtest_main)
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )
    list(APPEND CXXSCRIPT_TEST_TARGETS ${test_name})
endforeach()

# Google Test integration
include(GoogleTest)
foreach(test_target IN LISTS CXXSCRIPT_TEST_TARGETS)
    gtest_discover_tests(${test_target} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

# Custom target to run all tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS ${CXXSCRIPT_TEST_TARGETS}
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
