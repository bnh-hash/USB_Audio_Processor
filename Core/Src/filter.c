/*
 * filter.c
 *
 *  Created on: Mar 2, 2026
 *      Author: Gökçe
 */
#include "filter.h"
#include <math.h>

// --- SABITLER ---
#define WAH_MIN_FREQ  500.0f
#define WAH_MAX_FREQ  3000.0f
#define WAH_Q_FACTOR  0.15f
#define WAH_ATTACK    0.01f
#define WAH_RELEASE   0.005f

// --- DELAY AYARLARI ---
// 16384 ornek * 2 byte = 32KB RAM harcar.
// 48kHz'de yaklasik 341ms gecikme saglar (DJ setleri icin ideal ritmik eko).
#define DELAY_BUFFER_SIZE 16384

// --- INTERNAL STATE (Dahili Durum Degiskenleri) ---
static filter_params_t current_params;

// Ring Modulator State
static float carrier_phase = 0.0f;
static float carrier_increment = 0.0f;

// LPF State
static float lpf_prev_out_L = 0.0f;
static float lpf_prev_out_R = 0.0f;
static float lpf_alpha = 1.0f;

// HPF State
static float hpf_prev_in_L = 0.0f;
static float hpf_prev_in_R = 0.0f;
static float hpf_prev_out_L = 0.0f;
static float hpf_prev_out_R = 0.0f;
static float hpf_alpha = 0.0f;

// Auto-Wah State
static float env_level_L = 0.0f;
static float env_level_R = 0.0f;
static float svf_low_L = 0.0f, svf_band_L = 0.0f;
static float svf_low_R = 0.0f, svf_band_R = 0.0f;

// Bitcrusher State
static float crush_hold_L = 0.0f;
static float crush_hold_R = 0.0f;
static uint32_t crush_counter = 0;

// Delay State (YENI)
static int16_t delay_line[DELAY_BUFFER_SIZE];
static uint32_t delay_head = 0;

// --- YARDIMCI MATEMATIK ---

static float Calculate_LPF_Alpha(float cutoff_freq) {
    if (cutoff_freq >= (SAMPLE_RATE / 2.0f)) return 1.0f;
    if (cutoff_freq <= 0.0f) return 0.0f;
    float dt = 1.0f / SAMPLE_RATE;
    float rc = 1.0f / (2.0f * PI * cutoff_freq);
    return dt / (rc + dt);
}

static float Calculate_HPF_Alpha(float cutoff_freq) {
    if (cutoff_freq >= (SAMPLE_RATE / 2.0f)) return 0.0f;
    if (cutoff_freq <= 0.0f) return 1.0f;
    float dt = 1.0f / SAMPLE_RATE;
    float rc = 1.0f / (2.0f * PI * cutoff_freq);
    return rc / (rc + dt);
}

// --- INIT & SET PARAMS ---

void Filter_Init(filter_params_t *initial_params) {
    if (initial_params != NULL) {
        current_params = *initial_params;
    } else {
        current_params.master_volume = 1.0f;
        current_params.ring_mod_enable = 0;
        current_params.lpf_enable = 0;
        current_params.hpf_enable = 0;
        current_params.wah_enable = 0;
        current_params.bitcrush_enable = 0;
        current_params.delay_enable = 0;
    }

    // State Sifirlama
    carrier_phase = 0.0f;
    lpf_prev_out_L = 0.0f; lpf_prev_out_R = 0.0f;
    hpf_prev_in_L = 0.0f; hpf_prev_in_R = 0.0f;
    hpf_prev_out_L = 0.0f; hpf_prev_out_R = 0.0f;
    env_level_L = 0.0f; env_level_R = 0.0f;
    svf_low_L = 0.0f; svf_band_L = 0.0f;
    svf_low_R = 0.0f; svf_band_R = 0.0f;
    crush_hold_L = 0.0f; crush_hold_R = 0.0f;
    crush_counter = 0;

    // Delay hatti sifirlama
    for(int i = 0; i < DELAY_BUFFER_SIZE; i++) delay_line[i] = 0;
    delay_head = 0;

    Filter_Set_Params(&current_params);
}

void Filter_Set_Params(filter_params_t *new_params) {
    current_params = *new_params;

    carrier_increment = (2.0f * PI * current_params.ring_mod_freq) / SAMPLE_RATE;

    if (current_params.lpf_enable) lpf_alpha = Calculate_LPF_Alpha(current_params.lpf_cutoff_freq);
    else lpf_alpha = 1.0f;

    if (current_params.hpf_enable) hpf_alpha = Calculate_HPF_Alpha(current_params.hpf_cutoff_freq);
    else hpf_alpha = 1.0f;
}

// --- EFEKT FONKSIYONLARI ---

static void Apply_RingModulator(float *sample_L, float *sample_R) {
    if (!current_params.ring_mod_enable) return;
    float carrier = sinf(carrier_phase);
    carrier_phase += carrier_increment;
    if (carrier_phase >= 2.0f * PI) carrier_phase -= 2.0f * PI;
    float wet_L = (*sample_L) * carrier;
    float wet_R = (*sample_R) * carrier;
    *sample_L = (*sample_L) * (1.0f - current_params.ring_mod_intensity) + wet_L * current_params.ring_mod_intensity;
    *sample_R = (*sample_R) * (1.0f - current_params.ring_mod_intensity) + wet_R * current_params.ring_mod_intensity;
}

