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

/**
 * @brief  Tek bir ADC kanalını polling ile okur.
 *         Her çağrıda kanal yeniden konfigüre edilir,
 *         tek dönüşüm yapılır ve sonuç döndürülür.
 *
 *         ÖNEMLİ: MX_ADC1_Init'te ScanConvMode ve
 *         ContinuousConvMode DISABLE olmalıdır.
 */
static uint16_t Pot_ReadChannel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    sConfig.Channel      = channel;
    sConfig.Rank         = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    // 84 cycle: pot çıkış empedansı yüksek olabilir,
    // uzun örnekleme süresi daha kararlı okuma sağlar.

    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);

    uint16_t val = (uint16_t)HAL_ADC_GetValue(&hadc1);

    HAL_ADC_Stop(&hadc1);

    return val;
}

/**
 * @brief  6 pot kanalını sırayla okur ve jitter filtresi uygular.
 *         smooth_pot[] dizisi üzerinden 8-sample EMA ile yumuşatma.
 */
static void Pot_ReadAll(void)
{
    for (uint8_t i = 0; i < POT_COUNT; i++) {
        uint16_t raw = Pot_ReadChannel(POT_CHANNELS[i]);

        if (first_read) {
            smooth_pot[i] = raw;  // İlk okumada hemen kabul et
        } else {
            // 8-sample exponential moving average
            // (7/8 eski değer + 1/8 yeni değer)
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

/**
 * @brief  6 pot değerini filtre parametrelerine dönüştürür.
 *
 *   POT1 (idx 0) → Master Volume     0.0 – 1.5
 *   POT2 (idx 1) → LPF Cutoff        200 Hz – 20 kHz  (log)
 *   POT3 (idx 2) → HPF Cutoff        20 Hz – 5 kHz    (log)
 *   POT4 (idx 3) → Delay Feedback    0.0 – 0.85
 *   POT5 (idx 4) → Ring Mod Frekans  20 Hz – 1 kHz    (log)
 *   POT6 (idx 5) → Wah Hassasiyet    0.1 – 1.0
 */
static void Pot_ApplyToParams(void)
{
    /* ── POT1 → Master Volume ─────────────────────────── */
    app_filter_params.master_volume = Pot_Map(0, 0.0f, 1.5f);

    /* ── POT2 → LPF Cutoff ───────────────────────────── */
    // Pot tam saat yönünde (max) → cutoff çok yüksek →
    //   filtre şeffaf → LPF kapalı
    // Pot saat yönü tersine çevrildikçe cutoff düşer →
    //   tiz sesler kesilir
    if (pot_values[1] > 3950) {
        /* Pot neredeyse sonuna gelmiş → LPF devre dışı */
        app_filter_params.lpf_enable = 0;
    } else {
        app_filter_params.lpf_enable = 1;
        /* 0 – 3950 aralığını 0.0 – 1.0 normalize et */
        float norm = (float)pot_values[1] / 3950.0f;
        /* Logaritmik ölçek: 200 Hz – 20 000 Hz
         * 200 * 100^0 = 200 Hz  (pot min)
         * 200 * 100^1 = 20 000 Hz (pot ~max)            */
        app_filter_params.lpf_cutoff_freq = 200.0f * powf(100.0f, norm);
    }

    /* ── POT3 → HPF Cutoff ───────────────────────────── */
    // Pot minimum → cutoff çok düşük → filtre şeffaf → HPF kapalı
    // Pot yukarı çevrildikçe cutoff yükselir → bas kesilir
    if (pot_values[2] < 100) {
        /* Pot sıfıra yakın → HPF devre dışı */
        app_filter_params.hpf_enable = 0;
    } else {
        app_filter_params.hpf_enable = 1;
        float norm = (float)(pot_values[2] - 100) / 3995.0f;
        /* Logaritmik ölçek: 20 Hz – 5 000 Hz
         * 20 * 250^0 = 20 Hz   (pot min bölgesi)
         * 20 * 250^1 = 5000 Hz (pot max)                */
        app_filter_params.hpf_cutoff_freq = 20.0f * powf(250.0f, norm);
    }

    /* ── POT4 → Delay Feedback ────────────────────────── */
    {
        float fb = Pot_Map(3, 0.0f, 0.85f);
        if (fb < 0.02f) {
            /* Pot sıfırda → delay kapalı */
            app_filter_params.delay_enable   = 0;
            app_filter_params.delay_feedback = 0.0f;
        } else {
            app_filter_params.delay_enable   = 1;
            app_filter_params.delay_feedback = fb;
        }
    }

    /* ── POT5 → Ring Mod Frekans ──────────────────────── */
    if (pot_values[4] < 100) {
        /* Pot sıfırda → ring mod kapalı */
        app_filter_params.ring_mod_enable = 0;
    } else {
        app_filter_params.ring_mod_enable = 1;
        /* Logaritmik: 20 Hz – 1000 Hz
         * 10^(0) * 2 = 2 Hz  min  →  10^(2.699)*2 ≈ 1000 Hz max */
        float log_freq = Pot_Map(4, 0.0f, 2.699f);
        app_filter_params.ring_mod_freq = powf(10.0f, log_freq) * 2.0f;
    }

    /* ── POT6 → Wah Hassasiyet ────────────────────────── */
    if (pot_values[5] < 100) {
        /* Pot sıfırda → wah kapalı */
        app_filter_params.wah_enable = 0;
    } else {
        app_filter_params.wah_enable = 1;
        app_filter_params.wah_sensitivity = Pot_Map(5, 0.1f, 1.0f);
    }

    /* Güncellenmiş parametreleri DSP modülüne ilet */
    Filter_Set_Params(&app_filter_params);
}

/* ════════════════════════════════════════════
   APP_INIT
   ════════════════════════════════════════════ */
void App_Init(void)
{
    /* ─── Başlangıç Parametre Değerleri ─── */
    /* Pot ile kontrol EDİLMEYEN parametrelerin sabit değerleri: */
    app_filter_params.ring_mod_intensity = 0.7f;   // Ring Mod yoğunluğu (sabit)
    app_filter_params.wah_mix            = 0.8f;   // Wah kuru/ıslak oranı (sabit)
    app_filter_params.delay_mix          = 0.4f;   // Delay karışım oranı (sabit)
    app_filter_params.bitcrush_amount    = 0.3f;   // Bitcrusher miktarı (sabit)

    /* Tüm efektler başlangıçta kapalı – potlar açacak */
    app_filter_params.ring_mod_enable    = 0;
    app_filter_params.wah_enable         = 0;
    app_filter_params.lpf_enable         = 0;
    app_filter_params.hpf_enable         = 0;
    app_filter_params.bitcrush_enable    = 0;
    app_filter_params.delay_enable       = 0;

    /* Güvenli başlangıç değerleri */
    app_filter_params.master_volume      = 1.0f;
    app_filter_params.lpf_cutoff_freq    = 20000.0f;
    app_filter_params.hpf_cutoff_freq    = 20.0f;
    app_filter_params.ring_mod_freq      = 300.0f;
    app_filter_params.wah_sensitivity    = 0.5f;
    app_filter_params.delay_feedback     = 0.0f;

    /* Filtre motoru ve audio stream başlat */
    Filter_Init(&app_filter_params);
    AudioStream_Init();
    AudioStream_Start();

    /* İlk pot okuma — gerçek pot pozisyonları hemen yansısın */
    Pot_ReadAll();
    Pot_ApplyToParams();
}

/* ════════════════════════════════════════════
   APP_LOOP  (main.c while(1) içinden çağrılıyor)
   ════════════════════════════════════════════ */
void App_Loop(void)
{
    static uint32_t last_pot_tick = 0;

    if ((HAL_GetTick() - last_pot_tick) >= POT_UPDATE_MS) {
        last_pot_tick = HAL_GetTick();

        Pot_ReadAll();
        // 6 pot kanalını sırayla ADC1 ile polling yöntemiyle oku
        // + jitter filtresi uygula.

        Pot_ApplyToParams();
        // Okunan değerleri filtre parametrelerine map'le
        // ve DSP modülüne ilet.
    }
}
