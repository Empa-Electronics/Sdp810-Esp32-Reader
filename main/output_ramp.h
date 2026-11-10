#pragma once

/* Moves an output level (0.0 to 1.0) toward its target at a fixed rate.
 * Plain C with no ESP-IDF dependency, so the host tests can run it. */

#include <stdbool.h>
#include <stdint.h>

static inline float output_ramp_step(float level, bool on, uint32_t dt_ms,
                                     uint32_t rise_ms, uint32_t fall_ms)
{
    if (on) {
        level += (float)dt_ms / (float)rise_ms;
        return level > 1.0f ? 1.0f : level;
    }
    level -= (float)dt_ms / (float)fall_ms;
    return level < 0.0f ? 0.0f : level;
}
