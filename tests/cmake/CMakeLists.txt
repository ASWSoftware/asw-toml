cmake_minimum_required(VERSION 3.20)

project(ASWTomlTests LANGUAGES CXX)

# The library needs C++20, the default; a later standard checks that the library and the tests also build with it.
set(ASWTOML_CXX_STANDARD 20 CACHE STRING
    "The C++ standard to build the ASWToml unit tests (and library) with: 20, 23 or 26")
set_property(CACHE ASWTOML_CXX_STANDARD PROPERTY STRINGS 20 23 26)
if(NOT ASWTOML_CXX_STANDARD MATCHES "^(20|23|26)$")
    message(FATAL_ERROR "ASWTOML_CXX_STANDARD must be 20, 23 or 26 (got '${ASWTOML_CXX_STANDARD}')")
endif()

set(CMAKE_CXX_STANDARD ${ASWTOML_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASWTOML_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(ASWTOML_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tests")
set(ASWUNITTESTS_ROOT "${ASWTOML_ROOT}/third_party/asw-unit-tests")

# The ASWUnitTests framework is a git submodule. Fail with instructions rather than falling back to a download, so a
# build (CI included) always uses exactly the commit the submodule pins.
if(NOT EXISTS "${ASWUNITTESTS_ROOT}/src/ASWUnitTests_Sources.cmake")
    message(FATAL_ERROR
        "The ASWUnitTests submodule is missing from: ${ASWUNITTESTS_ROOT}\n"
        "From the repository root, run:\n"
        "    git submodule update --init\n"
        "or clone with:\n"
        "    git clone --recurse-submodules https://github.com/ASWSoftware/asw-toml.git")
endif()

include("${ASWUNITTESTS_ROOT}/src/ASWUnitTests_Sources.cmake")

# The ASWToml::ASWToml library target, from the repository root's CMakeLists.txt. It already exists when the root
# project builds the tests (ASWTOML_BUILD_TESTS).
if(NOT TARGET ASWToml::ASWToml)
    set(ASWTOML_WARNINGS ON)
    add_subdirectory("${ASWTOML_ROOT}" aswtoml)
endif()

add_executable(ASWTomlTests
    ${ASWUNITTESTS_SOURCES}
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Version.cpp"
)

target_include_directories(ASWTomlTests PRIVATE
    "${ASWUNITTESTS_SOURCE_DIR}"
    "${ASWTOML_TESTS_DIR}"
)

target_link_libraries(ASWTomlTests PRIVATE ASWToml::ASWToml)

# ASWUnitTests' --test-timeout-seconds uses std::thread/std::future, which needs an explicit threading library on
# Linux and some MinGW-w64 distributions.
find_package(Threads REQUIRED)
target_link_libraries(ASWTomlTests PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(ASWTomlTests PRIVATE /W4 /Zc:__cplusplus)
else()
    target_compile_options(ASWTomlTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MINGW)
    # Statically link the MinGW runtime so the executable launches without MinGW's bin directory on PATH (e.g. on a
    # CI runner).
    target_link_options(ASWTomlTests PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

set_target_properties(ASWTomlTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
