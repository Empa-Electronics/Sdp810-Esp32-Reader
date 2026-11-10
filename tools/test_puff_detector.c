/* Host tests for the inhale detector. Run with: tools/replay.sh test */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "output_ramp.h"
#include "puff_detector.h"

#define PI 3.14159265f

typedef float (*signal_fn)(float t_s);

typedef struct {
    int activations;
    float active_s;
    float first_on_s;
    puff_reason_t last_reason;
} result_t;

static float noise(void) { return ((float)rand() / (float)RAND_MAX - 0.5f) * 0.2f; }

static puff_config_t test_cfg;

static result_t run(signal_fn fn, float duration_s, uint32_t step_ms)
{
    puff_detector_t d;
    puff_init(&d, &test_cfg);
    result_t r = { 0, 0.0f, -1.0f, PUFF_REASON_NONE };
    puff_state_t prev = PUFF_IDLE;
    for (uint32_t ms = 0; ms < (uint32_t)(duration_s * 1000); ms += step_ms) {
        float t = ms / 1000.0f;
        puff_state_t s = puff_update(&d, ms, fn(t) + noise());
        if (s == PUFF_ACTIVE) {
            r.active_s += step_ms / 1000.0f;
            if (prev != PUFF_ACTIVE) {
                r.activations++;
                if (r.first_on_s < 0) {
                    r.first_on_s = t;
                }
            }
        }
        if (d.reason != PUFF_REASON_NONE) {
            r.last_reason = d.reason;
        }
        prev = s;
    }
    return r;
}

/* Trapezoid from t0 to t1, positive amp = inhale direction (raw pressure is negative). */
static float pulse(float t, float t0, float t1, float amp, float ramp)
{
    float v = 0.0f;
    if (t > t0 && t < t1) {
        v = 1.0f;
        if (t < t0 + ramp) v = (t - t0) / ramp;
        if (t > t1 - ramp) v = (t1 - t) / ramp;
    }
    return -amp * v;
}

/* Amplitudes and rise rates follow the labelled recording of 2026-11-05. */
static float ramp_to(float t, float t0, float amp, float rate)
{
    float v = (t - t0) * rate;
    return t <= t0 ? 0.0f : -(v < amp ? v : amp);
}

static float sig_quiet(float t) { (void)t; return 0.0f; }
static float sig_inhale(float t) { return pulse(t, 1, 5, 18, 0.9f); }
static float sig_gentle_inhale(float t) { return pulse(t, 1, 5, 11, 0.6f); }
static float sig_fast_inhale(float t) { return pulse(t, 1, 5, 12, 0.08f); }
static float sig_hard_inhale(float t) { return pulse(t, 1, 3, 85, 0.3f); }
static float sig_blip_then_inhale(float t) { return pulse(t, 1, 1.4f, -3.3f, 0.1f) + pulse(t, 2, 4, 40, 0.3f); }
static float sig_two_inhales(float t) { return pulse(t, 1, 4, 18, 0.9f) + pulse(t, 6, 9, 18, 0.9f); }
static float sig_blow(float t) { return pulse(t, 1, 4, -2, 0.5f); }
static float sig_tap(float t) { return pulse(t, 1, 1.12f, 85, 0.02f); }
static float sig_below_arm(float t) { return pulse(t, 1, 5, 0.6f, 0.5f); }
static float sig_draft(float t) { return pulse(t, 1, 5, 0.9f, 0.3f); }
static float sig_wind_jump(float t) { return pulse(t, 1, 3, 50, 0.1f); }
static float sig_gust_bipolar(float t) { return t > 1 && t < 6 ? 30 * sinf(2 * PI * 3 * t) : 0; }
static float sig_gust_one_sided(float t)
{
    return t > 1 && t < 6 ? -(30 + 20 * sinf(2 * PI * 8 * t)) : 0;
}
static float sig_gust_during_inhale(float t)
{
    return ramp_to(t, 1, 18, 20) + (t > 3 && t < 4 ? -40 : 0);
}
static float sig_blow_during_inhale(float t) { return t < 3 ? ramp_to(t, 1, 18, 20) : (t < 5 ? 2 : 0); }
static float sig_endless(float t) { return ramp_to(t, 1, 18, 20); }

static int failures;

