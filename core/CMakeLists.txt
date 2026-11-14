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
# SWARM_CORE_DIR: a local swarm-core core/ directory; empty fetches
# SWARM_CORE_REF (a full commit SHA, bumped by PR) from GitHub.
# BEEADHOC_CORE_SANITIZE=ON adds ASan + UBSan, as the sanitized CI leg.
cmake_minimum_required(VERSION 3.20)
project(beeadhoc_core LANGUAGES CXX)

# The C++ standard of the ns-3 release the org supports: ns-3's own
# CMAKE_CXX_STANDARD_MINIMUM (C++23 from ns-3.46), the standard swarm-core's
# core/ targets too (swarm-routing/ns3-swarm-core#141).
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SWARM_CORE_DIR "" CACHE PATH "swarm-core's core/ directory; empty: fetch SWARM_CORE_REF")
set(SWARM_CORE_REF "7be7fe212d66174bfd26e0510bdacb0709171f02" CACHE STRING
    "swarm-routing/ns3-swarm-core commit to fetch when SWARM_CORE_DIR is empty")
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
  include(FetchContent)
  FetchContent_Declare(swarm_core_src
    GIT_REPOSITORY https://github.com/swarm-routing/ns3-swarm-core.git
    GIT_TAG ${SWARM_CORE_REF}
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
