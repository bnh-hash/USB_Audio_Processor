/*
 * filter.c
 *
 * Created on: Mar 2, 2026
 * Author: Gökçe
 *
 * DSP Efekt Zinciri:
 * RingMod → LPF (2. derece) → HPF (2. derece) → AutoWah (değişecek)→  → Delay → Volume
 */
#include "filter.h"
#include <math.h>

/* ══════════════════════════════════════════════
   SABITLER & KORUMALAR
   ══════════════════════════════════════════════ */
// Denormal sayıları (FPU kilitlenmesini) önlemek için duyulmaz DC offset
#define ANTI_DENORMAL   1e-18f

/* --- WAH AYARLARI ---
#define WAH_MIN_FREQ    500.0f
#define WAH_MAX_FREQ    2500.0f
#define WAH_Q_FACTOR    0.7f     // 0.7 = stabil + güzel wah sesi
#define WAH_ATTACK      0.015f
#define WAH_RELEASE     0.004f
#define WAH_MAKEUP_GAIN 3.0f*/

// --- DELAY AYARLARI ---
#define DELAY_BUFFER_SIZE 16384  // 48 kHz'de ≈ 341 ms gecikme

/* ══════════════════════════════════════════════
   DAHİLİ DURUM DEĞİŞKENLERİ
   ══════════════════════════════════════════════ */
static filter_params_t current_params;

// --- Ring Modulator ---
static float carrier_phase     = 0.0f;
static float carrier_increment = 0.0f;

// --- LPF (2. derece — kaskat) ---
static float lpf_prev_out_L  = 0.0f, lpf_prev_out_R  = 0.0f;
static float lpf_prev_out2_L = 0.0f, lpf_prev_out2_R = 0.0f;
static float lpf_alpha = 1.0f;

// --- HPF (2. derece — kaskat) ---
static float hpf_prev_in_L   = 0.0f, hpf_prev_in_R   = 0.0f;
static float hpf_prev_out_L  = 0.0f, hpf_prev_out_R  = 0.0f;
static float hpf_prev_in2_L  = 0.0f, hpf_prev_in2_R  = 0.0f;
static float hpf_prev_out2_L = 0.0f, hpf_prev_out2_R = 0.0f;
static float hpf_alpha = 0.0f;

// --- Auto-Wah ---
static float env_level_L = 0.0f, env_level_R = 0.0f;
static float svf_low_L   = 0.0f, svf_band_L  = 0.0f;
static float svf_low_R   = 0.0f, svf_band_R  = 0.0f;

// --- Delay ---
static int16_t  delay_line[DELAY_BUFFER_SIZE];
static uint32_t delay_head = 0;

/* ══════════════════════════════════════════════
   YARDIMCI MATEMATİK
   ══════════════════════════════════════════════ */
static float Calculate_LPF_Alpha(float cutoff_freq) {
    if (cutoff_freq >= (SAMPLE_RATE / 2.0f)) return 1.0f;
    if (cutoff_freq <= 20.0f) return 0.0f;
    float dt = 1.0f / SAMPLE_RATE;
    float rc = 1.0f / (2.0f * PI * cutoff_freq);
    return dt / (rc + dt);
}

static float Calculate_HPF_Alpha(float cutoff_freq) {
    if (cutoff_freq >= (SAMPLE_RATE / 2.0f)) return 0.0f;
    if (cutoff_freq <= 20.0f) return 1.0f;
    float dt = 1.0f / SAMPLE_RATE;
    float rc = 1.0f / (2.0f * PI * cutoff_freq);
    return rc / (rc + dt);
}

/* ══════════════════════════════════════════════
   INIT & SET PARAMS
   ══════════════════════════════════════════════ */
void Filter_Init(filter_params_t *initial_params) {
    if (initial_params != NULL) {
        current_params = *initial_params;
    } else {
        current_params.master_volume      = 1.0f;
        current_params.ring_mod_enable    = 0;
        current_params.lpf_enable         = 0;
        current_params.hpf_enable         = 0;
        current_params.wah_enable         = 0;
        current_params.delay_enable       = 0;
        current_params.lpf_cutoff_freq    = 20000.0f;
        current_params.hpf_cutoff_freq    = 20.0f;
    }

    carrier_phase = 0.0f;

    lpf_prev_out_L = 0.0f;  lpf_prev_out_R = 0.0f;
    lpf_prev_out2_L = 0.0f; lpf_prev_out2_R = 0.0f;

    hpf_prev_in_L = 0.0f;   hpf_prev_in_R = 0.0f;
    hpf_prev_out_L = 0.0f;  hpf_prev_out_R = 0.0f;
    hpf_prev_in2_L = 0.0f;  hpf_prev_in2_R = 0.0f;
    hpf_prev_out2_L = 0.0f; hpf_prev_out2_R = 0.0f;

    env_level_L = 0.0f; env_level_R = 0.0f;
    svf_low_L = 0.0f;   svf_band_L = 0.0f;
    svf_low_R = 0.0f;   svf_band_R = 0.0f;

    for (int i = 0; i < DELAY_BUFFER_SIZE; i++) delay_line[i] = 0;
    delay_head = 0;

    Filter_Set_Params(&current_params);
}

