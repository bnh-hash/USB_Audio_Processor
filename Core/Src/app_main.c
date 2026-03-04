#include "app_main.h"
#include "filter.h"
#include "audio_stream.h"
#include "main.h"
#include <math.h>

/* ════════════════════════════════════════════
   HAL HANDLE
   ════════════════════════════════════════════ */
extern ADC_HandleTypeDef hadc1;
// CubeMX tarafından main.c'de tanımlanıyor, burada sadece kullanıyoruz.

/* ════════════════════════════════════════════
   GLOBAL DEĞİŞKENLER
   ════════════════════════════════════════════ */
volatile float      carrier_phase   = 0.0f;
filter_params_t     app_filter_params;
uint16_t            pot_values[POT_COUNT];
// pot_values: 5 elemanlı dizi, her biri 0–4095 arası ham ADC değeri.
// DMA yerine polling kullanıyoruz → hadc1 ile sırayla her kanalı okuyoruz.

/* ════════════════════════════════════════════
   POT OKUMA FONKSİYONLARI
   ════════════════════════════════════════════ */

static uint16_t Pot_ReadChannel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    sConfig.Channel     = channel;
    sConfig.Rank        = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    // 84 cycle örnekleme: pot'un çıkış empedansı yüksek olabilir,
    // uzun örnekleme süresi daha kararlı okuma sağlar.

    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    // Kanalı geçici olarak rank 1'e al ve dönüştür.
    // Her pot için aynı ADC1'i sırayla farklı kanalla kullanıyoruz.

    HAL_ADC_Start(&hadc1);
    // Tek dönüşüm başlat (ContinuousConvMode=DISABLE olduğu için bir kere çevirir).

    HAL_ADC_PollForConversion(&hadc1, 10);
    // En fazla 10ms bekle. Normal şartlarda mikrosaniyeler içinde biter.

    uint16_t val = (uint16_t)HAL_ADC_GetValue(&hadc1);
    // 12-bit sonucu oku: 0–4095 arası.

    HAL_ADC_Stop(&hadc1);
    // Bir sonraki kanal konfigürasyonuna geçmeden önce ADC'yi durdur.

    return val;
}

static void Pot_ReadAll(void)
{
    for (uint8_t i = 0; i < POT_COUNT; i++) {
        pot_values[i] = Pot_ReadChannel(POT_CHANNELS[i]);
    }
    // 5 kanalı sırayla oku ve pot_values dizisine yaz.
    // Toplam süre ~5 × (örnekleme + dönüşüm) ≈ birkaç mikrosaniye.
    // App_Loop'ta 20ms'de bir çağrılıyor, yükü ihmal edilebilir.
}

static float Pot_Map(uint8_t idx, float min_val, float max_val)
{
    return min_val + ((float)pot_values[idx] / 4095.0f) * (max_val - min_val);
    // Ham 0–4095 değerini istenen aralığa doğrusal olarak taşır.
    // idx: pot_values dizisindeki indeks.
}

static void Pot_ApplyToParams(void)
{
    /* POT0 → Ring Mod Carrier Frekansı (20 – 1000 Hz, logaritmik) */
    float log_freq = Pot_Map(0, 0.0f, 2.699f);
    // log ölçek: 10^0=1 … 10^2.699≈500 → *2 ile 2–1000 Hz arası.
    // Logaritmik tercih: kulağın frekansı logaritmik algılaması ile örtüşür,
    // potun tüm dönüş aralığı boyunca eşit "hassasiyet hissi" verir.
    app_filter_params.ring_mod_freq = powf(10.0f, log_freq) * 2.0f;

    /* POT1 → Ring Mod Yoğunluğu (0.0 – 1.0) */
    app_filter_params.ring_mod_intensity = Pot_Map(1, 0.0f, 1.0f);
    // 0.0: ring mod etkisiz, 1.0: tam ring modulation.

    /* POT2 → Wah Hassasiyeti (0.1 – 1.0) */
    app_filter_params.wah_sensitivity = Pot_Map(2, 0.1f, 1.0f);
    // 0.1 alt sınır: sıfırda wah tamamen kapanır, bu yüzden küçük bir offset bıraktık.

    /* POT3 → Delay Feedback (0.0 – 0.9) */
    app_filter_params.delay_feedback = Pot_Map(3, 0.0f, 0.9f);
    // 1.0'a izin verme: feedback=1.0 sonsuz döngüye ve klipse neden olur.
    // 0.9 pratik maksimum, yeterince uzun echo sağlar.

    /* POT4 → Master Volume (0.0 – 1.5) */
    app_filter_params.master_volume = Pot_Map(4, 0.0f, 1.5f);
    // 1.0 üstü hafif amplifikasyon imkanı tanır.
    // Çok yüksek yapma: DAC/amplifikatör klipse girer.

    Filter_Set_Params(&app_filter_params);
    // Güncellenmiş parametreleri filtre modülüne ilet.
    // Bu çağrı App_Loop'tan geliyor, DMA kesmesi sırasında değil.
}

/* ════════════════════════════════════════════
   APP_INIT
   ════════════════════════════════════════════ */
void App_Init(void)
{
    Filter_Init(&app_filter_params);

    AudioStream_Init();
    AudioStream_Start();

    /* Başlangıç parametre değerleri */
    app_filter_params.ring_mod_enable    = 1;
    app_filter_params.wah_enable         = 0;
    app_filter_params.lpf_enable         = 0;
    app_filter_params.hpf_enable         = 0;
    app_filter_params.bitcrush_enable    = 0;
    app_filter_params.delay_enable       = 0;

    app_filter_params.delay_feedback     = 0.6f;
    app_filter_params.delay_mix          = 0.4f;
    app_filter_params.ring_mod_freq      = 300.0f;
    app_filter_params.ring_mod_intensity = 0.4f;
    app_filter_params.wah_sensitivity    = 0.9f;
    app_filter_params.wah_mix            = 0.8f;
    app_filter_params.bitcrush_amount    = 0.3f;
    app_filter_params.master_volume      = 1.1f;

    Filter_Set_Params(&app_filter_params);

    /* İlk pot okuma — başlangıçta pot değerleri geçerli olsun */
    Pot_ReadAll();
    Pot_ApplyToParams();
    // Başlangıçta bir kere oku ki pot_values[i] = 0 ile başlamasın,
    // gerçek pot pozisyonları hemen yansısın.
}

/* ════════════════════════════════════════════
   APP_LOOP  (main.c while(1) içinden çağrılıyor)
   ════════════════════════════════════════════ */
void App_Loop(void)
{
    static uint32_t last_pot_tick = 0;

    if ((HAL_GetTick() - last_pot_tick) >= POT_UPDATE_MS) {
        Pot_ReadAll();
        // 5 pot kanalını sırayla ADC1 ile polling yöntemiyle oku.

        Pot_ApplyToParams();
        // Okunan değerleri filtre parametrelerine map'le ve uygula.

        last_pot_tick = HAL_GetTick();
        // Bir sonraki güncelleme için zaman damgasını güncelle.
    }
    // HAL_Delay(10) kaldırıldı: non-blocking yapı daha sağlıklı.
    // Gecikme ihtiyacı varsa DMA kesmesi zaten zamanlamayı yönetiyor.
}
