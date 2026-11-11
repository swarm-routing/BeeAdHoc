# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: GPL-2.0-only
build_lib_example(
  NAME beeadhoc-example
  SOURCE_FILES beeadhoc-example.cc
  LIBRARIES_TO_LINK ${libbeeadhoc} ${libswarm-core} ${libwifi} ${libmobility} ${libapplications}
                    ${libflow-monitor}
)
