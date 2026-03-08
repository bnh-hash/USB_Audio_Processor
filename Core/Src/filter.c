/*
 * filter.c
 *
 *  Created on: Mar 2, 2026
 *      Author: Gökçe
 *
 *  DSP Efekt Zinciri:
 *    RingMod → LPF (2. derece) → HPF (2. derece) → AutoWah → Bitcrusher → Delay → Volume
 */
#include "filter.h"
#include <math.h>

/* ══════════════════════════════════════════════
   SABITLER
   ══════════════════════════════════════════════ */

// --- WAH AYARLARI ---
#define WAH_MIN_FREQ    500.0f
#define WAH_MAX_FREQ    2500.0f
#define WAH_Q_FACTOR    0.7f     // Sönümleme: düşük=çok rezonans (gürültü!), yüksek=az rezonans
                                 // 0.15 -> kararsız SVF! 0.7 = stabil + güzel wah sesi
#define WAH_ATTACK      0.015f   // Zarf yükselme hızı
#define WAH_RELEASE     0.004f   // Zarf düşme hızı
#define WAH_MAKEUP_GAIN 3.0f     // Bandpass çıkışı kazancı

// --- DELAY AYARLARI ---
// 16384 örnek × 2 byte = 32 KB RAM.
// 48 kHz'de ≈ 341 ms gecikme (ritmik eko için ideal).
#define DELAY_BUFFER_SIZE 16384

/* ══════════════════════════════════════════════
   DAHİLİ DURUM DEĞİŞKENLERİ
   ══════════════════════════════════════════════ */
static filter_params_t current_params;

// --- Ring Modulator ---
static float carrier_phase     = 0.0f;
static float carrier_increment = 0.0f;

// --- LPF (2. derece — kaskat) ---
static float lpf_prev_out_L  = 0.0f;
static float lpf_prev_out_R  = 0.0f;
static float lpf_prev_out2_L = 0.0f;   // 2. kademe
static float lpf_prev_out2_R = 0.0f;   // 2. kademe
static float lpf_alpha = 1.0f;

// --- HPF (2. derece — kaskat) ---
static float hpf_prev_in_L   = 0.0f;
static float hpf_prev_in_R   = 0.0f;
static float hpf_prev_out_L  = 0.0f;
static float hpf_prev_out_R  = 0.0f;
static float hpf_prev_in2_L  = 0.0f;   // 2. kademe
static float hpf_prev_in2_R  = 0.0f;   // 2. kademe
static float hpf_prev_out2_L = 0.0f;   // 2. kademe
static float hpf_prev_out2_R = 0.0f;   // 2. kademe
static float hpf_alpha = 0.0f;

// --- Auto-Wah ---
static float env_level_L = 0.0f;
static float env_level_R = 0.0f;
static float svf_low_L   = 0.0f, svf_band_L = 0.0f;
static float svf_low_R   = 0.0f, svf_band_R = 0.0f;

// --- Bitcrusher ---
static float    bc_holdL = 0.0f, bc_holdR = 0.0f;
static uint32_t bc_cnt   = 0;
static float    bc_lpf_L = 0.0f, bc_lpf_R = 0.0f;  // Anti-aliasing LPF

// --- Delay ---
static int16_t  delay_line[DELAY_BUFFER_SIZE];
static uint32_t delay_head = 0;

/* ══════════════════════════════════════════════
   YARDIMCI MATEMATİK
   ══════════════════════════════════════════════ */

/**
 * @brief 1. derece LPF katsayısı hesapla.
 *        alpha = dt / (RC + dt)
 *        alpha → 0: filtre tamamen kapalı (çıkış = 0)
 *        alpha → 1: filtre şeffaf (çıkış = giriş)
 */
static float Calculate_LPF_Alpha(float cutoff_freq) {
    if (cutoff_freq >= (SAMPLE_RATE / 2.0f)) return 1.0f;
    if (cutoff_freq <= 20.0f) return 0.0f;
    float dt = 1.0f / SAMPLE_RATE;
    float rc = 1.0f / (2.0f * PI * cutoff_freq);
    return dt / (rc + dt);
}