static void Apply_LPF(float *sample_L, float *sample_R) {
    if (!current_params.lpf_enable) return;
    *sample_L = lpf_prev_out_L + lpf_alpha * ((*sample_L) - lpf_prev_out_L);
    *sample_R = lpf_prev_out_R + lpf_alpha * ((*sample_R) - lpf_prev_out_R);
    lpf_prev_out_L = *sample_L; lpf_prev_out_R = *sample_R;
}

static void Apply_HPF(float *sample_L, float *sample_R) {
    if (!current_params.hpf_enable) return;
    float in_L = *sample_L; float in_R = *sample_R;
    float out_L = hpf_alpha * (hpf_prev_out_L + in_L - hpf_prev_in_L);
    float out_R = hpf_alpha * (hpf_prev_out_R + in_R - hpf_prev_in_R);
    hpf_prev_in_L = in_L; hpf_prev_in_R = in_R;
    hpf_prev_out_L = out_L; hpf_prev_out_R = out_R;
    *sample_L = out_L; *sample_R = out_R;
}

#define WAH_MAKEUP_GAIN  4.0f
static void Apply_AutoWah(float *sample_L, float *sample_R) {
    // 1. Wah kapalıysa işlem yapmadan çık
    if (!current_params.wah_enable) {
        return;
    }

    // Orijinal (Clean) sinyalleri sakla
    float clean_L = *sample_L;
    float clean_R = *sample_R;

    // --- ENVELOPE FOLLOWER (Zarf Takipçisi) ---
    // Giriş sinyalinin mutlak değerini (genliğini) al
    float input_abs_L = fabsf(*sample_L);
    float input_abs_R = fabsf(*sample_R);

    // Sol Kanal Envelope
    if (input_abs_L > env_level_L) {
        env_level_L = WAH_ATTACK * input_abs_L + (1.0f - WAH_ATTACK) * env_level_L;
    } else {
        env_level_L = WAH_RELEASE * input_abs_L + (1.0f - WAH_RELEASE) * env_level_L;
    }

    // Sağ Kanal Envelope
    if (input_abs_R > env_level_R) {
        env_level_R = WAH_ATTACK * input_abs_R + (1.0f - WAH_ATTACK) * env_level_R;
    } else {
        env_level_R = WAH_RELEASE * input_abs_R + (1.0f - WAH_RELEASE) * env_level_R;
    }

    // --- FREKANS MODÜLASYONU ---
    // Envelope değerini 0.0 - 1.0 arasına normalize etmeye çalışıyoruz.
    // NOT: 10000.0f değeri giriş sinyalinizin genliğine (int16 veya float) göre değişebilir.
    float mod_L = (env_level_L / 10000.0f) * current_params.wah_sensitivity;
    float mod_R = (env_level_R / 10000.0f) * current_params.wah_sensitivity;

    // Sınırlandırma (Clamping) - Derleyici uyarısının olduğu yer burasıydı
    if (mod_L > 1.0f) { mod_L = 1.0f; }
    if (mod_L < 0.0f) { mod_L = 0.0f; }

    if (mod_R > 1.0f) { mod_R = 1.0f; }
    if (mod_R < 0.0f) { mod_R = 0.0f; }

    // Modülasyonu frekans aralığına eşle
    float current_cutoff_L = WAH_MIN_FREQ + (WAH_MAX_FREQ - WAH_MIN_FREQ) * mod_L;
    float current_cutoff_R = WAH_MIN_FREQ + (WAH_MAX_FREQ - WAH_MIN_FREQ) * mod_R;

    // --- STATE VARIABLE FILTER (SVF) HESAPLAMASI ---
    // Chamberlin SVF Formülü
    // f = 2 * sin(pi * Fc / Fs)
    float f_L = 2.0f * sinf(PI * current_cutoff_L / SAMPLE_RATE);
    float f_R = 2.0f * sinf(PI * current_cutoff_R / SAMPLE_RATE);

    // Sol Kanal Filtre İşlemi
    svf_low_L += f_L * svf_band_L;
    float high_L = (*sample_L) - svf_low_L - (WAH_Q_FACTOR * svf_band_L);
    svf_band_L += f_L * high_L;

    // Sağ Kanal Filtre İşlemi
    svf_low_R += f_R * svf_band_R;
    float high_R = (*sample_R) - svf_low_R - (WAH_Q_FACTOR * svf_band_R);
    svf_band_R += f_R * high_R;

    // --- ÇIKIŞ KARIŞIMI (MIX) ---
    // Islak (Efektli) sinyale biraz kazanç (Gain) ekliyoruz çünkü Bandpass filtre sesi kısar.
    float wet_L = svf_band_L * WAH_MAKEUP_GAIN;
    float wet_R = svf_band_R * WAH_MAKEUP_GAIN;

    // Dry/Wet Mix
    *sample_L = (clean_L * (1.0f - current_params.wah_mix)) + (wet_L * current_params.wah_mix);
    *sample_R = (clean_R * (1.0f - current_params.wah_mix)) + (wet_R * current_params.wah_mix);
}

