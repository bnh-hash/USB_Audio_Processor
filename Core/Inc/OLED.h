/*
 * OLED.h
 *
 *  Created on: Mar 23, 2026
 *      Author: Gökçe
 */
#ifndef INC_OLED_H_
#define INC_OLED_H_

#include "stdint.h"

// Fonksiyon Prototipleri

void UI_DrawHeader(const char* title);
void UI_DrawOscilloscope(void);
void UI_DrawPotentiometers(uint8_t pot1, uint8_t pot2, uint8_t pot3, uint8_t pot4, uint8_t pot5, uint8_t pot6);
void OLED_UpdateWaveform(int16_t *incoming_audio, uint16_t length);

#endif /* INC_OLED_H_ */
