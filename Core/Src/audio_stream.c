/*
 * audio_stream.c
 *
 * Created on: Mar 2, 2026
 * Author: Gökçe
 */

#include "audio_stream.h"
#include "filter.h"
#include "app_main.h"
#include "usbd_audio_if.h"
#include <string.h>
#include "main.h" // HAL ve hi2s tanımı için gerekli

extern USBD_HandleTypeDef hUsbDeviceFS;

// !!! DİKKAT: CubeMX ayarınıza göre burası hi2s2 veya hi2s3 olabilir.
// Harici bağlantı için genelde I2S2 kullanılır.
extern I2S_HandleTypeDef hi2s2;

static uint8_t Ring_Buffer[RING_BUFFER_SIZE];
static volatile uint32_t ring_write_ptr = 0;
static volatile uint32_t ring_read_ptr = 0;
static volatile int32_t ring_available_bytes = 0;

// --- TX BUFFER ---
int16_t Audio_Tx_Buffer[TX_FULL_SAMPLES];

/* ══════════════════════════════════════════════
   DIŞARIYA AÇILAN FONKSİYONLAR
   ══════════════════════════════════════════════ */

void AudioStream_Init(void) {
    AudioStream_Reset();
}

// DMA'yı başlatan fonksiyon
void AudioStream_Start(void) {
    // Circular DMA başlat. TX_FULL_SAMPLES kadar veri gönder.
    HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t*)Audio_Tx_Buffer, TX_FULL_SAMPLES);
}

void AudioStream_Reset(void) {
    ring_write_ptr = 0;
    ring_read_ptr = 0;
    ring_available_bytes = 0;

    memset(Audio_Tx_Buffer, 0, sizeof(Audio_Tx_Buffer));
    memset(Ring_Buffer, 0, sizeof(Ring_Buffer));
}

// USB'den gelen veriyi buraya atıyoruz (USB kesmesi içinden çağrılır)
void AudioStream_Write_USB_Packet(uint8_t* pbuf, uint32_t len) {
    // Buffer taşma koruması
    if ((ring_available_bytes + len) > RING_BUFFER_SIZE) {
        return; // Veri kaybı (Overrun)
    }

    // Veriyi ring buffer'a yaz
    for (uint32_t i = 0; i < len; i++) {
        Ring_Buffer[ring_write_ptr] = pbuf[i];
        ring_write_ptr = (ring_write_ptr + 1) % RING_BUFFER_SIZE;
    }

    // Mevcut bayt sayısını artır (Kesme korumaları kaldırıldı)
    ring_available_bytes += len;
}

uint8_t AudioStream_Is_Ready_To_Play(void) {
    return (ring_available_bytes >= TARGET_LEVEL) ? 1 : 0;
}

/* ══════════════════════════════════════════════
   AKILLI OKUMA VE DÜZELTME (CORE LOGIC)
   ══════════════════════════════════════════════ */
static void Process_Audio_Chunk(int16_t* pOutputBuffer, uint32_t n_samples) {
    uint32_t bytes_needed = n_samples * 2; // Her örnek (sample) 2 byte (16-bit)

    // --- Drift Correction (Kayma Düzeltmesi) ---
    // Eğer USB'den çok hızlı veri geliyorsa (PC saatinden dolayı)
    if (ring_available_bytes > (TARGET_LEVEL + DRIFT_THRESHOLD)) {
        ring_read_ptr = (ring_read_ptr + (AUDIO_CHANNELS * BYTES_PER_SAMPLE)) % RING_BUFFER_SIZE;
        ring_available_bytes -= (AUDIO_CHANNELS * BYTES_PER_SAMPLE);
    }
    // Eğer USB'den veri yavaş geliyorsa (Underrun tehlikesi)
    else if (ring_available_bytes < (TARGET_LEVEL - DRIFT_THRESHOLD)) {
        ring_read_ptr = (ring_read_ptr - (AUDIO_CHANNELS * BYTES_PER_SAMPLE) + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;
        ring_available_bytes += (AUDIO_CHANNELS * BYTES_PER_SAMPLE);
    }

    // --- Veri Kopyalama ---
    // Eğer buffer'da yeterli veri yoksa çıkışı sessize al (sıfırla)
    if (ring_available_bytes < bytes_needed) {
        memset(pOutputBuffer, 0, bytes_needed);
    }
    else {
        uint8_t* pOut8 = (uint8_t*)pOutputBuffer;
        for (uint32_t i = 0; i < bytes_needed; i++) {
            pOut8[i] = Ring_Buffer[ring_read_ptr];
            ring_read_ptr = (ring_read_ptr + 1) % RING_BUFFER_SIZE;
        }
        ring_available_bytes -= bytes_needed;
    }
}

// DMA Buffer'ın İLK yarısı bittiğinde çalışır (I2S DMA Kesmesi)
void AudioStream_Process_Half_Transfer(void) {
    USBD_AUDIO_Sync(&hUsbDeviceFS, AUDIO_OFFSET_HALF);
    int16_t* pSafeZone = &Audio_Tx_Buffer[0];

    // USB Buffer'ından veriyi çek
    Process_Audio_Chunk(pSafeZone, TX_HALF_SAMPLES);
    // Çekilen veriye DSP filtrelerini uygula
    Filter_Apply(pSafeZone, TX_HALF_SAMPLES);
}

// DMA Buffer'ın İKİNCİ yarısı bittiğinde çalışır (I2S DMA Kesmesi)
void AudioStream_Process_Full_Transfer(void) {
    USBD_AUDIO_Sync(&hUsbDeviceFS, AUDIO_OFFSET_FULL);
    int16_t* pSafeZone = &Audio_Tx_Buffer[TX_HALF_SAMPLES];

    // USB Buffer'ından veriyi çek
    Process_Audio_Chunk(pSafeZone, TX_HALF_SAMPLES);
    // Çekilen veriye DSP filtrelerini uygula
    Filter_Apply(pSafeZone, TX_HALF_SAMPLES);
}

/* ══════════════════════════════════════════════
   HAL CALLBACK'LERİ (DMA Kesme Yakalayıcılar)
   ══════════════════════════════════════════════ */

// I2S (SPI2) Yarım Aktarım Tamamlandı (Half Transfer Complete)
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
    if(hi2s->Instance == SPI2) {
        AudioStream_Process_Half_Transfer();
    }
}

// I2S (SPI2) Tam Aktarım Tamamlandı (Transfer Complete)
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
    if(hi2s->Instance == SPI2) {
        AudioStream_Process_Full_Transfer();
    }
}
