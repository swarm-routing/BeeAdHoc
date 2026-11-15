# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# BeeAdHoc's simulator-free core on its own: a static library and its tests,
# with no ns-3 tree. It builds on swarm-core's dual-licensed primitives
# (swarm-routing/ns3-swarm-core core/): the ports, RouteDecision, the dedup
# history and the wire helpers. The ns-3 module (ns3/, to come) compiles the
# same sources against the swarm-core contrib module and runs the same tests
# under test.py.
#
#   cmake -S core -B build/core && cmake --build build/core && ctest --test-dir build/core
#
# SWARM_CORE_DIR: a local swarm-core core/ directory (development against an
# unreleased swarm-core); empty fetches the release ../swarm-core.lock names.
# BEEADHOC_CORE_SANITIZE=ON adds ASan + UBSan, as the sanitized CI leg.
# BEEADHOC_CORE_TESTS builds the tests (ON at the top level).
# BEEADHOC_CORE_SWARM_CORE_FROM_PACKAGE=ON takes swarm-core from an installed
# package (find_package(swarm-core CONFIG), e.g. its vcpkg port) instead of
# the lock; BEEADHOC_CORE_INSTALL=ON (which needs it) adds the install rules and
# the CMake package beeadhoc-core: find_package(beeadhoc-core CONFIG REQUIRED) gives the
# imported target beeadhoc::core (swarm-routing/.github#436, the vcpkg port).
# BEEADHOC_CORE_VERSION is the package version (default: .cz.toml's, else 0.0.0).
cmake_minimum_required(VERSION 3.20)
# The package version: .cz.toml's (Commitizen bumps it at each release), or
# BEEADHOC_CORE_VERSION when core/ is built without it (a release tarball, a port).
set(BEEADHOC_CORE_VERSION "" CACHE STRING "beeadhoc-core's package version; empty: .cz.toml's")
if(BEEADHOC_CORE_VERSION)
  set(_beeadhoc_core_version "${BEEADHOC_CORE_VERSION}")
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../.cz.toml")
  file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../.cz.toml" _beeadhoc_core_version REGEX "^version = " LIMIT_COUNT 1)
  # a release bump of .cz.toml re-runs the configure step (the package version)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../.cz.toml")
  string(REGEX REPLACE "^version = \"([0-9.]+)\".*" "\\1" _beeadhoc_core_version "${_beeadhoc_core_version}")
else()
  set(_beeadhoc_core_version 0.0.0)
endif()
project(beeadhoc_core VERSION ${_beeadhoc_core_version} LANGUAGES CXX)

# The C++ standard of the ns-3 release the org supports: ns-3's own
# CMAKE_CXX_STANDARD_MINIMUM (C++23 from ns-3.46), the standard swarm-core's
# core/ targets too (swarm-routing/ns3-swarm-core#141). Set here, not left to
# swarm::core: a swarm-core commit or release before ns3-swarm-core#142
# (v0.3.0 included) passes on C++20.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SWARM_CORE_DIR "" CACHE PATH "swarm-core's core/ directory; empty: fetch the release swarm-core.lock names")
option(BEEADHOC_CORE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_beeadhoc_core_top ON)
else()
  set(_beeadhoc_core_top OFF)
endif()
option(BEEADHOC_CORE_TESTS "Build the tests and register them with ctest" ${_beeadhoc_core_top})
option(BEEADHOC_CORE_SWARM_CORE_FROM_PACKAGE "Take swarm-core from find_package(swarm-core CONFIG), not the lock" OFF)
option(BEEADHOC_CORE_INSTALL "Add the install rules and the CMake package beeadhoc-core" OFF)
if(BEEADHOC_CORE_INSTALL AND NOT BEEADHOC_CORE_SWARM_CORE_FROM_PACKAGE)
  message(FATAL_ERROR "BEEADHOC_CORE_INSTALL needs BEEADHOC_CORE_SWARM_CORE_FROM_PACKAGE=ON: the package links an installed swarm-core")
endif()