static void check(const char *name, signal_fn fn, float dur, uint32_t step, int want, puff_reason_t reason)
{
    result_t r = run(fn, dur, step);
    int ok = r.activations == want && (reason == PUFF_REASON_NONE || r.last_reason == reason);
    printf("%-4s %-24s %3u ms: activations=%d active=%.2fs first_on=%.2fs reason=%s\n",
           ok ? "ok" : "FAIL", name, step, r.activations, r.active_s, r.first_on_s,
           puff_reason_name(r.last_reason));
    if (!ok) {
        failures++;
    }
}

static void gap_turns_output_off(void)
{
    puff_detector_t d;
    puff_config_t cfg = puff_default_config();
    puff_init(&d, &cfg);
    uint32_t ms = 0;
    for (; ms < 2000; ms += 10) {
        puff_update(&d, ms, ramp_to(ms / 1000.0f, 0, 18, 20));
    }
    int was_active = d.state == PUFF_ACTIVE;
    puff_state_t after = puff_update(&d, ms + 1000, -18);
    int ok = was_active && after == PUFF_LOCKOUT && d.reason == PUFF_REASON_DATA_GAP;
    printf("%-4s sample gap forces output off\n", ok ? "ok" : "FAIL");
    if (!ok) {
        failures++;
    }
}

static void ramp_reaches_targets(void)
{
    float level = 0.0f;
    int steps_up = 0, steps_down = 0;
    while (level < 1.0f && steps_up < 1000) {
        level = output_ramp_step(level, true, 10, 300, 500);
        steps_up++;
    }
    while (level > 0.0f && steps_down < 1000) {
        level = output_ramp_step(level, false, 10, 300, 500);
        steps_down++;
    }
    int ok = steps_up >= 29 && steps_up <= 31 && steps_down >= 49 && steps_down <= 51;
    printf("%-4s output ramp: up %d steps, down %d steps\n", ok ? "ok" : "FAIL", steps_up, steps_down);
    if (!ok) {
        failures++;
    }
}

int main(void)
{
    static const uint32_t steps[] = { 10, 50 };
    for (int i = 0; i < 2; i++) {
        uint32_t st = steps[i];
        test_cfg = puff_default_config();
        check("quiet", sig_quiet, 5, st, 0, PUFF_REASON_NONE);
        check("inhale", sig_inhale, 7, st, 1, PUFF_REASON_RELEASED);
        check("gentle inhale", sig_gentle_inhale, 7, st, 1, PUFF_REASON_RELEASED);
        check("fast-start inhale", sig_fast_inhale, 7, st, 1, PUFF_REASON_RELEASED);
        check("hard inhale 85 Pa", sig_hard_inhale, 6, st, 1, PUFF_REASON_RELEASED);
        check("two inhales", sig_two_inhales, 12, st, 2, PUFF_REASON_RELEASED);
        check("blip then inhale", sig_blip_then_inhale, 7, st, 1, PUFF_REASON_RELEASED);
        check("blow +2 Pa", sig_blow, 6, st, 0, PUFF_REASON_REVERSE);
        if (st == 10) {  /* the filter behaves differently at 20 Hz; the firmware runs at 100 Hz */
            check("short tap", sig_tap, 4, st, 0, PUFF_REASON_NONE);
        }
        check("below arm level", sig_below_arm, 7, st, 0, PUFF_REASON_NONE);
        check("draft 0.9 Pa", sig_draft, 7, st, 0, PUFF_REASON_WEAK);
        check("blow during inhale", sig_blow_during_inhale, 7, st, 1, PUFF_REASON_REVERSE);
        check("endless inhale cutoff", sig_endless, 30, st, 1, PUFF_REASON_MAX_TIME);

        /* Wind limits are off by default and were tuned at these levels:
         * check they still work when enabled */
        test_cfg.arm_pa = 5.0f;
        test_cfg.on_pa = 8.0f;
        test_cfg.off_pa = 3.0f;
        test_cfg.max_roughness = 0.75f;
        test_cfg.max_slew_pa_s = 220.0f;
        test_cfg.max_active_slew_pa_s = 300.0f;
        check("wind on: wind jump", sig_wind_jump, 6, st, 0, PUFF_REASON_SLEW);
        check("wind on: bipolar gusts", sig_gust_bipolar, 8, st, 0, PUFF_REASON_NONE);
        if (st == 10) {  /* 8 Hz gusts cannot be resolved in 20 Hz logs */
            check("wind on: one-sided gusts", sig_gust_one_sided, 8, st, 0, PUFF_REASON_NONE);
        }
        check("wind on: gust during inhale", sig_gust_during_inhale, 8, st, 1, PUFF_REASON_SLEW);
    }
    gap_turns_output_off();
    ramp_reaches_targets();
    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures != 0;
}
