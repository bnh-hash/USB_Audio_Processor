#ifndef APP_MAIN_H
#define APP_MAIN_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* ════════════════════════════════════════════
   POT KONFİGÜRASYONU  (6 Potansiyometre)
   ════════════════════════════════════════════ */
#define POT_COUNT       6
#define POT_UPDATE_MS   20    // 20 ms → 50 Hz güncelleme (pürüzsüz kontrol)

/* Pin → ADC Kanal eşlemesi
 *   POT1 → PA6  → ADC1_IN6  → Master Volume
 *   POT2 → PA7  → ADC1_IN7  → LPF Cutoff
 *   POT3 → PC4  → ADC1_IN14 → HPF Cutoff
 *   POT4 → PC5  → ADC1_IN15 → Delay Feedback
 *   POT5 → PB0  → ADC1_IN8  → Ring Mod Frekans
 *   POT6 → PB1  → ADC1_IN9  → Wah Hassasiyet
 */
static const uint32_t POT_CHANNELS[POT_COUNT] = {
    ADC_CHANNEL_6,    // POT1 → PA6  → Master Volume
    ADC_CHANNEL_7,    // POT2 → PA7  → LPF Cutoff
    ADC_CHANNEL_14,   // POT3 → PC4  → HPF Cutoff
    ADC_CHANNEL_15,   // POT4 → PC5  → Delay Feedback
    ADC_CHANNEL_8,    // POT5 → PB0  → Ring Mod Frekans
    ADC_CHANNEL_9,    // POT6 → PB1  → Wah Hassasiyet
};

extern uint16_t pot_values[POT_COUNT];

void App_Init(void);
void App_Loop(void);

#endif
