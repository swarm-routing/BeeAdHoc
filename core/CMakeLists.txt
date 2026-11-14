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
cmake_minimum_required(VERSION 3.20)
project(beeadhoc_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SWARM_CORE_DIR "" CACHE PATH "swarm-core's core/ directory; empty: fetch the release swarm-core.lock names")
option(BEEADHOC_CORE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

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
if(SWARM_CORE_DIR)
  if(NOT EXISTS "${SWARM_CORE_DIR}/swarm-ports.h")
    message(FATAL_ERROR "SWARM_CORE_DIR=${SWARM_CORE_DIR} is not swarm-core's core/ directory")
  endif()
  add_subdirectory(${SWARM_CORE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/swarm-core)
else()
  # The swarm-core release ../swarm-core.lock names (swarm-routing/ns3-swarm-core#134):
  # its tarball by URL, checked against the lock's SHA256 (from the release's
  # SHA256SUMS). CMake strips the tarball's top directory swarm-core/.
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
    set(_swarm_core_timestamp DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
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
target_include_directories(beeadhoc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(beeadhoc_core PUBLIC swarm::core)
target_compile_options(beeadhoc_core PRIVATE ${BEEADHOC_WARNINGS})

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