# Sanitizer flags first: they are directory options, so swarm-core's sources,
# added below, are instrumented too.
if(BEEADHOC_CORE_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# swarm-core's core/ as a subproject: it defines the library target
# swarm::core (its own source list, so a new core/*.cc needs no edit here) and
# nothing else: no test target, no ctest entry, no -Werror on our sources
# (swarm-routing/ns3-swarm-core#100; its README "Use core/ from another CMake
# project").
if(BEEADHOC_CORE_SWARM_CORE_FROM_PACKAGE)
  find_package(swarm-core CONFIG REQUIRED)
elseif(SWARM_CORE_DIR)
  if(NOT EXISTS "${SWARM_CORE_DIR}/swarm-ports.h")
    message(FATAL_ERROR "SWARM_CORE_DIR=${SWARM_CORE_DIR} is not swarm-core's core/ directory")
  endif()
  add_subdirectory(${SWARM_CORE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/swarm-core)
else()
  # The swarm-core release ../swarm-core.lock names (swarm-routing/ns3-swarm-core#134):
  # its tarball by URL, checked against the lock's SHA256 (from the release's
  # SHA256SUMS). CMake strips the tarball's top directory swarm-core/. Extracted
  # files get the extraction time (DOWNLOAD_EXTRACT_TIMESTAMP FALSE, CMake 3.24+),
  # so a lock bump rebuilds every swarm-core object.
  file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/../swarm-core.lock _swarm_core_lock REGEX "^SWARM_CORE_[A-Z0-9_]+=")
  # a lock bump re-runs the configure step
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../swarm-core.lock)
  foreach(_swarm_core_kv IN LISTS _swarm_core_lock)
    string(REGEX MATCH "^([A-Z0-9_]+)=(.*)$" _swarm_core_m "${_swarm_core_kv}")
    set(_LOCK_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
  endforeach()
  foreach(_swarm_core_key REPO TAG ASSET SHA256)
    if(NOT _LOCK_SWARM_CORE_${_swarm_core_key})
      message(FATAL_ERROR "swarm-core.lock: no SWARM_CORE_${_swarm_core_key}")
    endif()
  endforeach()
  set(_swarm_core_timestamp)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    set(_swarm_core_timestamp DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  endif()
  include(FetchContent)
  FetchContent_Declare(swarm_core_src
    URL https://github.com/${_LOCK_SWARM_CORE_REPO}/releases/download/${_LOCK_SWARM_CORE_TAG}/${_LOCK_SWARM_CORE_ASSET}
    URL_HASH SHA256=${_LOCK_SWARM_CORE_SHA256}
    ${_swarm_core_timestamp}
    SOURCE_SUBDIR core)
  FetchContent_MakeAvailable(swarm_core_src)
endif()

# The strict warnings the org CI applies (ns3-containers' swarm-quality.cmake),
# on BeeAdHoc's own sources only.
# A swarm-core deprecation ([[deprecated]], its docs/api-policy.md
# "Deprecation") stays a warning: it shows in every build, never fails one.
set(BEEADHOC_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
                      -Wcast-align -Woverloaded-virtual -Wnull-dereference -Wformat=2 -Werror
                      -Wno-error=deprecated-declarations)

add_library(beeadhoc_core STATIC
  beeadhoc-forager.cc
  beeadhoc-router.cc
  beeadhoc-scout.cc
  beeadhoc-swarm.cc
  beeadhoc-trailer.cc
)
add_library(beeadhoc::core ALIAS beeadhoc_core)
# The installed package's imported target is beeadhoc::core too (EXPORT_NAME and
# the export's NAMESPACE).
set_target_properties(beeadhoc_core PROPERTIES EXPORT_NAME core)
include(GNUInstallDirs)
target_include_directories(beeadhoc_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/beeadhoc-core>)
if(BEEADHOC_CORE_SANITIZE)
  # The objects are instrumented, so whatever links them needs the sanitizer
  # runtime: add_link_options above reaches this directory's targets only, not
  # a consumer's (or an installed package's) executable.
  target_link_options(beeadhoc_core INTERFACE -fsanitize=address,undefined)
endif()
target_link_libraries(beeadhoc_core PUBLIC swarm::core)
target_compile_options(beeadhoc_core PRIVATE ${BEEADHOC_WARNINGS})

if(BEEADHOC_CORE_TESTS)
  add_executable(beeadhoc_core_tests
    test/all-tests.cc
    test/run-core-tests.cc
    test/test-data.cc
    test/test-forager.cc
    test/test-observer.cc
    test/test-router.cc
    test/test-scout.cc
    test/test-swarm.cc
  )
  target_link_libraries(beeadhoc_core_tests PRIVATE beeadhoc_core)
  target_compile_options(beeadhoc_core_tests PRIVATE ${BEEADHOC_WARNINGS})

  enable_testing()
  add_test(NAME beeadhoc-core COMMAND beeadhoc_core_tests)
endif()

if(BEEADHOC_CORE_INSTALL)
  include(CMakePackageConfigHelpers)
  set(_beeadhoc_core_cmake_dir ${CMAKE_INSTALL_LIBDIR}/cmake/beeadhoc-core)
  install(TARGETS beeadhoc_core EXPORT beeadhoc-coreTargets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(FILES
    beeadhoc-config.h
    beeadhoc-forager.h
    beeadhoc-router.h
    beeadhoc-scout.h
    beeadhoc-swarm.h
    beeadhoc-trailer.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/beeadhoc-core)
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/beeadhoc-core)
  install(EXPORT beeadhoc-coreTargets NAMESPACE beeadhoc:: DESTINATION ${_beeadhoc_core_cmake_dir})
  configure_package_config_file(beeadhoc-coreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/beeadhoc-coreConfig.cmake
    INSTALL_DESTINATION ${_beeadhoc_core_cmake_dir})
  # As swarm-core (its docs/api-policy.md): before 1.0 any minor may break,
  # so a 0.y release satisfies a request for 0.y only; from 1.0, its major.
  if(PROJECT_VERSION_MAJOR EQUAL 0)
    set(_beeadhoc_core_compat SameMinorVersion)
  else()
    set(_beeadhoc_core_compat SameMajorVersion)
  endif()
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/beeadhoc-coreConfigVersion.cmake
    COMPATIBILITY ${_beeadhoc_core_compat})
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/beeadhoc-coreConfig.cmake
                ${CMAKE_CURRENT_BINARY_DIR}/beeadhoc-coreConfigVersion.cmake
          DESTINATION ${_beeadhoc_core_cmake_dir})
endif()
