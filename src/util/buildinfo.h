#pragma once

/* Build identity, stamped by the Makefile (src/util/buildinfo_gen.h). Hosts
   that have not run make yet will get zero/unknown rather than a build
   failure. */
#if __has_include("util/buildinfo_gen.h")
#  include "util/buildinfo_gen.h"
#endif

#ifndef INDIGO_BUILD_COMMIT
#  define INDIGO_BUILD_COMMIT "unknown"
#endif
#ifndef INDIGO_BUILD_NUMBER
#  define INDIGO_BUILD_NUMBER 0
#endif
#ifndef INDIGO_BUILD_DATE
#  define INDIGO_BUILD_DATE "unknown"
#endif
