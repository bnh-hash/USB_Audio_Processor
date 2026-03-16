#include "button.h"
// Buton önceki fiziksel durumu (Pull-Up: Basılıyken RESET)
static uint8_t last_bypass_state = GPIO_PIN_SET;
static uint8_t last_mute_state   = GPIO_PIN_SET;
// Debounce zamanlayıcıları
static uint32_t last_bypass_press_time = 0;
static uint32_t last_mute_press_time   = 0;
// DSP durumları (Her ikisi de 0 olarak başlar - ses kesilmez!)
static uint8_t is_bypassed = 0;
static uint8_t is_muted    = 0;
void Buton_Init(void) {
    is_bypassed = 0;
    is_muted    = 0;
    last_bypass_state = GPIO_PIN_SET;
    last_mute_state   = GPIO_PIN_SET;
    // Başlangıçta LED'lerin ikisi de sönük
    HAL_GPIO_WritePin(LED_RED_PORT,   LED_RED_PIN,   GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_RESET);
}
uint8_t Get_Bypass_State(void) { return is_bypassed; }
uint8_t Get_Mute_State(void)   { return is_muted; }
void Button_Process(void) {
    uint32_t now = HAL_GetTick();
    // --- BYPASS BUTONU (Düşen kenar: SET -> RESET) ---
    uint8_t b = HAL_GPIO_ReadPin(BYPASS_BTN_PORT, BYPASS_BTN_PIN);
    if (b == GPIO_PIN_RESET && last_bypass_state == GPIO_PIN_SET) {
        if (now - last_bypass_press_time > 50) {
            is_bypassed = !is_bypassed;
            HAL_GPIO_WritePin(LED_RED_PORT, LED_RED_PIN,
                              is_bypassed ? GPIO_PIN_SET : GPIO_PIN_RESET);
            last_bypass_press_time = now;
        }
    }
    last_bypass_state = b;
    // --- MUTE BUTONU (Düşen kenar: SET -> RESET) ---
    uint8_t m = HAL_GPIO_ReadPin(MUTE_BTN_PORT, MUTE_BTN_PIN);
    if (m == GPIO_PIN_RESET && last_mute_state == GPIO_PIN_SET) {
        if (now - last_mute_press_time > 50) {
            is_muted = !is_muted;
            HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN,
                              is_muted ? GPIO_PIN_SET : GPIO_PIN_RESET);
            last_mute_press_time = now;
        }
    }
    last_mute_state = m;
}
