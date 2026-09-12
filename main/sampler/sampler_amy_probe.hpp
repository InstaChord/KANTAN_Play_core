// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#pragma once

namespace sampler_ns {

// Runs only in the sampler_s3_amy_probe environment. The release build gets
// an inline no-op and therefore does not link AMY or change its memory use.
#if defined(KANPLAY_AMY_PROBE) && !defined(M5UNIFIED_PC_BUILD)
void run_amy_startup_probe();
#else
inline void run_amy_startup_probe() {}
#endif

} // namespace sampler_ns
