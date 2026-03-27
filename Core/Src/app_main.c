#include "app_main.h"
#include "filter.h"
#include "audio_stream.h"
#include "main.h"
#include <math.h>
#include "button.h"
#include "ssd1306.h"
#include "OLED.h"
#include <stdlib.h>
#include "pot_management.h"
extern ADC_HandleTypeDef hadc1;



/* Pin → ADC Kanal eşlemesi
 *
 *   POT1 → PB0  → ADC1_IN8  → Phaser
 *   POT2 → PB1  → ADC1_IN9  → LPF Cutoff
 *   POT3 → PC5  → ADC1_IN15 → HPF Cutoff
 *   POT4 → PC4  → ADC1_IN14 → Delay Feedback
 *   POT5 → PA7  → ADC1_IN7  → Ring Mod Frekans
 *   POT6 → PA6  → ADC1_IN6  → Master Volume
 */
static const uint32_t POT_CHANNELS[POT_COUNT] = {
    ADC_CHANNEL_8,    // POT1 → PB0  → Phaser
    ADC_CHANNEL_9,    // POT2 → PB1  → LPF Cutoff
    ADC_CHANNEL_15,   // POT3 → PC5  → HPF Cutoff
    ADC_CHANNEL_14,   // POT4 → PC4  → Delay Feedback
    ADC_CHANNEL_7,    // POT5 → PA7  → Ring Mod Frekans
    ADC_CHANNEL_6,    // POT6 → PA6  → Master Volume
};

/* ════════════════════════ GLOBAL DEĞİŞKENLER ════════════════════════ */
filter_params_t  app_filter_params;
extern uint16_t display_audio_buffer[128];

static uint16_t  smooth_pot[POT_COUNT];
static uint8_t   first_read = 1;


/* ════════════════════════ POT OKUMA ════════════════════════ */

static uint16_t Pot_ReadChannel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel      = channel;
    sConfig.Rank         = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 1);
    uint16_t val = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return val;
}

static void Pot_ReadAll(void)
{
	uint16_t * p_values = get_pot_values();

    for (uint8_t i = 0; i < POT_COUNT; i++) {
        uint16_t raw = Pot_ReadChannel(POT_CHANNELS[i]);
        if (first_read) {
            smooth_pot[i] = 4096-raw;
        } else {
            smooth_pot[i] = (uint16_t)(((uint32_t)smooth_pot[i] * 7u + (uint32_t)(4096-raw)) / 8u);
        }
        p_values[i] = smooth_pot[i];
    }
    first_read = 0;
}

static float Pot_Map(uint8_t idx, float min_val, float max_val)
{
	uint16_t * p_values = get_pot_values();
	return min_val + ((float)p_values[idx] / 4095.0f) * (max_val - min_val);
}

/* ════════════════════════ POT → PARAMETRE ════════════════════════ */

static void Pot_ApplyToParams(void)
{
    /* ── POT1 (idx 0) → PHASER ──────────────────────────────────── */

	uint16_t * p_values = get_pot_values();
	if (p_values[POT_PHASER] < 100) {
        app_filter_params.wah_enable = 0;
    } else {
        app_filter_params.wah_enable = 1;
        app_filter_params.wah_sensitivity = (float)p_values[POT_PHASER] / 4095.0f;
    }

    /* ── POT2 (idx 1) → LPF ─────────────────────────────────────── */

    if (p_values[POT_LPF] >= 100) {
        app_filter_params.lpf_enable = 1;
    } else {
        app_filter_params.lpf_enable = 0;
    }
    if (p_values[POT_LPF] > 3850) p_values[POT_LPF] = 3850;

    app_filter_params.lpf_cutoff_freq = (1.0f - (float)p_values[POT_LPF] / 4095.0f) * 10000.0f;

    /* ── POT3 (idx 2) → HPF ─────────────────────────────────────── */

    if (p_values[POT_HPF] < 100) {
        app_filter_params.hpf_enable = 0;
    } else {
        app_filter_params.hpf_enable = 1;
        float norm = (float)p_values[POT_HPF] / 4095.0f;
        app_filter_params.hpf_cutoff_freq = 20.0f * powf(400.0f, norm);
    }

    /* ── POT4 (idx 3) → TEK-POT DELAY ───────────────────────── */

    if (p_values[POT_DELAY] < 100) {
            // Pot kapalıyken delay tamamen devre dışı
            app_filter_params.delay_enable   = 0;
            app_filter_params.delay_feedback = 0.0f;
            app_filter_params.delay_mix      = 0.0f;
        } else {
            app_filter_params.delay_enable   = 1;

            // 0.0 ile 1.0 arası normalize pot değeri
            float da = (float)p_values[POT_DELAY] / 4095.0f;

            // Potu açtıkça yankı hızı değişir (0.1 çok hızlı slapback, 1.0 uzun yankı)
            // 0.1 altına düşmüyoruz ki çok dipte cızırtı yapmasın
            app_filter_params.delay_time = 0.1f + (da * 0.9f);

            // 2. Yankı Tekrar Sayısı (Feedback)
            // Çok açarsak sonsuz döngüye girip patlamaması için %85 ile sınırlandırdık
            app_filter_params.delay_feedback = da * 0.85f;

            // 3. Orijinal ses ile yankılı sesin karışımı (Delay Level)
            app_filter_params.delay_mix = da * 0.7f;
        }
    /* ── POT5 (idx 4) → ring mod ────────────────────────── */

    if (p_values[POT_RING] < 100) {
        app_filter_params.ring_mod_enable = 0;
    } else {
        app_filter_params.ring_mod_enable = 1;
        float norm = (float)p_values[POT_RING]/ 4095.0f;
        app_filter_params.ring_mod_freq   = 100.0f * powf(8.0f, norm); // 1–100 Hz
    }

    /* ── POT6 (idx 5) → MASTER VOLUME ──────────────────────────── */

    app_filter_params.master_volume = Pot_Map(5, 0.0f, 1.5f);

    /* ──────── MUTE & BYPASS ZORLAMASI ─────────────────────────── */
    if (Get_Bypass_State()) {
        app_filter_params.lpf_enable      = 0;
        app_filter_params.hpf_enable      = 0;
        app_filter_params.ring_mod_enable = 0;
        app_filter_params.delay_enable    = 0;
        app_filter_params.wah_enable      = 0;
    }
    if (Get_Mute_State()) {
        app_filter_params.master_volume   = 0.0f;
    }

    /* Parametreleri DSP modülüne interrupt-safe olarak gönder */
    HAL_NVIC_DisableIRQ(DMA1_Stream4_IRQn);
    Filter_Set_Params(&app_filter_params);
    HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
}