// --- BITCRUSHER & ANTI-ALIASING STATE ---
static float bc_holdL = 0, bc_holdR = 0;
static uint32_t bc_cnt = 0;

// Anti-Aliasing LPF State (Bitcrusher'a ozel)
static float bc_lpf_L = 0, bc_lpf_R = 0;

static void Apply_Bitcrusher(float *sample_L, float *sample_R) {
    if (!current_params.bitcrush_enable) return;

    // 1. DOWNSAMPLING (Sample Rate Reduction)
    // Ne kadar cok örnek atlarsak ses o kadar "lo-fi" olur.
    uint32_t ds_factor = 1 + (uint32_t)(current_params.bitcrush_amount * 16.0f);

    if (++bc_cnt >= ds_factor) {
        bc_cnt = 0;
        bc_holdL = *sample_L;
        bc_holdR = *sample_R;
    } else {
        // Eski örneği tutarak o "blocky" sesi yaratıyoruz
        *sample_L = bc_holdL;
        *sample_R = bc_holdR;
    }

    // 2. QUANTIZATION (Bit Depth Reduction)
    // Değerleri basamaklara ayırıyoruz.
    // m değeri basamakların büyüklüğünü belirler.
    float m = 1.0f + (current_params.bitcrush_amount * current_params.bitcrush_amount * 1500.0f);
    float inv_m = 1.0f / m;

    *sample_L = (float)((int32_t)(*sample_L * inv_m)) * m;
    *sample_R = (float)((int32_t)(*sample_R * inv_m)) * m;

    // 3. ANTI-ALIASING LPF (Kritik Nokta!)
    // O tiz cızırtıları yok eden yumuşatma filtresi.
    // Denklem: y[n] = a * x[n] + (1-a) * y[n-1]
    // Alpha değerini bitcrush miktarına göre dinamik yapıyoruz.
    // Bitcrush arttıkça filtre daha çok kapanmalı.
    float alpha = 1.2f - (current_params.bitcrush_amount * 0.8f);

    bc_lpf_L = (alpha * (*sample_L)) + ((1.0f - alpha) * bc_lpf_L);
    bc_lpf_R = (alpha * (*sample_R)) + ((1.0f - alpha) * bc_lpf_R);

    *sample_L = bc_lpf_L;
    *sample_R = bc_lpf_R;
}

// --- 6. DELAY (YANKI) FONKSIYONU ---
static void Apply_Delay(float *sample_L, float *sample_R) {
    if (!current_params.delay_enable) return;

    // Gecikmis ornegi hattan oku
    float delayed_sample = (float)delay_line[delay_head];

    // Hafiza tasarrufu icin delay'i mono isliyoruz
    float input_mono = (*sample_L + *sample_R) * 0.5f;

    // Hatta yeni veriyi yaz (Giris + Feedback * Gecikmis Sinyal)
    float write_val = input_mono + (delayed_sample * current_params.delay_feedback);

    // Soft Clipping koruması
    if (write_val > 32000.0f) write_val = 32000.0f;
    else if (write_val < -32000.0f) write_val = -32000.0f;

    delay_line[delay_head] = (int16_t)write_val;

    // Pointer'ı dairesel olarak ilerlet
    delay_head = (delay_head + 1) % DELAY_BUFFER_SIZE;

    // Mix (Islak sinyali ana kanallara ekle)
    *sample_L += delayed_sample * current_params.delay_mix;
    *sample_R += delayed_sample * current_params.delay_mix;
}

static void Apply_MasterVolume_And_Clip(float *sample_L, float *sample_R) {
    *sample_L *= current_params.master_volume;
    *sample_R *= current_params.master_volume;
    if (*sample_L > 32767.0f) *sample_L = 32767.0f; else if (*sample_L < -32768.0f) *sample_L = -32768.0f;
    if (*sample_R > 32767.0f) *sample_R = 32767.0f; else if (*sample_R < -32768.0f) *sample_R = -32768.0f;
}

// --- ANA ISLEME FONKSIYONU ---

void Filter_Apply(int16_t *buffer, uint32_t num_samples) {
    float sample_L, sample_R;

    for (uint32_t i = 0; i < num_samples; i += 2) {
        sample_L = (float)buffer[i];
        sample_R = (float)buffer[i+1];

        // --- EFEKT ZINCIRI ---
        Apply_RingModulator(&sample_L, &sample_R);
        Apply_LPF(&sample_L, &sample_R);
        Apply_HPF(&sample_L, &sample_R);
        Apply_AutoWah(&sample_L, &sample_R);
        Apply_Bitcrusher(&sample_L, &sample_R);
        Apply_Delay(&sample_L, &sample_R); // En son Delay (Eko)

        // --- CIKIS ---
        Apply_MasterVolume_And_Clip(&sample_L, &sample_R);

        buffer[i]   = (int16_t)sample_L;
        buffer[i+1] = (int16_t)sample_R;
    }
}
