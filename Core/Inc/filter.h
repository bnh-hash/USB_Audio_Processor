/*
 * filter.h
 *
 *  Created on: Mar 2, 2026
 *      Author: Gökçe
 */

#ifndef FILTER_H
#define FILTER_H

#include <stdint.h>

/* --- SES KONFIGURASYONU --- */
#define SAMPLE_RATE 48000.0f
#define PI          3.14159265f

/* --- FILTRE PARAMETRE YAPISI --- */
/* Bu struct, tum efektlerin kontrol dugmelerini barindirir.
 * Filter_Init veya Filter_Set_Params ile motora gonderilir.
 */
typedef struct {
    // 0. Master Volume (Ana Ses Seviyesi)
    float master_volume;        // 0.0 - 1.0 (veya daha yuksek gain icin >1.0)

    // 1. Ring Modulator (Robotik/Metalik Ses)
    uint8_t ring_mod_enable;    // 1: Aktif, 0: Pasif
    float ring_mod_freq;        // Tasiyici frekans (Hz)
    float ring_mod_intensity;   // Karisim orani (0.0 Dry - 1.0 Wet)

    // 2. Low Pass Filter (LPF - Boguklastirma)
    uint8_t lpf_enable;         // 1: Aktif, 0: Pasif
    float lpf_cutoff_freq;      // Kesim frekansi (Hz)

    // 3. High Pass Filter (HPF - Sadece Tizler)
    uint8_t hpf_enable;         // 1: Aktif, 0: Pasif
    float hpf_cutoff_freq;      // Kesim frekansi (Hz)

    // 4. Auto-Wah (Funky Envelope Filter - Vak Vak)
    uint8_t wah_enable;         // 1: Aktif, 0: Pasif
    float wah_sensitivity;      // Giris sesine tepki hassasiyeti (0.0 - 1.0)
    float wah_mix;              // Karisim orani (0.0 Dry - 1.0 Wet)

    // 5. Bitcrusher
	uint8_t bitcrush_enable;    // 1: Aktif
	float bitcrush_amount;      // 0.0 (Temiz) - 1.0 (Tamamen Yikim)

	// 6. Delay (YENI - Yanki)
	uint8_t delay_enable;
	float delay_feedback; // 0.0 - 0.9 (Yankinin ne kadar surecegi)
	float delay_mix;      // 0.0 - 0.5 (Yanki sesi ne kadar gurultulu)

} filter_params_t;

/* --- FONKSIYON PROTOTIPLERI --- */

/**
 * @brief Filtre motorunu baslatir ve varsayilan degerleri yukler.
 * @param initial_params: Baslangic ayarlari (NULL verilirse default ayarlar yuklenir)
 */
void Filter_Init(filter_params_t *initial_params);

/**
 * @brief Calisma zamaninda (Runtime) filtre ayarlarini gunceller.
 * Potansiyometre veya buton degisimlerinde bu cagrilmalidir.
 * @param new_params: Yeni ayar yapisi
 */
void Filter_Set_Params(filter_params_t *new_params);

/**
 * @brief Ses tamponunu isler (Efekt Zincirini Uygular).
 * Islem sirasi: RingMod -> LPF -> HPF -> AutoWah -> Volume -> Clip
 * @param buffer: Islenecek ses verisi (In-Place degistirilir)
 * @param num_samples: Toplam ornek sayisi (Stereo oldugu icin L+R toplami)
 */
void Filter_Apply(int16_t *buffer, uint32_t num_samples);

#endif // FILTER_H