/* ════════════════════════ APP_INIT ════════════════════════ */
void App_Init(void)
{

    app_filter_params.ring_mod_enable    = 1;
    app_filter_params.hpf_enable         = 1;
    app_filter_params.delay_enable       = 1;
    app_filter_params.lpf_enable         = 1;
    app_filter_params.wah_enable         = 1;

    /* Güvenli başlangıç değerleri */
    app_filter_params.master_volume      = 1.0f;   // Başlangıçta sabit 1.0
    app_filter_params.lpf_cutoff_freq    = 8000.0f;
    app_filter_params.hpf_cutoff_freq    = 10.0f;
    app_filter_params.ring_mod_freq      = 100.0f;
    app_filter_params.ring_mod_intensity = 0.8f;
    app_filter_params.delay_feedback     = 0.0f;
    app_filter_params.delay_mix          = 0.0f;
    app_filter_params.delay_time         = 0.4f;

    /* Filtre motoru ve ses akışını başlat */
    Filter_Init(&app_filter_params);
    AudioStream_Init();
    ssd1306_Init();
    AudioStream_Start();
    Buton_Init();

    /* İlk pot okuma (smoothing tamponunu doldur) */
    Pot_ReadAll();
    /* NOT: Pot_ApplyToParams() burada çağrılmıyor.
       App_Loop ilk 100ms içinde otomatik çağıracak. */
    // Ekran ilk açıldığında dalga düz bir çizgi olsun diye
        // buffer'ı sessizlik seviyesi (2048) ile dolduruyoruz:
        for(int i = 0; i < 128; i++) {
            display_audio_buffer[i] = 2048;
        }
}

/* ════════════════════════ APP_LOOP ════════════════════════ */
void App_Loop(void)
{
	uint16_t * p_values = get_pot_values();

    Button_Process();

    static uint32_t last_pot_tick = 0;
    static uint32_t last_ui_tick = 0;

    // --- 1. SİSTEM VE POT OKUMA (20ms) ---
    if (HAL_GetTick() - last_pot_tick >= 20) {
        last_pot_tick = HAL_GetTick();
        Pot_ReadAll();
        Pot_ApplyToParams();
    }

    // --- 2. EKRAN GÜNCELLEME (50ms) ---
    if (HAL_GetTick() - last_ui_tick >= 50) {
        last_ui_tick = HAL_GetTick();

        ssd1306_Fill(Black);

        UI_DrawHeader("funkyfairy");

        // --- GERÇEK OSİLOSKOP ---
        // Display_audio_buffer içindeki veriyi çizer.
        // İçine ses verisi gelmediği sürece ip gibi düz bir çizgi çizecek.
        UI_DrawOscilloscope();

        // Kutucuk hesaplamaları

        uint8_t p1 = (p_values[POT_PHASER] * 100) / 4000; if(p1>100) p1=100;
        uint8_t p2 = (p_values[POT_LPF] * 100) / 4000; if(p2>100) p2=100;
        uint8_t p3 = (p_values[POT_HPF] * 100) / 4000; if(p3>100) p3=100;
        uint8_t p4 = (p_values[POT_DELAY] * 100) / 4000; if(p4>100) p4=100;
        uint8_t p5 = (p_values[POT_RING] * 100) / 4000; if(p5>100) p5=100;
        uint8_t p6 = (p_values[POT_VOLUME]* 100) / 4000; if(p6>100) p6=100;

        UI_DrawPotentiometers(p1, p2, p3, p4, p5, p6);

        ssd1306_UpdateScreen();
    }
}
