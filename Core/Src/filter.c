#include "filter.h"
#include <math.h>
#include <stdlib.h>
#include "OLED.h"

#define ANTI_DENORMAL 1e-18f

// Orijinal parametre taşıyıcısı
static filter_params_t current_params;

/* ════════════ EFEKT DURUM (STATE) DEĞİŞKENLERİ ════════════ */

static float ring_mod_phase = 0.0f;
static float smooth_lpf_freq = 10000.0f;
static float smooth_hpf_freq = 20.0f;

static float lpf_low_L = 0.0f, lpf_band_L = 0.0f;
static float lpf_low_R = 0.0f, lpf_band_R = 0.0f;

static float hpf_low_L = 0.0f, hpf_band_L = 0.0f;
static float hpf_low_R = 0.0f, hpf_band_R = 0.0f;

static int16_t delay_line[DELAY_BUFFER_SIZE];
static uint32_t delay_head = 0;
static float smooth_delay_samples = 1000.0f;
static float tape_lpf = 0.0f;

static float p_apf1_L = 0.0f, p_apf2_L = 0.0f, p_apf3_L = 0.0f, p_apf4_L = 0.0f;
static float p_apf1_R = 0.0f, p_apf2_R = 0.0f, p_apf3_R = 0.0f, p_apf4_R = 0.0f;
static float p_lfo = 0.0f;

/* ════════════ BAŞLATMA VE PARAMETRE GÜNCELLEME ════════════ */

void Filter_Init(filter_params_t *init_params) {
    if (init_params != NULL) {
        current_params = *init_params;
    }

    ring_mod_phase = 0.0f;
    lpf_low_L = 0.0f; lpf_band_L = 0.0f; lpf_low_R = 0.0f; lpf_band_R = 0.0f;
    hpf_low_L = 0.0f; hpf_band_L = 0.0f; hpf_low_R = 0.0f; hpf_band_R = 0.0f;

    for (int i = 0; i < DELAY_BUFFER_SIZE; i++) delay_line[i] = 0;
    delay_head = 0;
    smooth_delay_samples = 1000.0f;
    tape_lpf = 0.0f;

    p_apf1_L = 0.0f; p_apf2_L = 0.0f; p_apf3_L = 0.0f; p_apf4_L = 0.0f;
    p_apf1_R = 0.0f; p_apf2_R = 0.0f; p_apf3_R = 0.0f; p_apf4_R = 0.0f;
    p_lfo = 0.0f;
}

void Filter_Set_Params(filter_params_t *new_params) {
    if (new_params != NULL) {
        current_params = *new_params;
    }
}

/* ════════════ EFEKT İŞLEME YARDIMCI FONKSİYONLARI ════════════ */


static void Apply_RingMod(float *sample_L, float *sample_R) {
    if (!current_params.ring_mod_enable) return;

    float phase_inc = (2.0f * PI * current_params.ring_mod_freq) / SAMPLE_RATE;
    ring_mod_phase += phase_inc;
    if (ring_mod_phase > 2.0f * PI) ring_mod_phase -= 2.0f * PI;

    float carrier = sinf(ring_mod_phase);
    float wet_L = *sample_L * carrier;
    float wet_R = *sample_R * carrier;

    *sample_L = (*sample_L * (1.0f - current_params.ring_mod_intensity)) + (wet_L * current_params.ring_mod_intensity);
    *sample_R = (*sample_R * (1.0f - current_params.ring_mod_intensity)) + (wet_R * current_params.ring_mod_intensity);
}

static void Apply_LPF(float *sample_L, float *sample_R) {
    if (!current_params.lpf_enable) return;

    smooth_lpf_freq += 0.005f * (current_params.lpf_cutoff_freq - smooth_lpf_freq);
    if (smooth_lpf_freq > 12000.0f) smooth_lpf_freq = 12000.0f;

    float f = 2.0f * sinf(PI * smooth_lpf_freq / SAMPLE_RATE);
    float q = 0.5f;

    lpf_low_L += f * lpf_band_L + ANTI_DENORMAL;
    lpf_band_L += f * (*sample_L - lpf_low_L - q * lpf_band_L);
    *sample_L = lpf_low_L - ANTI_DENORMAL;

    lpf_low_R += f * lpf_band_R + ANTI_DENORMAL;
    lpf_band_R += f * (*sample_R - lpf_low_R - q * lpf_band_R);
    *sample_R = lpf_low_R - ANTI_DENORMAL;
}

