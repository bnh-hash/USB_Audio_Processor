#ifndef INC_BUTTON_H_
#define INC_BUTTON_H_
#include "stm32f4xx_hal.h"
// --- BUTON PİNLERİ (CubeIDE'da GPIOE olarak ayarladığınız)---
#define BYPASS_BTN_PORT   GPIOE
#define BYPASS_BTN_PIN    GPIO_PIN_2
#define MUTE_BTN_PORT     GPIOE
#define MUTE_BTN_PIN      GPIO_PIN_3
// --- LED PİNLERİ ---
#define LED_RED_PORT      GPIOE
#define LED_RED_PIN       GPIO_PIN_5   // Bypass aktifken yanar
#define LED_GREEN_PORT    GPIOE
#define LED_GREEN_PIN     GPIO_PIN_4   // Mute aktifken yanar
// --- FONKSİYON PROTOTİPLERİ ---
void Buton_Init(void);
uint8_t Get_Bypass_State(void);
uint8_t Get_Mute_State(void);
void Button_Process(void);
#endif /* INC_BUTTON_H_ */
