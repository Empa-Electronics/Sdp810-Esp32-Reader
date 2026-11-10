/* Replays a plotter CSV log through the real detector and reports what it would do.
 * Usage: puff_replay log.csv [key=value ...]
 * Keys: sign arm on off reverse rough slew aslew confirm release lockout */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "puff_detector.h"

#define MAX_LABELS 8

typedef struct {
    char name[16];
    int samples;
    int activations;
    int active_samples;
} label_stats_t;

static void apply_option(puff_config_t *c, const char *arg)
{
    const char *eq = strchr(arg, '=');
    if (!eq) {
        fprintf(stderr, "bad option: %s\n", arg);
        exit(2);
    }
    size_t n = (size_t)(eq - arg);
    double v = atof(eq + 1);
#define IS(k) (n == strlen(k) && strncmp(arg, k, n) == 0)
    if (IS("sign")) c->inhale_sign = (int)v;
    else if (IS("arm")) c->arm_pa = (float)v;
    else if (IS("on")) c->on_pa = (float)v;
    else if (IS("off")) c->off_pa = (float)v;
    else if (IS("reverse")) c->reverse_pa = (float)v;
    else if (IS("rough")) c->max_roughness = (float)v;
    else if (IS("slew")) c->max_slew_pa_s = (float)v;
    else if (IS("aslew")) c->max_active_slew_pa_s = (float)v;
    else if (IS("lockout")) c->lockout_ms = (uint32_t)v;
    else if (IS("confirm")) c->confirm_ms = (uint32_t)v;
    else if (IS("release")) c->release_ms = (uint32_t)v;
    else {
        fprintf(stderr, "unknown option: %s\n", arg);
        exit(2);
    }
#undef IS
}

static int column(char *header, const char *name)
{
    int i = 0;
    for (char *f = strtok(header, ",\r\n"); f; f = strtok(NULL, ",\r\n"), i++) {
        if (strcmp(f, name) == 0) {
            return i;
        }
    }
    return -1;
}

static label_stats_t *stats_for(label_stats_t *stats, int *count, const char *name)
{
    for (int i = 0; i < *count; i++) {
        if (strcmp(stats[i].name, name) == 0) {
            return &stats[i];
        }
    }
    int slot = *count < MAX_LABELS ? (*count)++ : MAX_LABELS - 1;
    snprintf(stats[slot].name, sizeof(stats[slot].name), "%s", name);
    return &stats[slot];
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s log.csv [key=value ...]\n", argv[0]);
        return 2;
    }
    puff_config_t cfg = puff_default_config();
    for (int i = 2; i < argc; i++) {
        apply_option(&cfg, argv[i]);
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) {
        perror(argv[1]);
        return 1;
    }

    char line[256], header[256];
    if (!fgets(line, sizeof(line), f)) {
        return 1;
    }
    strcpy(header, line);
    int col_ms = column(header, "device_ms");
    strcpy(header, line);
    int col_pa = column(header, "pressure_pa");
    strcpy(header, line);
    int col_label = column(header, "label");
    if (col_ms < 0 || col_pa < 0) {
        fprintf(stderr, "missing device_ms/pressure_pa columns\n");
        return 1;
    }

    puff_detector_t d;
    puff_init(&d, &cfg);
    label_stats_t stats[MAX_LABELS] = { 0 };
    int n_labels = 0;
    int rejected[9] = { 0 };
    puff_state_t prev = PUFF_IDLE;
    uint32_t t0 = 0, active_from = 0;
    int first = 1;

    printf("sign=%d arm=%.1f on=%.1f off=%.1f reverse=%.1f rough=%.2f slew=%.0f confirm=%u ms\n",
           cfg.inhale_sign, cfg.arm_pa, cfg.on_pa, cfg.off_pa, cfg.reverse_pa,
           cfg.max_roughness, cfg.max_slew_pa_s, cfg.confirm_ms);

    while (fgets(line, sizeof(line), f)) {
        uint32_t ms = 0;
        float pa = 0;
        const char *label = "-";
        int i = 0, seen = 0;
        for (char *p = strtok(line, ",\r\n"); p; p = strtok(NULL, ",\r\n"), i++) {
            if (i == col_ms) { ms = (uint32_t)strtoul(p, NULL, 10); seen++; }
            else if (i == col_pa) { pa = (float)atof(p); seen++; }
            else if (i == col_label) { label = p; }
        }
        if (seen != 2) {
            continue;
        }
        if (first) {
            t0 = ms;
            first = 0;
        }
        puff_reason_t before = d.reason;
        puff_state_t s = puff_update(&d, ms, pa);
        double t = (ms - t0) / 1000.0;
        label_stats_t *ls = stats_for(stats, &n_labels, label);
        ls->samples++;
        if (s == PUFF_ACTIVE) {
            ls->active_samples++;
        }
        if (s == PUFF_ACTIVE && prev != PUFF_ACTIVE) {
            active_from = ms;
            ls->activations++;
            printf("%9.2f s  ON   label=%s\n", t, label);
        } else if (s != PUFF_ACTIVE && prev == PUFF_ACTIVE) {
            rejected[d.reason]++;
            printf("%9.2f s  OFF  after %.2f s (%s)\n", t, (ms - active_from) / 1000.0,
                   puff_reason_name(d.reason));
        } else if (prev == PUFF_CANDIDATE && s != PUFF_CANDIDATE) {
            printf("%9.2f s  rejected: %s  label=%s\n", t, puff_reason_name(d.reason), label);
            rejected[d.reason]++;
        } else if (prev == PUFF_IDLE && s == PUFF_LOCKOUT && d.reason != before) {
            printf("%9.2f s  rejected: %s  label=%s\n", t, puff_reason_name(d.reason), label);
            rejected[d.reason]++;
        }
        prev = s;
    }
    fclose(f);

    printf("\nlabel        samples  activations  active samples\n");
    for (int i = 0; i < n_labels; i++) {
        printf("%-12s %7d  %11d  %14d\n", stats[i].name, stats[i].samples,
               stats[i].activations, stats[i].active_samples);
    }
    printf("rejected: reverse=%d dip=%d rough=%d weak=%d slew=%d\n", rejected[PUFF_REASON_REVERSE],
           rejected[PUFF_REASON_DIP], rejected[PUFF_REASON_ROUGH], rejected[PUFF_REASON_WEAK],
           rejected[PUFF_REASON_SLEW]);
    return 0;
}
