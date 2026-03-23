/*
 * OLED.c
 *
 *  Created on: Mar 23, 2026
 *      Author: Gökçe
 */
#include "OLED.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "main.h"
#include <string.h>

/* ════════════════════════ GLOBAL DEĞİŞKENLER ════════════════════════ */
// filter.c'den gelen gerçek ses verilerini burada tutuyoruz
uint16_t display_audio_buffer[128] = {2048};

/* ════════════════════════ KALP (♥) ÇİZİMİ ════════════════════════ */
void Draw_Heart(uint8_t x_pos, uint8_t y_pos) {
    const uint8_t heart_bitmap[6] = {
        0x66, 0xFF, 0xFF, 0x7E, 0x3C, 0x18
    };
    for(int y = 0; y < 6; y++) {
        for(int x = 0; x < 8; x++) {
            if(heart_bitmap[y] & (1 << (7 - x))) {
                ssd1306_DrawPixel(x_pos + x, y_pos + y, White);
            }
        }
    }
}

/* ════════════════════════ ÜST BAŞLIK (GÖKÇE) ════════════════════════ */
void UI_DrawHeader(const char* title) {
    Draw_Heart(2, 4);
    Draw_Heart(118, 4);

    static uint8_t scroll_index = 0;
    static uint32_t last_scroll_tick = 0;

    char display_str[15] = {0};
    int title_len = strlen(title);

    // Yazı kayma hızı: 300ms (hızlandırmak için düşürebilirsin)
    if(HAL_GetTick() - last_scroll_tick > 300) {
        scroll_index++;
        if(scroll_index >= title_len + 6) {
            scroll_index = 0;
        }
        last_scroll_tick = HAL_GetTick();
    }

    for(int i = 0; i < 14; i++) {
        int char_pos = (scroll_index + i) % (title_len + 6);
        if(char_pos < title_len) {
            display_str[i] = title[char_pos];
        } else {
            display_str[i] = ' ';
        }
    }
    display_str[14] = '\0';

    ssd1306_SetCursor(14, 2);
    ssd1306_WriteString(display_str, Font_7x10, White);

    ssd1306_Line(0, 16, 127, 16, White);
}

/* ════════════════════════ ORTA ANİMASYON (GERÇEK OSİLOSKOP) ════════════════════════ */

void UI_DrawOscilloscope(void) {
    uint8_t center_y = 35;
    uint8_t max_amp = 16;
    uint8_t prev_x = 0;
    uint8_t prev_y = center_y;

    for (uint8_t x = 0; x < 128; x++) {
        int32_t sample = display_audio_buffer[x];
        int32_t centered_sample = sample - 2048;

        // Hassasiyet ayarı: Eğer dalga çok küçükse 16'yı 8 veya 4 yap.
        int8_t y_offset = centered_sample / 16;

        if(y_offset > max_amp) y_offset = max_amp;
        if(y_offset < -max_amp) y_offset = -max_amp;

        uint8_t y = center_y - y_offset;
        if (x > 0) {
            ssd1306_Line(prev_x, prev_y, x, y, White);
        }
        prev_x = x;
        prev_y = y;
    }
}

/* ════════════════════════ ALT POTANSİYOMETRELER ════════════════════════ */

void UI_DrawPotentiometers(uint8_t pot1, uint8_t pot2, uint8_t pot3, uint8_t pot4, uint8_t pot5, uint8_t pot6) {
    uint8_t pot_values[6] = {pot1, pot2, pot3, pot4, pot5, pot6};
    uint8_t box_width = 12, box_height = 6, start_y = 56, spacing = 20, offset_x = 6;

    for (int i = 0; i < 6; i++) {
        uint8_t start_x = offset_x + (i * spacing);
        ssd1306_DrawRectangle(start_x, start_y, start_x + box_width, start_y + box_height, White);
        if (pot_values[i] > 100) pot_values[i] = 100;
        uint8_t fill_width = (pot_values[i] * (box_width - 2)) / 100;
        if (fill_width > 0) {
            for(uint8_t fill_x = 0; fill_x < fill_width; fill_x++) {
                ssd1306_Line(start_x + 1 + fill_x, start_y + 1, start_x + 1 + fill_x, start_y + box_height - 1, White);
            }
        }
    }
}

/* ════════════════════════ SES KÖPRÜSÜ ════════════════════════ */

void OLED_UpdateWaveform(int16_t *incoming_audio, uint16_t length) {
    /* Eğer gelen veri 128'den küçükse, tüm ekranı doldurmak için
	her bir piksele karşılık gelen veriyi hesaplıyoruz.*/


    for(int i = 0; i < 128; i++) {
        // Gelen veriyi 128 piksellik ekrana oranla (Mapping)
        // Bu satır, 64 örnek gelse bile onu 128 piksele yayar.
    	uint16_t sample_idx = (i * length) / 128;

        // Stereo veride (L-R-L-R) sadece sol kanalı (çift indexler) alıyoruz
        int16_t raw_sample = incoming_audio[sample_idx * 2];

        // Hassasiyet: 16'ya bölmek dalgayı belirginleştirir.
        // Eğer hala çok küçükse 8 yapabilirsin.
        int32_t mapped = (raw_sample / 16) + 2048;

        if(mapped > 4095) mapped = 4095;
        if(mapped < 0) mapped = 0;

        display_audio_buffer[i] = (uint16_t)mapped;
    }
}