static void Apply_HPF(float *sample_L, float *sample_R) {
    if (!current_params.hpf_enable) return;

    smooth_hpf_freq += 0.005f * (current_params.hpf_cutoff_freq - smooth_hpf_freq);
    float f = 2.0f * sinf(PI * smooth_hpf_freq / SAMPLE_RATE);
    float q = 0.5f;

    hpf_low_L += f * hpf_band_L + ANTI_DENORMAL;
    float high_L = *sample_L - hpf_low_L - q * hpf_band_L;
    hpf_band_L += f * high_L;
    *sample_L = high_L - ANTI_DENORMAL;

    hpf_low_R += f * hpf_band_R + ANTI_DENORMAL;
    float high_R = *sample_R - hpf_low_R - q * hpf_band_R;
    hpf_band_R += f * high_R;
    *sample_R = high_R - ANTI_DENORMAL;
}

static float APF(float input, float *state, float g) {
    float v = input - g * (*state) + ANTI_DENORMAL;
	float out = g * v + (*state) - ANTI_DENORMAL;
	*state = v;
	return out;
}

static void Apply_Phaser(float *sample_L, float *sample_R) {
    if (!current_params.wah_enable) return;

    float rate = current_params.wah_sensitivity * 3.0f;
    p_lfo += (rate * 2.0f * PI) / SAMPLE_RATE;
    if (p_lfo > 2.0f * PI) p_lfo -= 2.0f * PI;

    float lfo_val = (sinf(p_lfo) + 1.0f) * 0.5f;
    float sweep_freq = 300.0f + lfo_val * 2500.0f;
    float rc = 1.0f / (2.0f * PI * sweep_freq);
    float dt = 1.0f / SAMPLE_RATE;
    float alpha = (dt - rc) / (dt + rc);

    *sample_L = (APF(APF(APF(APF(*sample_L, &p_apf1_L, alpha), &p_apf2_L, alpha), &p_apf3_L, alpha), &p_apf4_L, alpha) * 0.5f) + (*sample_L * 0.5f);
    *sample_R = (APF(APF(APF(APF(*sample_R, &p_apf1_R, alpha), &p_apf2_R, alpha), &p_apf3_R, alpha), &p_apf4_R, alpha) * 0.5f) + (*sample_R * 0.5f);
}

static void Apply_Delay(float *sample_L, float *sample_R) {
    if (!current_params.delay_enable) return;

    float target_delay = current_params.delay_time * (DELAY_BUFFER_SIZE - 1);
    if (target_delay < 10.0f) target_delay = 10.0f;

    smooth_delay_samples += 0.001f * (target_delay - smooth_delay_samples);
    int32_t read_idx = (int32_t)delay_head - (int32_t)smooth_delay_samples;
    while (read_idx < 0) read_idx += DELAY_BUFFER_SIZE;

    float delayed_sample = (float)delay_line[read_idx % DELAY_BUFFER_SIZE];
    tape_lpf += 0.75f * (delayed_sample - tape_lpf);
    delayed_sample = tape_lpf;

    float input_mono = (*sample_L + *sample_R) * 0.5f;
    float write_val = input_mono + (delayed_sample * current_params.delay_feedback);
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

/* ════════════ ANA İŞLEME FONKSİYONU ════════════ */
/* ════════════ ANA İŞLEME FONKSİYONU ════════════ */
void Filter_Apply(int16_t *audio_in, int16_t *audio_out, uint16_t samples_per_channel) {

    // --- EKRAN GÜNCELLEME KÖPRÜSÜ (YENİ) ---
    // Her ses bloğu geldiğinde (genelde 128 veya 256 örnek)
    // bunun sol kanalını alıp ekrana gönderiyoruz.
    OLED_UpdateWaveform(audio_in, samples_per_channel);

    for (uint16_t i = 0; i < samples_per_channel; i++) {
        // Mevcut filtreleme işlemleri burada devam ediyor.

        float sample_L = (float)audio_in[i * 2];
        float sample_R = (float)audio_in[i * 2 + 1];

        Apply_RingMod(&sample_L, &sample_R);
        Apply_LPF(&sample_L, &sample_R);
        Apply_HPF(&sample_L, &sample_R);
        Apply_Phaser(&sample_L, &sample_R);
        Apply_Delay(&sample_L, &sample_R);
        Apply_MasterVolume_And_Clip(&sample_L, &sample_R);

        audio_out[i * 2]     = (int16_t)sample_L;
        audio_out[i * 2 + 1] = (int16_t)sample_R;
    }
}
