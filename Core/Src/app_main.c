/*
 * app_main.c
 *
 *  Created on: Mar 2, 2026
 *      Author: Gökçe
 */
/*
 * app_main.c
 */

#include "filter.h"
#include "audio_stream.h"
#include "app_main.h"
// #include "stm32f4_discovery_audio.h" // SİLİNDİ: Harici DAC için gerek yok
#include <math.h>
#include "main.h"

/* --- Global Değişkenler --- */
volatile float carrier_phase = 0.0f;
filter_params_t app_filter_params;

void App_Init(void) {
    // 1. BSP Audio Init SİLİNDİ.
    // MAX98357'nin herhangi bir I2C init komutuna ihtiyacı yoktur.

    // 2. Filtre API'sini Baslat
    Filter_Init(&app_filter_params);

    // 3. Audio Stream Hazirla ve DMA'yı başlat
    AudioStream_Init();
    AudioStream_Start(); // <--- YENİ: DMA transferini doğrudan başlatıyoruz

    // 4. Baslangic Modu
    app_filter_params.ring_mod_enable = 0;
	app_filter_params.wah_enable      = 0;
	app_filter_params.lpf_enable      = 0;
	app_filter_params.hpf_enable      = 0;
	app_filter_params.bitcrush_enable = 0;
	app_filter_params.delay_enable    = 0;
	app_filter_params.master_volume   = 1.0f;

	app_filter_params.delay_enable = 0;
	app_filter_params.delay_feedback = 0.6f;
	app_filter_params.delay_mix = 0.4f;

	app_filter_params.ring_mod_enable = 0;
	app_filter_params.ring_mod_freq = 300.0f;
	app_filter_params.ring_mod_intensity = 0.4f;

	app_filter_params.wah_enable = 0;
	app_filter_params.wah_sensitivity = 0.9f;
	app_filter_params.wah_mix = 0.8f;

	app_filter_params.bitcrush_enable = 0;
	app_filter_params.bitcrush_amount = 0.3f;
	app_filter_params.master_volume = 1.1f;

	Filter_Set_Params(&app_filter_params);

}


void App_Loop(void) {
    HAL_Delay(10);
}
