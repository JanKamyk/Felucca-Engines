/* SPDX-License-Identifier: MIT */
#include "fe_tables.h"
#include "felucca_plugin.h"
#include "dsp.c"

static void tr808_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0;
    v->s[0] = 32767; /* Kick Pitch Envelope Amount */
    v->s[1] = 32767; /* Volume Envelope State */

    /* Seed for noise */
    uint32_t seed = (v->note + 1u) * 0x9E3779B9u ^ (uint32_t)v->fine * 0x85EBCA6Bu;
    v->s[2] = (int32_t)(seed ? seed : 1u);

    v->s[4] = 0; /* filter ic1 */
    v->s[5] = 0; /* filter ic2 */
}

static int32_t tr808_amp(track_t *t, voice_t *v, int32_t adsr)
{
    (void)adsr;

    int is_kick = (v->note < 40);
    int32_t decay_param = is_kick ? t->p[P_E1] : t->p[P_E3];

    /* map decay_param (0-127) to decay factor */
    int32_t decay_amt = 1000 - (decay_param * 7);
    if (decay_amt < 10) decay_amt = 10;

    v->s[1] -= decay_amt;
    if (v->s[1] <= 0) {
        v->s[1] = 0;
        v->env = 0;
        v->stage = 0;
        v->active = 0;
        return 0;
    }

    return v->s[1];
}

static void tr808_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    int is_kick = (v->note < 40);
    uint32_t i;

    if (is_kick) {
        int32_t p_env = v->s[0];
        uint32_t base_inc = m->inc;

        for (i = 0; i < n; i++) {
            /* Pitch decays fast for the punch of a kick drum */
            uint32_t current_inc = base_inc + ((base_inc * p_env) >> 13);
            int32_t sample = osc_sine(v->ph[0]);
            v->ph[0] += current_inc;
            out[i] += voice_amp(sample, m, i);
        }

        /* Fast pitch envelope decay per block */
        p_env -= p_env >> 2;
        if (p_env < 0) p_env = 0;
        v->s[0] = p_env;

    } else {
        /* Hihat */
        int32_t tone = t->p[P_E2]; /* 0-127 */
        int32_t rng = v->s[2];
        int32_t ic1 = v->s[4];
        int32_t ic2 = v->s[5];

        tsvf_t f;
        /* Bandpass filter with tone controlling frequency, some fixed resonance */
        tsvf_coef(&f, ( tone + 40 ) << 8, 80); /* cut 40-167 << 8, res 80 */

        for (i = 0; i < n; i++) {
            uint32_t r = noise32(&rng);
            int32_t w = (int32_t)(r >> 16) - 32768; /* white noise -32768..32767 */

            /* tsvf_lp gives lowpass, but bandpass is easily derived.
               We just need bandpass.
               v3 = in - ic2;
               v1 = a1 * ic1 + a2 * v3;
               v1 is proportional to bandpass state! Wait, in tsvf_lp, the state update is:
               v3 = in - ic2
               v1 = a1*ic1 + a2*v3
               bp = v1 (scaled).
               Let's just use tsvf_lp and extract BP manually or use LP if it sounds ok?
               Let's extract bandpass since Simper SVF bandpass = v1 * something,
               but we can do it inline: */
            int32_t v3 = w - ic2;
            int32_t v1 = (f.a1 * ic1 + f.a2 * v3) >> 13;
            int32_t v2 = ic2 + ((f.a2 * ic1 + f.a3 * v3) >> 13);
            ic1 = clamp(2 * v1 - ic1, -150000, 150000);
            ic2 = clamp(2 * v2 - ic2, -150000, 150000);

            int32_t bp = v1; /* v1 is the bandpass state in simper SVF */

            /* boost bandpass level since it's quiet */
            bp = (bp * 3);
            if (bp > 32767) bp = 32767;
            if (bp < -32768) bp = -32768;

            out[i] += voice_amp(bp, m, i);
        }

        v->s[2] = rng;
        v->s[4] = ic1;
        v->s[5] = ic2;
    }
}

static const preset_t TR808_PRESETS[] = {
    {"808KIT", {40, 60, 100, 40, 0, 0, 0, 0}, {0, 60, 0, 60}, 0, 0, FX(0, 0, 0, 0), PAT(0)},
};

static const engine_t ENG_TR808 = {
    .name = "TR808",
    .page_title = {"KICK", "HH"},
    .edit = {
        {"K TUNE", F_PCT, 0, 127, 64, 0, 0},
        {"K DECA", F_PCT, 0, 127, 64, 0, 0},
        {"H TONE", F_PCT, 0, 127, 64, 0, 0},
        {"H DECA", F_PCT, 0, 127, 64, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    .presets = TR808_PRESETS,
    .npresets = sizeof TR808_PRESETS / sizeof TR808_PRESETS[0],
    .knob = {P_E0, P_E1, P_E2, P_E3},
    .oneshot = 1,
    .note_on = tr808_note_on,
    .render = tr808_render,
    .amp = tr808_amp,
};

FE_PLUGIN(ENG_TR808, FE_ID('t', 'r', '8', '0'), 1, FE_NEW, 0)
