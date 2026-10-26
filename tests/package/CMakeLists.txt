cmake_minimum_required(VERSION 3.20)

project(ASWTomlPackageCheck LANGUAGES CXX)

# Checks the installed CMake package: builds Check_Package.cpp (the version) and Check_Usage.cpp (every header, a
# settings class, a text, a file and a document) against an ASWToml installed with `cmake --install` (point
# CMAKE_PREFIX_PATH at the install prefix), and `ctest` runs them. See the README's CMake section.
find_package(ASWToml CONFIG REQUIRED)

add_executable(ASWTomlPackageCheck
    "${CMAKE_CURRENT_SOURCE_DIR}/Check_Package.cpp"
)

add_executable(ASWTomlUsageCheck
    "${CMAKE_CURRENT_SOURCE_DIR}/Check_Usage.cpp"
)

# The version the package's version file reported, which the check compares with the installed headers' version
target_compile_definitions(ASWTomlPackageCheck PRIVATE "ASWTOML_PACKAGE_VERSION=\"${ASWToml_VERSION}\"")

foreach(check ASWTomlPackageCheck ASWTomlUsageCheck)
    # No C++ standard of their own: the package's target must give C++20 to the code that links to it.
    target_link_libraries(${check} PRIVATE ASWToml::ASWToml)

    if(MSVC)
        target_compile_options(${check} PRIVATE /W4 /Zc:__cplusplus)
    else()
        target_compile_options(${check} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(MINGW)
        # Statically link the MinGW runtime so the executable launches without MinGW's bin directory on PATH (e.g. on
        # a CI runner).
        target_link_options(${check} PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()
endforeach()

enable_testing()
add_test(NAME ASWTomlPackageCheck COMMAND ASWTomlPackageCheck)
add_test(NAME ASWTomlUsageCheck COMMAND ASWTomlUsageCheck)
