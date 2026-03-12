#include "app_main.h"
#include "filter.h"
#include "audio_stream.h"
#include "main.h"
#include <math.h>

/* ════════════════════════════════════════════
   HAL HANDLE
   ════════════════════════════════════════════ */
extern ADC_HandleTypeDef hadc1;

/* ════════════════════════════════════════════
   GLOBAL DEĞİŞKENLER
   ════════════════════════════════════════════ */
volatile float      carrier_phase   = 0.0f;
filter_params_t     app_filter_params;
uint16_t            pot_values[POT_COUNT];

/* Pot smoothing (8-sample exponential moving average, jitter önleme) */
static uint16_t smooth_pot[POT_COUNT];
static uint8_t  first_read = 1;

/* ════════════════════════════════════════════
   POT OKUMA FONKSİYONLARI
   ════════════════════════════════════════════ */

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
    for (uint8_t i = 0; i < POT_COUNT; i++) {
        uint16_t raw = Pot_ReadChannel(POT_CHANNELS[i]);

        if (first_read) {
            smooth_pot[i] = raw;
        } else {
            smooth_pot[i] = (uint16_t)(((uint32_t)smooth_pot[i] * 7u + (uint32_t)raw) / 8u);
        }
        pot_values[i] = smooth_pot[i];
    }
    first_read = 0;
}

/**
 * @brief  Ham 0–4095 değerini istenen [min, max] aralığına doğrusal taşır.
 */
static float Pot_Map(uint8_t idx, float min_val, float max_val)
{
    return min_val + ((float)pot_values[idx] / 4095.0f) * (max_val - min_val);
}

/* ════════════════════════════════════════════
   POT → PARAMETRE HARİTALAMA
   ════════════════════════════════════════════ */

static void Pot_ApplyToParams(void)
{
    /* ── POT6 (idx 5) → Master Volume ─────────────────────────── */
    app_filter_params.master_volume = Pot_Map(5, 0.0f, 1.5f);

    /* ── POT2 (idx 1) → LPF Cutoff ───────────────────────────── */
    if (pot_values[1] >= 100) {
        app_filter_params.lpf_enable = 1;
    } else {
        app_filter_params.lpf_enable = 0;
    }

    app_filter_params.lpf_cutoff_freq = ((float)pot_values[1] / 4095.0f) * 10000.0f;

    /* ── POT2 (idx 1) → LPF Cutoff (LOGARİTMİK YAPI ) ── */
      /*  if (pot_values[1] >= 100) {
            app_filter_params.lpf_enable = 1;

            // 1. Pot değerini 0.0 ile 1.0 arasına sıkıştır (normalize et)
            float norm = (float)pot_values[1] / 4095.0f;

            // 2. Logaritmik Formül: MIN_FREQ * (MAX_FREQ / MIN_FREQ) ^ norm
            // 100 Hz'den 8000 Hz'e yavaş ve dengeli bir geçiş yapar (8000/100 = 80).
            app_filter_params.lpf_cutoff_freq = 100.0f * powf(80.0f, norm);

        } else {
            // Pot en sola dayandığında filtre devreden çıkar
            app_filter_params.lpf_enable = 0;
        }*/
    /* ── POT3 (idx 2) → HPF Cutoff (LOGARİTMİK YAPI) ───────────── */
        if (pot_values[2] < 100) {
            // Pot sıfıra yakınken HPF tamamen devreden çıkar (Sesi hiç kesmez)
            app_filter_params.hpf_enable = 0;
        } else {
            app_filter_params.hpf_enable = 1;

            // Değeri 0.0 ile 1.0 arasına normalize et
            float norm = (float)pot_values[2] / 4095.0f;

            // Logaritmik geçiş: 20 Hz ile 1500 Hz arası (1500 / 20 = 75 çarpanı)
            // Böylece bas frekanslarda  hassas bir kontrol olur.
            app_filter_params.hpf_cutoff_freq = 20.0f * powf(100.0f, norm);
        }
        /* POT4 → Delay Feedback */
            float fb = ((float)pot_values[3] / 4095.0f) * 0.45f;
            if (fb < 0.02f) {
                app_filter_params.delay_enable   = 0;
                app_filter_params.delay_feedback = 0.0f;
            } else {
                app_filter_params.delay_enable   = 1;
                app_filter_params.delay_feedback = fb;
            }

            /* ── POT5 (idx 4) → Ring Mod Frekans  ──────── */
                if (pot_values[4] < 100) {
                    // En soldayken  efekt tamamen kapanır
                    app_filter_params.ring_mod_enable = 0;
                } else {
                    app_filter_params.ring_mod_enable = 1;

                    float norm = (float)pot_values[4] / 4095.0f;

                    // BAŞLANGIÇ FREKANSI: 150 Hz (Pes, hırıltılı bir robot sesi, kesilme değil)
                    // BİTİŞ FREKANSI: 3000 Hz (Tiz, çan/oyuncak uzaylı sesi)
                    // Oran: 3000 / 150 = 20
                    app_filter_params.ring_mod_freq = 150.0f * powf(20.0f, norm);
                }

    /* Güncellenmiş parametreleri DSP modülüne ilet */
    HAL_NVIC_DisableIRQ(DMA1_Stream4_IRQn);
    Filter_Set_Params(&app_filter_params);
    HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
}

/* ════════════════════════════════════════════
   APP_INIT
   ════════════════════════════════════════════ */
void App_Init(void)
{
    /* Tüm efektler kapalı, sadece LPF aktifleşebilecek */
    app_filter_params.ring_mod_enable    = 1;
    app_filter_params.wah_enable         = 0;
    app_filter_params.hpf_enable         = 1;
    app_filter_params.delay_enable       = 1;
    app_filter_params.lpf_enable         = 1;

    /* Güvenli başlangıç değerleri */
    app_filter_params.master_volume      = 1.0f;
    app_filter_params.lpf_cutoff_freq    =1000.0f;
    app_filter_params.hpf_cutoff_freq    = 20.0f;
    app_filter_params.ring_mod_freq      = 300.0f;
    app_filter_params.wah_sensitivity    = 0.5f;
    app_filter_params.delay_feedback     = 0.0f;

    app_filter_params.ring_mod_intensity = 0.8f;
    app_filter_params.wah_mix            = 0.8f;
    app_filter_params.delay_mix          = 0.8f;

    /* Filtre motoru ve audio stream başlat */
    Filter_Init(&app_filter_params);
    AudioStream_Init();
    AudioStream_Start();

    /* İlk pot okuma */
    Pot_ReadAll();
    Pot_ApplyToParams();
}

/* ════════════════════════════════════════════
   APP_LOOP
   ════════════════════════════════════════════ */
void App_Loop(void)
{
    static uint32_t last_pot_tick = 0;

    if ((HAL_GetTick() - last_pot_tick) >= POT_UPDATE_MS) {
        last_pot_tick = HAL_GetTick();

        Pot_ReadAll();
        Pot_ApplyToParams();
    }
}
