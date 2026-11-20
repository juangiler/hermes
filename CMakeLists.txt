cmake_minimum_required(VERSION 3.20)
project(hermes_core C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Migration-only switches: both UIs are built until the SDL shell is removed.
option(HERMES_BUILD_QT_UI "Build the Kirigami frontend" ON)
set(HERMES_DEFAULT_UI "legacy" CACHE STRING "Default installed UI during migration")
set_property(CACHE HERMES_DEFAULT_UI PROPERTY STRINGS legacy kirigami)
if(NOT HERMES_DEFAULT_UI STREQUAL "legacy" AND NOT HERMES_DEFAULT_UI STREQUAL "kirigami")
  message(FATAL_ERROR "HERMES_DEFAULT_UI must be 'legacy' or 'kirigami', got '${HERMES_DEFAULT_UI}'")
endif()

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

if(HERMES_BUILD_QT_UI)
  find_package(ECM 6.0 REQUIRED NO_MODULE)
  list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test)
  find_package(KF6Kirigami REQUIRED)
  include(GNUInstallDirs)

  add_library(hermes-kirigami-ui STATIC
      qtui/hermes-qt.qrc
      qtui/directory_model.cpp
      qtui/directory_watcher.cpp
      qtui/pane_tree.cpp
      qtui/places_model.cpp
      qtui/shell_controller.cpp)
  set_target_properties(hermes-kirigami-ui PROPERTIES AUTOMOC ON)
  target_compile_features(hermes-kirigami-ui PUBLIC cxx_std_17)
  target_compile_options(hermes-kirigami-ui PRIVATE -Wall -Wextra -Werror)
  target_include_directories(hermes-kirigami-ui PUBLIC qtui)
  target_link_libraries(hermes-kirigami-ui
      PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 KF6::Kirigami
             hermes_core hermes-portals)
  # BASE strips the qtui/ directory so the runtime path is qrc:/qtui/qml/main.qml.
  qt_add_resources(hermes-kirigami-ui "hermes-qml"
      PREFIX "/qtui" BASE qtui FILES qtui/qml/main.qml qtui/qml/DirectoryPage.qml
      qtui/qml/PaneView.qml)

  add_executable(hermes-qt qtui/main.cpp)
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/app.c" HERMES_APP_VERSION_LINE
      REGEX "^#define APP_VERSION ")
  string(REGEX MATCH "\"([^\"]+)\"" HERMES_APP_VERSION_MATCH "${HERMES_APP_VERSION_LINE}")
  set(HERMES_APP_VERSION "${CMAKE_MATCH_1}")
  if(HERMES_APP_VERSION STREQUAL "")
    message(FATAL_ERROR "Could not read APP_VERSION from src/app.c")
  endif()
  target_compile_definitions(hermes-qt PRIVATE HERMES_APP_VERSION="${HERMES_APP_VERSION}")
  target_link_libraries(hermes-qt PRIVATE hermes-kirigami-ui)
  set_target_properties(hermes-qt PROPERTIES
      OUTPUT_NAME hermes-qt
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

  add_executable(hermes-qt-selftest tools/qtui-selftest.cpp)
  set_target_properties(hermes-qt-selftest PROPERTIES
      AUTOMOC ON
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  target_compile_options(hermes-qt-selftest PRIVATE -Wall -Wextra -Werror)
  target_compile_definitions(hermes-qt-selftest PRIVATE HERMES_APP_VERSION="${HERMES_APP_VERSION}")
  target_link_libraries(hermes-qt-selftest PRIVATE hermes-kirigami-ui Qt6::Test)

  pkg_check_modules(LEGACY REQUIRED IMPORTED_TARGET sdl3 freetype2 harfbuzz fontconfig vterm)
  add_executable(hermes-legacy src/main.c)
  target_compile_options(hermes-legacy PRIVATE
      -std=gnu11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format-truncation)
  target_link_libraries(hermes-legacy PRIVATE PkgConfig::LEGACY PkgConfig::CORE m dl pthread)
  set_target_properties(hermes-legacy PROPERTIES
      OUTPUT_NAME hermes-legacy
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

  install(TARGETS hermes-legacy hermes-qt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  # The 'hermes' name follows the temporary default UI; PR 5 drops this alias.
  if(HERMES_DEFAULT_UI STREQUAL "legacy")
    install(PROGRAMS $<TARGET_FILE:hermes-legacy>
        DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME hermes)
  else()
    install(PROGRAMS $<TARGET_FILE:hermes-qt>
        DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME hermes)
  endif()
endif()