void Filter_Set_Params(filter_params_t *new_params) {
    current_params = *new_params;

    carrier_increment = (2.0f * PI * current_params.ring_mod_freq) / SAMPLE_RATE;

    lpf_alpha = current_params.lpf_enable ? Calculate_LPF_Alpha(current_params.lpf_cutoff_freq) : 1.0f;
    hpf_alpha = current_params.hpf_enable ? Calculate_HPF_Alpha(current_params.hpf_cutoff_freq) : 1.0f;
}

/* ══════════════════════════════════════════════
   EFEKT FONKSİYONLARI
   ══════════════════════════════════════════════ */

static void Apply_RingModulator(float *sample_L, float *sample_R) {
    if (!current_params.ring_mod_enable) return;

    float carrier = sinf(carrier_phase);
    carrier_phase += carrier_increment;
    if (carrier_phase >= 2.0f * PI) carrier_phase -= 2.0f * PI;

    float mix = current_params.ring_mod_intensity;
    *sample_L = (*sample_L) * (1.0f - mix) + (*sample_L) * carrier * mix;
    *sample_R = (*sample_R) * (1.0f - mix) + (*sample_R) * carrier * mix;
}

static void Apply_LPF(float *sample_L, float *sample_R) {
    if (!current_params.lpf_enable) return;

    // Kademe 1 (+ Anti-Denormal Koruması)
    *sample_L = lpf_prev_out_L + lpf_alpha * ((*sample_L) - lpf_prev_out_L) + ANTI_DENORMAL;
    *sample_R = lpf_prev_out_R + lpf_alpha * ((*sample_R) - lpf_prev_out_R) + ANTI_DENORMAL;
    lpf_prev_out_L = *sample_L;
    lpf_prev_out_R = *sample_R;

    // Kademe 2
    *sample_L = lpf_prev_out2_L + lpf_alpha * ((*sample_L) - lpf_prev_out2_L) - ANTI_DENORMAL;
    *sample_R = lpf_prev_out2_R + lpf_alpha * ((*sample_R) - lpf_prev_out2_R) - ANTI_DENORMAL;
    lpf_prev_out2_L = *sample_L;
    lpf_prev_out2_R = *sample_R;
}

/*static void Apply_HPF(float *sample_L, float *sample_R) {
    if (!current_params.hpf_enable) return;

    float in_L = *sample_L;
    float in_R = *sample_R;

    // Kademe 1 (+ Anti-Denormal Koruması)
    float out_L = hpf_alpha * (hpf_prev_out_L + in_L - hpf_prev_in_L) + ANTI_DENORMAL;
    float out_R = hpf_alpha * (hpf_prev_out_R + in_R - hpf_prev_in_R) + ANTI_DENORMAL;
    hpf_prev_in_L  = in_L;
    hpf_prev_in_R  = in_R;
    hpf_prev_out_L = out_L;
    hpf_prev_out_R = out_R;

    // Kademe 2
    float out2_L = hpf_alpha * (hpf_prev_out2_L + out_L - hpf_prev_in2_L) - ANTI_DENORMAL;
    float out2_R = hpf_alpha * (hpf_prev_out2_R + out_R - hpf_prev_in2_R) - ANTI_DENORMAL;
    hpf_prev_in2_L  = out_L;
    hpf_prev_in2_R  = out_R;
    hpf_prev_out2_L = out2_L;
    hpf_prev_out2_R = out2_R;

    *sample_L = out2_L;
    *sample_R = out2_R;
}*/
static void Apply_HPF(float *sample_L, float *sample_R) {
    if (!current_params.hpf_enable) return;
    float in_L = *sample_L;
    float in_R = *sample_R;
    // ── 1. KADEME ────────────────────────────────────────────
    float out_L = hpf_alpha * (hpf_prev_out_L + in_L - hpf_prev_in_L) + ANTI_DENORMAL;
    float out_R = hpf_alpha * (hpf_prev_out_R + in_R - hpf_prev_in_R) + ANTI_DENORMAL;
    out_L -= ANTI_DENORMAL;
    out_R -= ANTI_DENORMAL;
    hpf_prev_in_L  = in_L;
    hpf_prev_in_R  = in_R;
    hpf_prev_out_L = out_L;
    hpf_prev_out_R = out_R;
    // ── 2. KADEME ────────────────────────────────────────────
    float out2_L = hpf_alpha * (hpf_prev_out2_L + out_L - hpf_prev_in2_L) - ANTI_DENORMAL;
    float out2_R = hpf_alpha * (hpf_prev_out2_R + out_R - hpf_prev_in2_R) - ANTI_DENORMAL;
    hpf_prev_in2_L  = out_L;
    hpf_prev_in2_R  = out_R;
    hpf_prev_out2_L = out2_L;
    hpf_prev_out2_R = out2_R;
    *sample_L = out2_L;
    *sample_R = out2_R;
}
static void Apply_AutoWah(float *sample_L, float *sample_R) {
    if (!current_params.wah_enable) return;

    float clean_L = *sample_L;
    float clean_R = *sample_R;

    float input_abs_L = fabsf(*sample_L);
    float input_abs_R = fabsf(*sample_R);

    // Envelope Follower (+ Anti-Denormal)
    env_level_L += (input_abs_L > env_level_L ? WAH_ATTACK : WAH_RELEASE) * (input_abs_L - env_level_L) + ANTI_DENORMAL;
    env_level_R += (input_abs_R > env_level_R ? WAH_ATTACK : WAH_RELEASE) * (input_abs_R - env_level_R) + ANTI_DENORMAL;

    float mod_L = fminf(fmaxf((env_level_L / 16000.0f) * current_params.wah_sensitivity, 0.0f), 1.0f);
    float mod_R = fminf(fmaxf((env_level_R / 16000.0f) * current_params.wah_sensitivity, 0.0f), 1.0f);

    float current_cutoff_L = WAH_MIN_FREQ + (WAH_MAX_FREQ - WAH_MIN_FREQ) * mod_L;
    float current_cutoff_R = WAH_MIN_FREQ + (WAH_MAX_FREQ - WAH_MIN_FREQ) * mod_R;

    float f_L = 2.0f * sinf(PI * current_cutoff_L / SAMPLE_RATE);
    float f_R = 2.0f * sinf(PI * current_cutoff_R / SAMPLE_RATE);

    // SVF Sol Kanal
    svf_low_L += f_L * svf_band_L;
    float high_L = (*sample_L) - svf_low_L - (WAH_Q_FACTOR * svf_band_L);
    svf_band_L += f_L * high_L;

    // SVF Sağ Kanal
    svf_low_R += f_R * svf_band_R;
    float high_R = (*sample_R) - svf_low_R - (WAH_Q_FACTOR * svf_band_R);
    svf_band_R += f_R * high_R;

    *sample_L = (clean_L * (1.0f - current_params.wah_mix)) + (svf_band_L * WAH_MAKEUP_GAIN * current_params.wah_mix);
    *sample_R = (clean_R * (1.0f - current_params.wah_mix)) + (svf_band_R * WAH_MAKEUP_GAIN * current_params.wah_mix);
}


