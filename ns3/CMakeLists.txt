# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: GPL-2.0-only
#
# The `beeadhoc` ns-3 contrib module: BeeAdHoc's simulator-free core/ (one
# directory up, MIT OR GPL-2.0-only) compiled into ns-3 on swarm-core, with the
# adapter that carries out its decisions (model/), a helper, the harness arm
# (helper/beeadhoc-bench-arms.cc, compiled only when routing-bench is installed
# beside it), an example and the test suite. Install it next to swarm-core:
#
#   ns3-build-contrib --test swarm-core=/src/ns3-swarm-core beeadhoc=/src/beeadhoc/ns3
#
# ns-3 reaches this directory through contrib/beeadhoc, a link to the
# checkout's ns3/: core/ is found beside the link's target, not beside the link.
get_filename_component(beeadhoc_ns3_dir "${CMAKE_CURRENT_SOURCE_DIR}" REALPATH)  # CMake < 3.19 too
get_filename_component(beeadhoc_core_dir "${beeadhoc_ns3_dir}/../core" ABSOLUTE)
# core/ includes swarm-core's primitives without the ns3/ prefix (they are the
# same files as ns3/swarm-*.h), so it builds alone as well.
set(beeadhoc_swarm_core_dir "${PROJECT_SOURCE_DIR}/contrib/swarm-core/core")
foreach(d "${beeadhoc_core_dir}" "${beeadhoc_swarm_core_dir}")
  if(NOT EXISTS "${d}")
    message(FATAL_ERROR "beeadhoc: ${d} not found (install swarm-core as contrib/swarm-core)")
  endif()
endforeach()
include_directories("${beeadhoc_core_dir}" "${beeadhoc_swarm_core_dir}")

# The harness arm, only where swarm-routing/ns3-tools' routing-bench is
# installed and enabled. Its library is named through a variable: ns-3 scans
# module CMakeLists for library tokens, and `beeadhoc` must also build alone
# (routing-bench README, "Registering an arm").
set(beeadhoc_bench_sources)
set(beeadhoc_bench_libs)
set(beeadhoc_bench_module routing-bench)
if(EXISTS ${PROJECT_SOURCE_DIR}/contrib/${beeadhoc_bench_module}/CMakeLists.txt
   AND beeadhoc_bench_module IN_LIST ns3-all-enabled-modules)
  set(beeadhoc_bench_sources helper/beeadhoc-bench-arms.cc)
  set(beeadhoc_bench_lib lib${beeadhoc_bench_module})
  set(beeadhoc_bench_libs ${${beeadhoc_bench_lib}})
endif()

build_lib(
  LIBNAME beeadhoc
  SOURCE_FILES
    ${beeadhoc_core_dir}/beeadhoc-forager.cc
    ${beeadhoc_core_dir}/beeadhoc-router.cc
    ${beeadhoc_core_dir}/beeadhoc-scout.cc
    ${beeadhoc_core_dir}/beeadhoc-swarm.cc
    ${beeadhoc_core_dir}/beeadhoc-trailer.cc
    model/beeadhoc-header.cc
    model/beeadhoc-routing-protocol.cc
    helper/beeadhoc-helper.cc
    ${beeadhoc_bench_sources}
  HEADER_FILES
    model/beeadhoc-routing-protocol.h
    helper/beeadhoc-helper.h
  LIBRARIES_TO_LINK ${libswarm-core} ${libcore} ${libnetwork} ${libinternet} ${beeadhoc_bench_libs}
  TEST_SOURCES
    ${beeadhoc_core_dir}/test/all-tests.cc
    ${beeadhoc_core_dir}/test/test-data.cc
    ${beeadhoc_core_dir}/test/test-forager.cc
    ${beeadhoc_core_dir}/test/test-observer.cc
    ${beeadhoc_core_dir}/test/test-router.cc
    ${beeadhoc_core_dir}/test/test-scout.cc
    ${beeadhoc_core_dir}/test/test-swarm.cc
    test/beeadhoc-test-suite.cc
)
