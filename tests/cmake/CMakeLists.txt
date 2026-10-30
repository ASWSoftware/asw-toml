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
set(ASWTOML_TOML_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../toml-test")
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

# The test modules and helpers, which both the console runner and, on Windows, the Win32 GUI runner run (see
# ASWTomlTestObjects below).
set(ASWTOML_TEST_SOURCES
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Base.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Converters.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Converters_Containers.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Converters_Fuzz.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Converters_Structs.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Document.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Edits.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_File.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Interface.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Json.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Options.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Path.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Scalars.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Settings.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Types.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Utils.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Value.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Version.cpp"
    "${ASWTOML_TESTS_DIR}/Test_ASWToml_Writer.cpp"
    "${ASWTOML_TESTS_DIR}/UT_Helper_Converters.cpp"
    "${ASWTOML_TESTS_DIR}/UT_Helper_FailingAlloc.cpp"
    "${ASWTOML_TESTS_DIR}/UT_Helper_Json.cpp"
    "${ASWTOML_TESTS_DIR}/UT_Helper_TempDir.cpp"
    "${ASWTOML_TESTS_DIR}/UT_Helper_Threads.cpp"
    "${ASWTOML_TESTS_DIR}/UT_Helper_TomlTest.cpp"
    "${ASWTOML_TOML_TEST_DIR}/TomlTest_Corpus.cpp"
)

# ASWUnitTests' --test-timeout-seconds uses std::thread/std::future, which needs an explicit threading library on
# Linux and some MinGW-w64 distributions.
find_package(Threads REQUIRED)

# The warnings the tests and the test runners compile with.
function(aswtoml_set_test_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# The tests, compiled once for both test runners. An object library, whose object files are all linked into each
# runner, rather than a static library, from which a linker only takes the object files something refers to: nothing
# refers to a test module, since each registers itself, so every test would be left out.
add_library(ASWTomlTestObjects OBJECT ${ASWTOML_TEST_SOURCES})

target_include_directories(ASWTomlTestObjects PRIVATE
    "${ASWUNITTESTS_SOURCE_DIR}"
    "${ASWTOML_TESTS_DIR}"
    "${ASWTOML_TOML_TEST_DIR}"
)

# PUBLIC, so the runners link them too, and compile their own sources with ASWToml's options (ASWTOML_SANITIZE's).
target_link_libraries(ASWTomlTestObjects PUBLIC ASWToml::ASWToml Threads::Threads)
aswtoml_set_test_warnings(ASWTomlTestObjects)

# The converter tests instantiate thousands of templates, so an unoptimized build of them has more sections than a
# Windows object file holds in its usual format (MSVC's error C1128); the big object format holds them. MSVC needs
# the option, while Clang and GNU as (binutils 2.47) switch to that format by themselves when a file needs it.
if(MSVC)
    target_compile_options(ASWTomlTestObjects PRIVATE /bigobj)
endif()

# The settings both test runners share.
function(aswtoml_configure_test_runner target)
    target_link_libraries(${target} PRIVATE ASWTomlTestObjects)
    aswtoml_set_test_warnings(${target})

    if(MINGW)
        # Statically link the MinGW runtime so the executable launches without MinGW's bin directory on PATH (e.g. on
        # a CI runner).
        target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()

    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endfunction()

add_executable(ASWTomlTests ${ASWUNITTESTS_SOURCES})

target_include_directories(ASWTomlTests PRIVATE "${ASWUNITTESTS_SOURCE_DIR}")
aswtoml_configure_test_runner(ASWTomlTests)

if(WIN32)
    # The ASWUnitTests icon, which the resource compiler finds through the include path.
    target_sources(ASWTomlTests PRIVATE "${ASWUNITTESTS_ROOT}/resource/ASWUnitTests_Icon.rc")
    target_include_directories(ASWTomlTests PRIVATE "${ASWUNITTESTS_ROOT}/resource")

    # The Win32 GUI runner: the same tests in a window. Its WinMain takes the place of the console runner's main.cpp,
    # and its sources list has the icon.
    include("${ASWUNITTESTS_ROOT}/win32/gui/src/ASWUnitTests_Win32GUI_Sources.cmake")

    add_executable(ASWTomlTests_Win32_GUI WIN32 ${ASWUNITTESTS_WIN32_GUI_SOURCES})

    target_include_directories(ASWTomlTests_Win32_GUI PRIVATE ${ASWUNITTESTS_WIN32_GUI_INCLUDE_DIRS})
    target_link_libraries(ASWTomlTests_Win32_GUI PRIVATE ${ASWUNITTESTS_WIN32_GUI_LIBRARIES})
    target_link_options(ASWTomlTests_Win32_GUI PRIVATE ${ASWUNITTESTS_WIN32_GUI_LINK_OPTIONS})
    aswtoml_configure_test_runner(ASWTomlTests_Win32_GUI)
endif()

# The decoder that toml-test's own runner starts for each test file (see tests/toml-test/README.md), built with the
# same options, next to the tests.
add_executable(ASWTomlTestDecoder
    "${ASWTOML_TOML_TEST_DIR}/decoder/main.cpp"
)

target_link_libraries(ASWTomlTestDecoder PRIVATE ASWToml::ASWToml)

if(MSVC)
    target_compile_options(ASWTomlTestDecoder PRIVATE /W4 /Zc:__cplusplus)
else()
    target_compile_options(ASWTomlTestDecoder PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MINGW)
    target_link_options(ASWTomlTestDecoder PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

set_target_properties(ASWTomlTestDecoder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