static void Apply_Delay(float *sample_L, float *sample_R) {
    if (!current_params.delay_enable) return;
    // --- ZAMAN (TIME) AYARI ---
    uint32_t delay_time_samples = DELAY_BUFFER_SIZE / 3;
    int32_t read_idx = delay_head - delay_time_samples;
    if (read_idx < 0) read_idx += DELAY_BUFFER_SIZE;
    // --------------------------
    float delayed_sample = (float)delay_line[read_idx];
    // --- TAPE ECHO LPF ---
    static float tape_lpf = 0.0f;
    tape_lpf += 0.75f * (delayed_sample - tape_lpf);
    delayed_sample = tape_lpf;
    float input_mono = (*sample_L + *sample_R) * 0.5f;
    float write_val = input_mono + (delayed_sample * current_params.delay_feedback);
    // --- KIRPMA (SOFT CLIPPING)   ---
    write_val = fminf(fmaxf(write_val, -32000.0f), 32000.0f);
    delay_line[delay_head] = (int16_t)write_val;
    delay_head = (delay_head + 1) % DELAY_BUFFER_SIZE;
    *sample_L += delayed_sample * current_params.delay_mix;
    *sample_R += delayed_sample * current_params.delay_mix;
}
static void Apply_MasterVolume_And_Clip(float *sample_L, float *sample_R) {
    *sample_L *= current_params.master_volume;
    *sample_R *= current_params.master_volume;

    *sample_L = fminf(fmaxf(*sample_L, -32768.0f), 32767.0f);
    *sample_R = fminf(fmaxf(*sample_R, -32768.0f), 32767.0f);
}

/* ══════════════════════════════════════════════
   ANA İŞLEME FONKSİYONU
   ══════════════════════════════════════════════ */
void Filter_Apply(int16_t *buffer, uint32_t num_samples) {
    float sample_L, sample_R;

    for (uint32_t i = 0; i < num_samples; i += 2) {
        sample_L = (float)buffer[i];
        sample_R = (float)buffer[i + 1];

        Apply_RingModulator(&sample_L, &sample_R);
        Apply_LPF(&sample_L, &sample_R);
        Apply_HPF(&sample_L, &sample_R);
        Apply_AutoWah(&sample_L, &sample_R);
        Apply_Delay(&sample_L, &sample_R);

        Apply_MasterVolume_And_Clip(&sample_L, &sample_R);

        buffer[i]     = (int16_t)sample_L;
        buffer[i + 1] = (int16_t)sample_R;
    }
}
