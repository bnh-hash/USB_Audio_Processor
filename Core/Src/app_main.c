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

/* --- Donanim Tanimlamalari --- */
#define USER_BUTTON_PIN  GPIO_PIN_0
#define USER_BUTTON_PORT GPIOA

#define LED_GREEN_PIN    GPIO_PIN_12
#define LED_ORANGE_PIN   GPIO_PIN_13
#define LED_RED_PIN      GPIO_PIN_14
#define LED_BLUE_PIN     GPIO_PIN_15
#define LED_PORT         GPIOD

/* --- Global Değişkenler --- */
volatile float carrier_phase = 0.0f;
filter_params_t app_filter_params;
static uint8_t current_audio_mode = 0;

/* --- LED Kontrol Yardimcisi --- */
void Update_LEDs(uint8_t mode) {
    HAL_GPIO_WritePin(LED_PORT, LED_GREEN_PIN | LED_ORANGE_PIN | LED_RED_PIN | LED_BLUE_PIN, GPIO_PIN_RESET);
    switch (mode) {
        case 1: HAL_GPIO_WritePin(LED_PORT, LED_ORANGE_PIN, GPIO_PIN_SET); break;
        case 2: HAL_GPIO_WritePin(LED_PORT, LED_RED_PIN, GPIO_PIN_SET); break;
        case 3: HAL_GPIO_WritePin(LED_PORT, LED_BLUE_PIN, GPIO_PIN_SET); break;
        case 4: HAL_GPIO_WritePin(LED_PORT, LED_GREEN_PIN, GPIO_PIN_SET); break;
    }
}

/* --- Mod Parametrelerini Yukle --- */
void Set_Audio_Mode(uint8_t mode) {
    app_filter_params.ring_mod_enable = 0;
    app_filter_params.wah_enable      = 1;
    app_filter_params.lpf_enable      = 0;
    app_filter_params.hpf_enable      = 0;
    app_filter_params.bitcrush_enable = 0;
    app_filter_params.delay_enable    = 0;
    app_filter_params.master_volume   = 1.0f;

    switch (mode) {
        case 1: // Delay
            app_filter_params.delay_enable = 1;
            app_filter_params.delay_feedback = 0.6f;
            app_filter_params.delay_mix = 0.4f;
            break;
        case 2: // Ring Mod
            app_filter_params.ring_mod_enable = 1;
            app_filter_params.ring_mod_freq = 300.0f;
            app_filter_params.ring_mod_intensity = 0.4f;
            break;
        case 3: // Auto Wah
            app_filter_params.wah_enable = 1;
            app_filter_params.wah_sensitivity = 0.9f;
            app_filter_params.wah_mix = 0.8f;
            break;
        case 4: // Bitcrusher
            app_filter_params.bitcrush_enable = 1;
            app_filter_params.bitcrush_amount = 0.3f;
            app_filter_params.master_volume = 1.1f;
            break;
    }
    Filter_Set_Params(&app_filter_params);
    Update_LEDs(mode);
}

/* --- Uygulama Başlatma --- */
void App_Init(void) {
    // 1. BSP Audio Init SİLİNDİ.
    // MAX98357'nin herhangi bir I2C init komutuna ihtiyacı yoktur.

    // 2. Filtre API'sini Baslat
    Filter_Init(&app_filter_params);

    // 3. Audio Stream Hazirla ve DMA'yı başlat
    AudioStream_Init();
    AudioStream_Start(); // <--- YENİ: DMA transferini doğrudan başlatıyoruz

    // 4. Baslangic Modu
    current_audio_mode = 0;
    Set_Audio_Mode(current_audio_mode);
}

/* --- Buton ve Ana Döngü --- */
void Check_Filter_Button(void) {
    if (HAL_GPIO_ReadPin(USER_BUTTON_PORT, USER_BUTTON_PIN) == GPIO_PIN_SET) {
        HAL_Delay(50);
        if (HAL_GPIO_ReadPin(USER_BUTTON_PORT, USER_BUTTON_PIN) == GPIO_PIN_SET) {
            current_audio_mode++;
            if (current_audio_mode > 4) current_audio_mode = 0;
            Set_Audio_Mode(current_audio_mode);
            while (HAL_GPIO_ReadPin(USER_BUTTON_PORT, USER_BUTTON_PIN) == GPIO_PIN_SET) {
                HAL_Delay(10);
            }
        }
    }
}

void App_Loop(void) {
    Check_Filter_Button();
    HAL_Delay(10);
}
