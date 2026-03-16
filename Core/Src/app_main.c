#include "app_main.h"
#include "filter.h"
#include "audio_stream.h"
#include "main.h"
#include <math.h>
#include "button.h"

extern ADC_HandleTypeDef hadc1;

/* ════════════════════════ GLOBAL DEĞİŞKENLER ════════════════════════ */
filter_params_t  app_filter_params;
uint16_t         pot_values[POT_COUNT];

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
    for (uint8_t i = 0; i < POT_COUNT; i++) {
        uint16_t raw = Pot_ReadChannel(POT_CHANNELS[i]);
        if (first_read) {
            smooth_pot[i] = 4096-raw;
        } else {
            smooth_pot[i] = (uint16_t)(((uint32_t)smooth_pot[i] * 7u + (uint32_t)(4096-raw)) / 8u);
        }
        pot_values[i] = smooth_pot[i];
    }
    first_read = 0;
}

static float Pot_Map(uint8_t idx, float min_val, float max_val)
{
    return min_val + ((float)pot_values[idx] / 4095.0f) * (max_val - min_val);
}

/* ════════════════════════ POT → PARAMETRE ════════════════════════ */
static void Pot_ApplyToParams(void)
{
    /* ── POT1 (idx 0) → PHASER ──────────────────────────────────── */
    if (pot_values[0] < 100) {
        app_filter_params.wah_enable = 0;
    } else {
        app_filter_params.wah_enable = 1;
        app_filter_params.wah_sensitivity = (float)pot_values[0] / 4095.0f;
    }

    /* ── POT2 (idx 1) → LPF ─────────────────────────────────────── */
    if (pot_values[1] >= 100) {
        app_filter_params.lpf_enable = 1;
    } else {
        app_filter_params.lpf_enable = 0;
    }
    app_filter_params.lpf_cutoff_freq = ((float)pot_values[1] / 4095.0f) * 10000.0f;

    /* ── POT3 (idx 2) → HPF ─────────────────────────────────────── */
    if (pot_values[2] < 100) {
        app_filter_params.hpf_enable = 0;
    } else {
        app_filter_params.hpf_enable = 1;
        float norm = (float)pot_values[2] / 4095.0f;
        app_filter_params.hpf_cutoff_freq = 20.0f * powf(100.0f, norm);
    }

    /* ── POT4 (idx 3) → TEK-POT DJ DELAY ───────────────────────── */
    if (pot_values[3] < 100) {
        app_filter_params.delay_enable   = 0;
        app_filter_params.delay_feedback = 0.0f;
        app_filter_params.delay_mix      = 0.0f;
    } else {
        app_filter_params.delay_enable   = 1;
        float da = (float)pot_values[3] / 4095.0f;
        app_filter_params.delay_time     = 0.4f;   // Sabit yankı hızı
        app_filter_params.delay_feedback = da * 0.85f;
        app_filter_params.delay_mix      = da;
    }

    /* ── POT5 (idx 4) → ring mod ────────────────────────── */
    if (pot_values[4] < 100) {
        app_filter_params.ring_mod_enable = 0;
    } else {
        app_filter_params.ring_mod_enable = 1;
        float norm = (float)pot_values[4] / 4095.0f;
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
    /* Güvenli başlangıç: Tüm efektler KAPALI */
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
    AudioStream_Start();
    Buton_Init();

    /* İlk pot okuma (smoothing tamponunu doldur) */
    Pot_ReadAll();
    /* NOT: Pot_ApplyToParams() burada çağrılmıyor.
       App_Loop ilk 100ms içinde otomatik çağıracak. */
}

/* ════════════════════════ APP_LOOP ════════════════════════ */
void App_Loop(void)
{
    /* 1. Buton kontrolü (her döngüde) */
    Button_Process();

    /* 2. Potansiyometre okuma + parametre güncelleme (100ms'de bir) */
    static uint32_t last_pot_tick = 0;
    if (HAL_GetTick() - last_pot_tick >= 20) {
        last_pot_tick = HAL_GetTick();
        Pot_ReadAll();
        Pot_ApplyToParams();
    }
}
