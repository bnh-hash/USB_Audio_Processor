#ifndef FILTER_H
#define FILTER_H

#include <stdint.h>

// --- Sabitler ---
#define PI                  3.14159265358979323846f
#define SAMPLE_RATE         48000.0f  // USB Audio standardı
#define DELAY_BUFFER_SIZE   4096      // Delay için tampon boyutu

// --- Parametre Yapısı ---
typedef struct {
    // Ring Modulator
    uint8_t  ring_mod_enable;
    float    ring_mod_freq;
    float    ring_mod_intensity;

    // LPF (Low Pass)
    uint8_t  lpf_enable;
    float    lpf_cutoff_freq;

    // HPF (High Pass)
    uint8_t  hpf_enable;
    float    hpf_cutoff_freq;

    // Phaser (Wah ismiyle eşleştirildi)
    uint8_t  wah_enable;
    float    wah_sensitivity;

    // Delay
    uint8_t  delay_enable;
    float    delay_time;      // 0.0 - 1.0 arası
    float    delay_feedback;  // 0.0 - 0.9 arası
    float    delay_mix;       // 0.0 - 1.0 arası

    // Master
    float    master_volume;   // 0.0 - 1.0 arası
} filter_params_t;

// --- Fonksiyon Prototipleri ---
void Filter_Init(filter_params_t *init_params);
void Filter_Set_Params(filter_params_t *new_params);
void Filter_Apply(int16_t *audio_in, int16_t *audio_out, uint16_t samples_per_channel);

#endif /* FILTER_H */
