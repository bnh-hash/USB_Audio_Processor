/*
 * audio_stream.h
 *
 *  Created on: Mar 2, 2026
 *      Author: Gökçe
 */
#ifndef AUDIO_STREAM_H
#define AUDIO_STREAM_H

#include <stdint.h>

/* ==========================================================================
 * SES SISTEMI AYARLARI
 * ========================================================================== */

// --- SES FORMATI ---
#define AUDIO_SAMPLE_RATE    (48000) // 48 kHz
#define AUDIO_CHANNELS       (2)     // Stereo
#define BYTES_PER_SAMPLE     (2)     // 16-bit (int16) = 2 byte

// --- DMA BUFFER AYARLARI (PING-PONG) ---
// 1920 Sample = 960 Stereo Frame = 20ms Gecikme (Stabil ve Guvenli)
// Eger daha dusuk gecikme isterseniz bunu 960 (10ms) veya 480 (5ms) yapabilirsiniz.
#define TX_HALF_SAMPLES      (192) // 2ms (Not: USB paket boyutuyla uyumlu olmali)
#define TX_FULL_SAMPLES      (TX_HALF_SAMPLES * 2)

// --- RING BUFFER AYARLARI ---
// USB'den gelen paketleri depolayan ana havuz.
// DMA buffer'indan en az 2-4 kat buyuk olmalidir.
#define RING_BUFFER_SIZE     (65536)

// --- DRIFT COMPENSATION (KAYMA DUZELTME) AYARLARI ---
// Halka tamponun neresinde durmayi hedefliyoruz? (%50)
#define TARGET_LEVEL         (RING_BUFFER_SIZE / 2)

// Tampon %50'den ne kadar saparsa mudahale edelim?
#define DRIFT_THRESHOLD      (2048)

/* ==========================================================================
 * GLOBAL DEGISKENLER
 * ========================================================================== */

// DMA tarafindan I2S'e gonderilen ana veri dizisi
extern int16_t Audio_Tx_Buffer[TX_FULL_SAMPLES];

/* ==========================================================================
 * FONKSIYON PROTOTIPLERI
 * ========================================================================== */

// Sistemi baslatir ve degiskenleri sifirlar
void AudioStream_Init(void);

// [YENI] DMA Transferini baslatan fonksiyon (app_main.c'den cagrilir)
void AudioStream_Start(void);

// Pointerlari ve bufferlari temizler
void AudioStream_Reset(void);

// USB paketini Ring Buffer'a yazar (USBD_AUDIO_DataOut icinden cagirilir)
void AudioStream_Write_USB_Packet(uint8_t* pbuf, uint32_t len);

// Yeterli veri birikti mi kontrol eder (Ilk calisa baslama izni)
uint8_t AudioStream_Is_Ready_To_Play(void);

// Yari-Tamamlanma (HT) Kesmesi Logic'i (Ilk yariyi isler)
void AudioStream_Process_Half_Transfer(void);

// Tam-Tamamlanma (TC) Kesmesi Logic'i (Ikinci yariyi isler)
void AudioStream_Process_Full_Transfer(void);

#endif // AUDIO_STREAM_H