/**
 * @brief 1. derece HPF katsayısı hesapla.
 *        alpha = RC / (RC + dt)
 *        alpha → 1: yalnız yüksek frekanslar geçer (agresif filtre)
 *        alpha → 0: filtre şeffaf
 */
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
        current_params.bitcrush_enable    = 0;
        current_params.delay_enable       = 0;
        current_params.lpf_cutoff_freq    = 20000.0f;
        current_params.hpf_cutoff_freq    = 20.0f;
    }

    // --- Tüm state sıfırlama ---
    carrier_phase = 0.0f;

    // LPF (2 kademe)
    lpf_prev_out_L = 0.0f;  lpf_prev_out_R = 0.0f;
    lpf_prev_out2_L = 0.0f; lpf_prev_out2_R = 0.0f;

    // HPF (2 kademe)
    hpf_prev_in_L = 0.0f;   hpf_prev_in_R = 0.0f;
    hpf_prev_out_L = 0.0f;  hpf_prev_out_R = 0.0f;
    hpf_prev_in2_L = 0.0f;  hpf_prev_in2_R = 0.0f;
    hpf_prev_out2_L = 0.0f; hpf_prev_out2_R = 0.0f;

    // Wah
    env_level_L = 0.0f; env_level_R = 0.0f;
    svf_low_L = 0.0f;   svf_band_L = 0.0f;
    svf_low_R = 0.0f;   svf_band_R = 0.0f;

    // Bitcrusher
    bc_holdL = 0.0f; bc_holdR = 0.0f;
    bc_cnt = 0;
    bc_lpf_L = 0.0f; bc_lpf_R = 0.0f;

    // Delay
    for (int i = 0; i < DELAY_BUFFER_SIZE; i++) delay_line[i] = 0;
    delay_head = 0;

    Filter_Set_Params(&current_params);
}

void Filter_Set_Params(filter_params_t *new_params) {
    current_params = *new_params;

    carrier_increment = (2.0f * PI * current_params.ring_mod_freq) / SAMPLE_RATE;

    if (current_params.lpf_enable)
        lpf_alpha = Calculate_LPF_Alpha(current_params.lpf_cutoff_freq);
    else
        lpf_alpha = 1.0f;

    if (current_params.hpf_enable)
        hpf_alpha = Calculate_HPF_Alpha(current_params.hpf_cutoff_freq);
    else
        hpf_alpha = 1.0f;
}

/* ══════════════════════════════════════════════
   EFEKT FONKSİYONLARI
   ══════════════════════════════════════════════ */

/* ─── 1. RING MODULATOR ─── */
static void Apply_RingModulator(float *sample_L, float *sample_R) {
    if (!current_params.ring_mod_enable) return;

    float carrier = sinf(carrier_phase);
    carrier_phase += carrier_increment;
    if (carrier_phase >= 2.0f * PI) carrier_phase -= 2.0f * PI;

    float mix = current_params.ring_mod_intensity;
    *sample_L = (*sample_L) * (1.0f - mix) + (*sample_L) * carrier * mix;
    *sample_R = (*sample_R) * (1.0f - mix) + (*sample_R) * carrier * mix;
}

/* ─── 2. LOW-PASS FILTER (2. Derece — Kaskat) ─── */
/*
 * 2×1. derece = 12 dB/oktav eğim.
 * Tek kademe (6 dB/oktav) çok hafif kalıyordu → kaskat ile
 * efekt net duyulur hale geldi.
 */
static void Apply_LPF(float *sample_L, float *sample_R) {
    if (!current_params.lpf_enable) return;

    // Kademe 1
    *sample_L = lpf_prev_out_L + lpf_alpha * ((*sample_L) - lpf_prev_out_L);
    *sample_R = lpf_prev_out_R + lpf_alpha * ((*sample_R) - lpf_prev_out_R);
    lpf_prev_out_L = *sample_L;
    lpf_prev_out_R = *sample_R;

    // Kademe 2
    *sample_L = lpf_prev_out2_L + lpf_alpha * ((*sample_L) - lpf_prev_out2_L);
    *sample_R = lpf_prev_out2_R + lpf_alpha * ((*sample_R) - lpf_prev_out2_R);
    lpf_prev_out2_L = *sample_L;
    lpf_prev_out2_R = *sample_R;
}

