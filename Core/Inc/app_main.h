#ifndef APP_MAIN_H
#define APP_MAIN_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define POT_COUNT       5
#define POT_UPDATE_MS   200

/* Pin → Kanal eşlemesi (CubeMX görseline göre) */
static const uint32_t POT_CHANNELS[POT_COUNT] = {
    ADC_CHANNEL_6,    // POT0 → PA6  → Ring Mod Frekans
    ADC_CHANNEL_7,    // POT1 → PA7  → Ring Mod Yoğunluk
    ADC_CHANNEL_14,   // POT2 → PC4  → Wah Hassasiyet
    ADC_CHANNEL_15,   // POT3 → PC5  → Delay Feedback
    ADC_CHANNEL_9     // POT4 → PB0  → Master Volume
};

extern uint16_t pot_values[POT_COUNT];

void App_Init(void);
void App_Loop(void);

#endif
