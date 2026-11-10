#pragma once

/* Inhale detector for a differential pressure signal.
 * Plain C with no ESP-IDF dependency, so the same code runs on the ESP32 and in
 * the host tests / log replay (tools/test_puff_detector.c, tools/puff_replay.c). */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PUFF_IDLE = 0,
    PUFF_CANDIDATE,   /* above arm level, being verified */
    PUFF_ACTIVE,      /* confirmed inhale: output on */
    PUFF_LOCKOUT,     /* ignoring input until the signal has been quiet */
} puff_state_t;

/* Why the detector last left a state. Reported for tuning and logging. */
typedef enum {
    PUFF_REASON_NONE = 0,
    PUFF_REASON_RELEASED,   /* inhale ended normally */
    PUFF_REASON_REVERSE,    /* opposite polarity: blowing or wind */
    PUFF_REASON_DIP,        /* dropped out during verification: too short */
    PUFF_REASON_ROUGH,      /* too much fluctuation: gusty */
    PUFF_REASON_WEAK,       /* never reached on_pa: draft or weak wind */
    PUFF_REASON_MAX_TIME,   /* safety cutoff on active time */
    PUFF_REASON_DATA_GAP,   /* samples stopped arriving */
    PUFF_REASON_SLEW,       /* pressure changed too fast: wind or a knock */
} puff_reason_t;

typedef struct {
    int inhale_sign;           /* sign of the raw pressure during an inhale: -1 or +1 */
    float filter_tau_ms;       /* low-pass time constant */
    float arm_pa;              /* start verifying above this */
    float on_pa;               /* must reach this to activate */
    float off_pa;              /* release below this */
    float reverse_pa;          /* opposite-polarity level that forces a lockout */
    float max_roughness;       /* path length / peak allowed during verification */
    float max_slew_pa_s;       /* fastest rise allowed at the start of an inhale */
    float max_active_slew_pa_s; /* fastest rise allowed during an inhale, which ripples more */
    uint32_t confirm_ms;       /* minimum time above arm_pa before activating */
    uint32_t weak_timeout_ms;  /* give up if on_pa is not reached in this time */
    uint32_t release_ms;       /* time below off_pa that ends the inhale */
    uint32_t max_active_ms;    /* hard limit on continuous output */
    uint32_t cooldown_ms;      /* quiet time required after a normal release */
    uint32_t lockout_ms;       /* quiet time required after a rejection */
    uint32_t max_gap_ms;       /* sample gap treated as a fault */
} puff_config_t;

typedef struct {
    puff_config_t cfg;
    puff_state_t state;
    puff_reason_t reason;
    bool started;
    uint32_t last_ms;
    uint32_t state_since_ms;
    uint32_t below_since_ms;
    uint32_t quiet_since_ms;
    uint32_t quiet_required_ms;
    float filtered;            /* filtered signal, positive = inhale direction */
    float prev_filtered;
    float path;                /* total variation since entering CANDIDATE */
    float peak;
} puff_detector_t;

puff_config_t puff_default_config(void);
void puff_init(puff_detector_t *d, const puff_config_t *cfg);
/* Feeds one sample and returns the new state. Output should be on only in PUFF_ACTIVE. */
puff_state_t puff_update(puff_detector_t *d, uint32_t now_ms, float pressure_pa);
const char *puff_state_name(puff_state_t state);
const char *puff_reason_name(puff_reason_t reason);

#ifdef __cplusplus
}
#endif