/* ─── 3. HIGH-PASS FILTER (2. Derece — Kaskat) ─── */
static void Apply_HPF(float *sample_L, float *sample_R) {
    if (!current_params.hpf_enable) return;

    float in_L = *sample_L;
    float in_R = *sample_R;

    // Kademe 1
    float out_L = hpf_alpha * (hpf_prev_out_L + in_L - hpf_prev_in_L);
    float out_R = hpf_alpha * (hpf_prev_out_R + in_R - hpf_prev_in_R);
    hpf_prev_in_L  = in_L;
    hpf_prev_in_R  = in_R;
    hpf_prev_out_L = out_L;
    hpf_prev_out_R = out_R;

    // Kademe 2
    float out2_L = hpf_alpha * (hpf_prev_out2_L + out_L - hpf_prev_in2_L);
    float out2_R = hpf_alpha * (hpf_prev_out2_R + out_R - hpf_prev_in2_R);
    hpf_prev_in2_L  = out_L;
    hpf_prev_in2_R  = out_R;
    hpf_prev_out2_L = out2_L;
    hpf_prev_out2_R = out2_R;

    *sample_L = out2_L;
    *sample_R = out2_R;
}

/* ─── 4. AUTO-WAH (Envelope Bandpass Filter) ─── */
static void Apply_AutoWah(float *sample_L, float *sample_R) {
    if (!current_params.wah_enable) return;

    float clean_L = *sample_L;
    float clean_R = *sample_R;

    // --- ENVELOPE FOLLOWER ---
    float input_abs_L = fabsf(*sample_L);
    float input_abs_R = fabsf(*sample_R);

    // Sol kanal envelope
    if (input_abs_L > env_level_L)
        env_level_L += WAH_ATTACK  * (input_abs_L - env_level_L);
    else
        env_level_L += WAH_RELEASE * (input_abs_L - env_level_L);

    // Sağ kanal envelope
    if (input_abs_R > env_level_R)
        env_level_R += WAH_ATTACK  * (input_abs_R - env_level_R);
    else
        env_level_R += WAH_RELEASE * (input_abs_R - env_level_R);

    // --- FREKANS MODÜLASYONU ---
    // int16 aralığı: max ≈ 32767. Normalize böleni buna göre.
    float mod_L = (env_level_L / 16000.0f) * current_params.wah_sensitivity;
    float mod_R = (env_level_R / 16000.0f) * current_params.wah_sensitivity;

    // Clamp 0–1
    if (mod_L > 1.0f) mod_L = 1.0f;
    if (mod_L < 0.0f) mod_L = 0.0f;
    if (mod_R > 1.0f) mod_R = 1.0f;
    if (mod_R < 0.0f) mod_R = 0.0f;

    float current_cutoff_L = WAH_MIN_FREQ + (WAH_MAX_FREQ - WAH_MIN_FREQ) * mod_L;
    float current_cutoff_R = WAH_MIN_FREQ + (WAH_MAX_FREQ - WAH_MIN_FREQ) * mod_R;

    // --- STATE VARIABLE FILTER (Chamberlin SVF) ---
    // Stabilite koşulu: f < 2 * q
    // q=0.7, max f = 2*sin(pi*2500/48000) ≈ 0.327  →  0.327 < 1.4 ✓
    float f_L = 2.0f * sinf(PI * current_cutoff_L / SAMPLE_RATE);
    float f_R = 2.0f * sinf(PI * current_cutoff_R / SAMPLE_RATE);

    // Sol kanal
    svf_low_L += f_L * svf_band_L;
    float high_L = (*sample_L) - svf_low_L - (WAH_Q_FACTOR * svf_band_L);
    svf_band_L += f_L * high_L;

    // Sağ kanal
    svf_low_R += f_R * svf_band_R;
    float high_R = (*sample_R) - svf_low_R - (WAH_Q_FACTOR * svf_band_R);
    svf_band_R += f_R * high_R;

    // --- ÇIKIŞ MIX ---
    float wet_L = svf_band_L * WAH_MAKEUP_GAIN;
    float wet_R = svf_band_R * WAH_MAKEUP_GAIN;

    *sample_L = (clean_L * (1.0f - current_params.wah_mix)) + (wet_L * current_params.wah_mix);
    *sample_R = (clean_R * (1.0f - current_params.wah_mix)) + (wet_R * current_params.wah_mix);
}

