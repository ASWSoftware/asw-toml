cmake_minimum_required(VERSION 3.20)

project(ASWTomlExample LANGUAGES CXX)

# The example builds at C++20 by default, the library's minimum, to show that it needs nothing newer; 23 or 26 builds
# it (and the library) at a later standard. When the root project builds both the tests and the example, the first of
# them to set this cache variable chooses it for both (the root adds the tests first).
set(ASWTOML_CXX_STANDARD 20 CACHE STRING
    "The C++ standard to build the ASWToml example (and library) with: 20, 23 or 26")
set_property(CACHE ASWTOML_CXX_STANDARD PROPERTY STRINGS 20 23 26)
if(NOT ASWTOML_CXX_STANDARD MATCHES "^(20|23|26)$")
    message(FATAL_ERROR "ASWTOML_CXX_STANDARD must be 20, 23 or 26 (got '${ASWTOML_CXX_STANDARD}')")
endif()

set(CMAKE_CXX_STANDARD ${ASWTOML_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASWTOML_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# The ASWToml::ASWToml library target, from the repository root's CMakeLists.txt. It already exists when the root
# project builds the example (ASWTOML_BUILD_EXAMPLE).
if(NOT TARGET ASWToml::ASWToml)
    set(ASWTOML_WARNINGS ON)
    add_subdirectory("${ASWTOML_ROOT}" aswtoml)
endif()

add_executable(ASWTomlExample
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/main.cpp"
)

target_link_libraries(ASWTomlExample PRIVATE ASWToml::ASWToml)

# The example's second thread (std::thread) needs an explicit threading library on Linux with an older glibc and on
# some MinGW-w64 distributions.
find_package(Threads REQUIRED)
target_link_libraries(ASWTomlExample PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(ASWTomlExample PRIVATE /W4 /Zc:__cplusplus)
else()
    target_compile_options(ASWTomlExample PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MINGW)
    # Statically link the MinGW runtime so the executable launches without MinGW's bin directory on PATH (e.g. on a
    # CI runner).
    target_link_options(ASWTomlExample PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

set_target_properties(ASWTomlExample PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
