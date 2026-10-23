cmake_minimum_required(VERSION 3.20)

# The version lives only in ASWToml/ASWToml_Version.h, so the project version comes from its ASWTOML_VERSION_MAJOR,
# _MINOR, and _PATCH macros (a CMake version has no pre-release part).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/ASWToml/ASWToml_Version.h" ASWTOML_VERSION_DEFINES
    REGEX "^#define ASWTOML_VERSION_(MAJOR|MINOR|PATCH) ")
foreach(versionPart MAJOR MINOR PATCH)
    if(NOT ASWTOML_VERSION_DEFINES MATCHES "#define ASWTOML_VERSION_${versionPart} ([0-9]+)")
        message(FATAL_ERROR "ASWToml/ASWToml_Version.h doesn't define ASWTOML_VERSION_${versionPart} as a number")
    endif()
    set(ASWTOML_VERSION_${versionPart} "${CMAKE_MATCH_1}")
endforeach()

project(ASWToml
    VERSION ${ASWTOML_VERSION_MAJOR}.${ASWTOML_VERSION_MINOR}.${ASWTOML_VERSION_PATCH}
    DESCRIPTION "A thread-safe, comment-preserving C++20 TOML file reader/writer for Windows and Linux"
    HOMEPAGE_URL "https://github.com/ASWSoftware/asw-toml"
    LANGUAGES CXX
)

# Building ASWToml itself (cmake -S .) rather than consuming it through add_subdirectory() or FetchContent.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ASWTOML_IS_TOP_LEVEL ON)
else()
    set(ASWTOML_IS_TOP_LEVEL OFF)
endif()

option(ASWTOML_BUILD_TESTS "Build the ASWToml unit tests (tests/cmake; needs the asw-unit-tests submodule)" OFF)
option(ASWTOML_INSTALL "Generate the install rules for the ASWToml library, headers, and CMake package files"
    ${ASWTOML_IS_TOP_LEVEL})
option(ASWTOML_WARNINGS "Compile the ASWToml sources with extra warnings" ${ASWTOML_IS_TOP_LEVEL})

set(ASWTOML_SANITIZE "" CACHE STRING
    "Build ASWToml and everything that links to it with a sanitizer: thread, or address (with undefined behavior)")
set_property(CACHE ASWTOML_SANITIZE PROPERTY STRINGS "" thread address)

add_library(ASWToml STATIC
    ASWToml/ASWToml_File.cpp
    ASWToml/ASWToml_File.h
    ASWToml/ASWToml_Options.cpp
    ASWToml/ASWToml_Options.h
    ASWToml/ASWToml_Types.cpp
    ASWToml/ASWToml_Types.h
    ASWToml/ASWToml_Utils.cpp
    ASWToml/ASWToml_Utils.h
    ASWToml/ASWToml_Value.cpp
    ASWToml/ASWToml_Value.h
    ASWToml/ASWToml_Version.cpp
    ASWToml/ASWToml_Version.h
)
add_library(ASWToml::ASWToml ALIAS ASWToml)

# C++20 is the minimum. A consumer may compile at a later standard.
target_compile_features(ASWToml PUBLIC cxx_std_20)
# A Debug build is named ASWTomld, so Debug and Release can be installed to the same prefix.
set_target_properties(ASWToml PROPERTIES
    CXX_EXTENSIONS OFF
    DEBUG_POSTFIX d
)

# The installed target's include folder comes from install(TARGETS) below.
target_include_directories(ASWToml PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ASWToml>")

# The library's locks: std::mutex and std::shared_mutex need the threading library on Linux with an older glibc (and
# on some MinGW-w64 distributions). PUBLIC, since a static library's dependencies are linked by its consumers.
find_package(Threads REQUIRED)
target_link_libraries(ASWToml PUBLIC Threads::Threads)

if(ASWTOML_WARNINGS)
    if(MSVC)
        target_compile_options(ASWToml PRIVATE /W4)
    else()
        # -Wshadow matches /W4's C4456-C4459 (a declaration that hides another)
        target_compile_options(ASWToml PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    endif()
endif()

# A sanitizer must instrument the whole program (ThreadSanitizer misses races in, or misreports them through,
# uninstrumented code), so the flags are PUBLIC: they reach the compile and link of every target linking to ASWToml.
if(ASWTOML_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "ASWTOML_SANITIZE is supported only with GCC and Clang")
    endif()

    if(ASWTOML_SANITIZE STREQUAL "thread")
        set(ASWTOML_SANITIZE_FLAGS -fsanitize=thread)
    elseif(ASWTOML_SANITIZE STREQUAL "address")
        # Stop at the first undefined behavior report, as AddressSanitizer does, so the run fails.
        set(ASWTOML_SANITIZE_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined)
    else()
        message(FATAL_ERROR "ASWTOML_SANITIZE must be empty, thread, or address (got '${ASWTOML_SANITIZE}')")
    endif()

    # Debug information and frame pointers give the sanitizer reports complete call stacks.
    target_compile_options(ASWToml PUBLIC ${ASWTOML_SANITIZE_FLAGS} -g -fno-omit-frame-pointer)
    target_link_options(ASWToml PUBLIC ${ASWTOML_SANITIZE_FLAGS})
endif()

# After `cmake --install`, find_package(ASWToml CONFIG) gives the ASWToml::ASWToml target. The headers are installed
# to <prefix>/include/ASWToml, the installed target's include folder, so code includes them the same way as from the
# sources (e.g. #include "ASWToml_Version.h").
if(ASWTOML_INSTALL)
    include(CMakePackageConfigHelpers)
    include(GNUInstallDirs)

    set(ASWTOML_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/ASWToml")

    install(TARGETS ASWToml
        EXPORT ASWTomlTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ASWToml"
    )

    # Every header is public.
    get_target_property(ASWTOML_HEADERS ASWToml SOURCES)
    list(FILTER ASWTOML_HEADERS INCLUDE REGEX "\\.h$")
    install(FILES ${ASWTOML_HEADERS} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ASWToml")

    install(EXPORT ASWTomlTargets
        NAMESPACE ASWToml::
        DESTINATION "${ASWTOML_INSTALL_CMAKEDIR}"
    )

    configure_package_config_file(cmake/ASWTomlConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/ASWTomlConfig.cmake"
        INSTALL_DESTINATION "${ASWTOML_INSTALL_CMAKEDIR}"
    )

    # As in Semantic Versioning, find_package(ASWToml 1.2) accepts version 1.2.0 or any later 1.x, but not 2.0.
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/ASWTomlConfigVersion.cmake"
        VERSION "${ASWToml_VERSION}"
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/ASWTomlConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ASWTomlConfigVersion.cmake"
        DESTINATION "${ASWTOML_INSTALL_CMAKEDIR}"
    )
endif()

if(ASWTOML_BUILD_TESTS)
    add_subdirectory(tests/cmake tests)
endif()
