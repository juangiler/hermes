cmake_minimum_required(VERSION 3.20)
project(hermes_core C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CORE REQUIRED IMPORTED_TARGET libarchive fuse3 openssl)
find_package(Qt6 REQUIRED COMPONENTS Core DBus Test)

# src/hermes_core.c is a unity source: the same engine files the SDL app
# compiles, but without SDL or the UI. build.sh remains authoritative for the
# SDL binary; this target only serves headless tests and future frontends.
add_library(hermes_core STATIC src/hermes_core.c)
target_include_directories(hermes_core PUBLIC src)
target_compile_options(hermes_core PRIVATE
    -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format-truncation)
target_link_libraries(hermes_core PUBLIC PkgConfig::CORE m dl pthread)

add_executable(hermes-core-selftest tools/core-selftest.c)
target_compile_options(hermes-core-selftest PRIVATE
    -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format-truncation)
target_link_libraries(hermes-core-selftest PRIVATE hermes_core)

# A real C++ consumer compiling only against the public headers. This is the
# check that the static archive itself, not a duplicate unity build, links.
add_executable(hermes-cpp-probe tools/core-api-probe.cpp)
target_compile_options(hermes-cpp-probe PRIVATE -Wall -Wextra -Werror)
target_link_libraries(hermes-cpp-probe PRIVATE hermes_core)

# Unlike the engine harness, this suite links the archive: no engine sources
# are included here, so the hermes_core object file must be pulled in.
add_executable(hermes-core-abi-selftest tools/core-abi-selftest.c)
target_compile_options(hermes-core-abi-selftest PRIVATE -Wall -Wextra -Werror -Wno-format-truncation)
target_link_libraries(hermes-core-abi-selftest PRIVATE hermes_core)

add_subdirectory(portals)
