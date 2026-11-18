cmake_minimum_required(VERSION 3.20)
project(hermes_core C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CORE REQUIRED IMPORTED_TARGET libarchive fuse3 openssl)

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