/* ─── 5. BITCRUSHER ─── */
static void Apply_Bitcrusher(float *sample_L, float *sample_R) {
    if (!current_params.bitcrush_enable) return;

    // 1. DOWNSAMPLING
    uint32_t ds_factor = 1 + (uint32_t)(current_params.bitcrush_amount * 16.0f);

    if (++bc_cnt >= ds_factor) {
        bc_cnt = 0;
        bc_holdL = *sample_L;
        bc_holdR = *sample_R;
    } else {
        *sample_L = bc_holdL;
        *sample_R = bc_holdR;
    }

    // 2. QUANTIZATION
    float m     = 1.0f + (current_params.bitcrush_amount * current_params.bitcrush_amount * 1500.0f);
    float inv_m = 1.0f / m;

    *sample_L = (float)((int32_t)(*sample_L * inv_m)) * m;
    *sample_R = (float)((int32_t)(*sample_R * inv_m)) * m;

    // 3. ANTI-ALIASING LPF
    // Önceki hata: alpha = 1.2 olabiliyordu (>1 → kararsız!)
    // Düzeltme: 0.25 – 0.95 aralığında sınırlandırıldı
    float alpha = 0.95f - (current_params.bitcrush_amount * 0.7f);

    bc_lpf_L = (alpha * (*sample_L)) + ((1.0f - alpha) * bc_lpf_L);
    bc_lpf_R = (alpha * (*sample_R)) + ((1.0f - alpha) * bc_lpf_R);

    *sample_L = bc_lpf_L;
    *sample_R = bc_lpf_R;
}

/* ─── 6. DELAY (YANKI) ─── */
static void Apply_Delay(float *sample_L, float *sample_R) {
    if (!current_params.delay_enable) return;

    // Gecikmeli örneği oku
    float delayed_sample = (float)delay_line[delay_head];

    // Mono mixdown (RAM tasarrufu)
    float input_mono = (*sample_L + *sample_R) * 0.5f;

    // Hatta yeni veri yaz (Giriş + Feedback × Gecikmeli Sinyal)
    float write_val = input_mono + (delayed_sample * current_params.delay_feedback);

    // Soft clipping koruması
    if (write_val > 32000.0f) write_val = 32000.0f;
    else if (write_val < -32000.0f) write_val = -32000.0f;

    delay_line[delay_head] = (int16_t)write_val;

    // Dairesel pointer
    delay_head = (delay_head + 1) % DELAY_BUFFER_SIZE;

    // Mix (gecikmeli sinyali ana kanallara ekle)
    *sample_L += delayed_sample * current_params.delay_mix;
    *sample_R += delayed_sample * current_params.delay_mix;
}

/* ─── MASTER VOLUME & CLIP ─── */
static void Apply_MasterVolume_And_Clip(float *sample_L, float *sample_R) {
    *sample_L *= current_params.master_volume;
    *sample_R *= current_params.master_volume;

    if (*sample_L >  32767.0f) *sample_L =  32767.0f;
    else if (*sample_L < -32768.0f) *sample_L = -32768.0f;

    if (*sample_R >  32767.0f) *sample_R =  32767.0f;
    else if (*sample_R < -32768.0f) *sample_R = -32768.0f;
}

/* ══════════════════════════════════════════════
   ANA İŞLEME FONKSİYONU
   ══════════════════════════════════════════════ */

void Filter_Apply(int16_t *buffer, uint32_t num_samples) {
    float sample_L, sample_R;

    for (uint32_t i = 0; i < num_samples; i += 2) {
        sample_L = (float)buffer[i];
        sample_R = (float)buffer[i + 1];

        // --- EFEKT ZİNCİRİ ---
        Apply_RingModulator(&sample_L, &sample_R);
        Apply_LPF(&sample_L, &sample_R);
        Apply_HPF(&sample_L, &sample_R);
        Apply_AutoWah(&sample_L, &sample_R);
        Apply_Bitcrusher(&sample_L, &sample_R);
        Apply_Delay(&sample_L, &sample_R);

        // --- ÇIKIŞ ---
        Apply_MasterVolume_And_Clip(&sample_L, &sample_R);

        buffer[i]     = (int16_t)sample_L;
        buffer[i + 1] = (int16_t)sample_R;
    }
}
