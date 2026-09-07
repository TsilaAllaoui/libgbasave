#pragma once

/*
 * Allocation-free / bounded-memory frontend for NDS, ESP32 and similar hosts.
 *
 * This is not a second patch engine.  It is libgbasave's constrained frontend:
 * the database, target policy, runtime assets and patch semantics remain owned
 * by libgbasave.  Consumers provide transport/filesystem callbacks and apply
 * the returned overlays; they must not synthesize save routes or placements.
 */

#define GBASAVE_STREAMING_API_VERSION 1u

#include "gbasave/streaming/save_plan.h"
#include "gbasave/streaming/compat_database.h"
#include "gbasave/streaming/analyzer.h"
#include "gbasave/streaming/exact_plan.h"
#include "gbasave/streaming/nor_layout.h"
#include "gbasave/streaming/nor_patch.h"
#include "gbasave/streaming/nor_prepare.h"
#include "gbasave/streaming/save_memory.h"
#include "gbasave/save_memory_profiles.h"
