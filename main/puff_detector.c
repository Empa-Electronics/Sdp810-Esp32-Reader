#include "puff_detector.h"

#include <math.h>
#include <stddef.h>

puff_config_t puff_default_config(void)
{
    /* Set from recordings: inhale 10-85 Pa with onset up to 574 Pa/s, blow +2 to +9 Pa.
     * Wind limits (max_roughness, max_slew_pa_s, max_active_slew_pa_s) are off because hard
     * pulls overlap the wind recorded so far: set them to finite values to enable them. */
    puff_config_t c = {
        .inhale_sign = -1,      /* "+" port on the air path, "-" port on protected ambient */
        .filter_tau_ms = 30.0f,
        .arm_pa = 0.8f,
        .on_pa = 1.0f,
        .off_pa = 0.5f,
        .reverse_pa = 1.0f,
        .max_roughness = INFINITY,
        .max_slew_pa_s = INFINITY,
        .max_active_slew_pa_s = INFINITY,
        .confirm_ms = 250,
        .weak_timeout_ms = 1000,
        .release_ms = 150,
        .max_active_ms = 10000,
        .cooldown_ms = 500,
        .lockout_ms = 300,
        .max_gap_ms = 200,
    };
    return c;
}

void puff_init(puff_detector_t *d, const puff_config_t *cfg)
{
    d->cfg = *cfg;
    d->state = PUFF_IDLE;
    d->reason = PUFF_REASON_NONE;
    d->started = false;
    d->last_ms = 0;
    d->state_since_ms = 0;
    d->below_since_ms = 0;
    d->quiet_since_ms = 0;
    d->quiet_required_ms = 0;
    d->filtered = 0.0f;
    d->prev_filtered = 0.0f;
    d->path = 0.0f;
    d->peak = 0.0f;
}

static void enter(puff_detector_t *d, puff_state_t state, uint32_t now_ms)
{
    d->state = state;
    d->state_since_ms = now_ms;
}

static void lock_out(puff_detector_t *d, uint32_t now_ms, puff_reason_t reason, uint32_t quiet_ms)
{
    d->reason = reason;
    d->quiet_since_ms = now_ms;
    d->quiet_required_ms = quiet_ms;
    enter(d, PUFF_LOCKOUT, now_ms);
}

puff_state_t puff_update(puff_detector_t *d, uint32_t now_ms, float pressure_pa)
{
    const puff_config_t *c = &d->cfg;
    float raw = (float)c->inhale_sign * pressure_pa;

    if (!d->started) {
        d->started = true;
        d->filtered = raw;
        d->prev_filtered = raw;
        d->last_ms = now_ms;
        d->state_since_ms = now_ms;
        return d->state;
    }

    uint32_t dt = now_ms - d->last_ms;
    d->last_ms = now_ms;

    if (dt > c->max_gap_ms) {
        d->filtered = raw;
        d->prev_filtered = raw;
        lock_out(d, now_ms, PUFF_REASON_DATA_GAP, c->lockout_ms);
        return d->state;
    }

    d->prev_filtered = d->filtered;
    float alpha = (float)dt / (c->filter_tau_ms + (float)dt);
    d->filtered += alpha * (raw - d->filtered);
    float s = d->filtered;
    float slew = dt ? (s - d->prev_filtered) * 1000.0f / (float)dt : 0.0f;

    /* Opposite polarity always wins, whatever the state: an inhale cannot produce it. */
    if (s < -c->reverse_pa && d->state != PUFF_LOCKOUT) {
        lock_out(d, now_ms, PUFF_REASON_REVERSE, c->lockout_ms);
        return d->state;
    }

    switch (d->state) {
    case PUFF_IDLE:
        if (s > c->arm_pa && slew > c->max_slew_pa_s) {
            lock_out(d, now_ms, PUFF_REASON_SLEW, c->lockout_ms);
        } else if (s > c->arm_pa) {
            d->path = 0.0f;
            d->peak = s;
            enter(d, PUFF_CANDIDATE, now_ms);
        }
        break;

    case PUFF_CANDIDATE: {
        d->path += fabsf(s - d->prev_filtered);
        if (s > d->peak) {
            d->peak = s;
        }
        uint32_t elapsed = now_ms - d->state_since_ms;
        if (slew > c->max_slew_pa_s) {
            lock_out(d, now_ms, PUFF_REASON_SLEW, c->lockout_ms);
        } else if (s < c->arm_pa) {
            d->reason = PUFF_REASON_DIP;
            enter(d, PUFF_IDLE, now_ms);
        } else if (d->path > c->max_roughness * d->peak) {
            lock_out(d, now_ms, PUFF_REASON_ROUGH, c->lockout_ms);
        } else if (elapsed >= c->confirm_ms && s >= c->on_pa) {
            d->reason = PUFF_REASON_NONE;
            d->below_since_ms = now_ms;
            enter(d, PUFF_ACTIVE, now_ms);
        } else if (elapsed >= c->weak_timeout_ms) {
            lock_out(d, now_ms, PUFF_REASON_WEAK, c->lockout_ms);
        }
        break;
    }

    case PUFF_ACTIVE:
        /* Only rises count: the pressure falls quickly at the end of a normal inhale. */
        if (now_ms - d->state_since_ms >= c->max_active_ms) {
            lock_out(d, now_ms, PUFF_REASON_MAX_TIME, c->lockout_ms);
        } else if (slew > c->max_active_slew_pa_s) {
            lock_out(d, now_ms, PUFF_REASON_SLEW, c->lockout_ms);
        } else if (s >= c->off_pa) {
            d->below_since_ms = now_ms;
        } else if (now_ms - d->below_since_ms >= c->release_ms) {
            lock_out(d, now_ms, PUFF_REASON_RELEASED, c->cooldown_ms);
        }
        break;

    case PUFF_LOCKOUT:
        /* The quiet timer restarts on any activity, so held pressure stays locked out. */
        if (fabsf(s) >= c->arm_pa || s < -c->reverse_pa) {
            d->quiet_since_ms = now_ms;
        } else if (now_ms - d->quiet_since_ms >= d->quiet_required_ms) {
            enter(d, PUFF_IDLE, now_ms);
        }
        break;
    }

    return d->state;
}

const char *puff_state_name(puff_state_t state)
{
    static const char *const names[] = { "IDLE", "CANDIDATE", "ACTIVE", "LOCKOUT" };
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "?";
}

const char *puff_reason_name(puff_reason_t reason)
{
    static const char *const names[] = {
        "none", "released", "reverse", "dip", "rough", "weak", "max_time", "data_gap", "slew",
    };
    return (unsigned)reason < sizeof(names) / sizeof(names[0]) ? names[reason] : "?";
}
