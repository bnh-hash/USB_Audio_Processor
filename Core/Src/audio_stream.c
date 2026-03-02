/*
 * audio_stream.c
 *
 *  Created on: Mar 2, 2026
 *      Author: Gökçe
 */

/*
 * audio_stream.c
 */
#include <filter.h>
#include "audio_stream.h"
#include "app_main.h"
#include "usbd_audio_if.h"
#include <string.h>
#include "main.h" // HAL ve hi2s tanımı için gerekli

extern USBD_HandleTypeDef hUsbDeviceFS;
extern volatile float carrier_phase;

// !!! DİKKAT: CubeMX ayarınıza göre burası hi2s2 veya hi2s3 olabilir.
// Harici bağlantı için genelde I2S2 kullanılır.
extern I2S_HandleTypeDef hi2s2;

static uint8_t Ring_Buffer[RING_BUFFER_SIZE];
static volatile uint32_t ring_write_ptr = 0;
static volatile uint32_t ring_read_ptr = 0;
static volatile int32_t ring_available_bytes = 0;

// --- TX BUFFER ---
int16_t Audio_Tx_Buffer[TX_FULL_SAMPLES];

// --- DISARIYA ACILAN FONKSIYONLAR ---

void AudioStream_Init(void) {
    AudioStream_Reset();
}

// YENİ: DMA'yı başlatan fonksiyon
void AudioStream_Start(void) {
    // Circular DMA başlat. TX_FULL_SAMPLES (veya array size) kadar veri gönder.
    HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t*)Audio_Tx_Buffer, TX_FULL_SAMPLES);
}

void AudioStream_Reset(void) {
    ring_write_ptr = 0;
    ring_read_ptr = 0;
    ring_available_bytes = 0;
    memset(Audio_Tx_Buffer, 0, sizeof(Audio_Tx_Buffer));
    memset(Ring_Buffer, 0, sizeof(Ring_Buffer));
}

// USB'den gelen veriyi buraya atiyoruz
void AudioStream_Write_USB_Packet(uint8_t* pbuf, uint32_t len) {
    if ((ring_available_bytes + len) > RING_BUFFER_SIZE) {
        return;
    }
    for (uint32_t i = 0; i < len; i++) {
        Ring_Buffer[ring_write_ptr] = pbuf[i];
        ring_write_ptr = (ring_write_ptr + 1) % RING_BUFFER_SIZE;
    }
    __disable_irq();
    ring_available_bytes += len;
    __enable_irq();
}

uint8_t AudioStream_Is_Ready_To_Play(void) {
    if (ring_available_bytes >= TARGET_LEVEL) {
        return 1;
    }
    return 0;
}

// --- AKILLI OKUMA VE DUZELTME (CORE LOGIC) ---
static void Process_Audio_Chunk(int16_t* pOutputBuffer, uint32_t n_samples) {
    uint32_t bytes_needed = n_samples * 2;

    // Drift Correction
    if (ring_available_bytes > (TARGET_LEVEL + DRIFT_THRESHOLD))
    {
        ring_read_ptr = (ring_read_ptr + (AUDIO_CHANNELS * BYTES_PER_SAMPLE)) % RING_BUFFER_SIZE;
        __disable_irq();
        ring_available_bytes -= (AUDIO_CHANNELS * BYTES_PER_SAMPLE);
        __enable_irq();
    }
    else if (ring_available_bytes < (TARGET_LEVEL - DRIFT_THRESHOLD))
    {
        ring_read_ptr = (ring_read_ptr - (AUDIO_CHANNELS * BYTES_PER_SAMPLE) + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;
        __disable_irq();
        ring_available_bytes += (AUDIO_CHANNELS * BYTES_PER_SAMPLE);
        __enable_irq();
    }

    // Kopyalama
    if (ring_available_bytes < bytes_needed) {
        memset(pOutputBuffer, 0, bytes_needed);
    }
    else {
        uint8_t* pOut8 = (uint8_t*)pOutputBuffer;
        for (uint32_t i = 0; i < bytes_needed; i++) {
            pOut8[i] = Ring_Buffer[ring_read_ptr];
            ring_read_ptr = (ring_read_ptr + 1) % RING_BUFFER_SIZE;
        }
        __disable_irq();
        ring_available_bytes -= bytes_needed;
        __enable_irq();
    }
}

void AudioStream_Process_Half_Transfer(void) {
    USBD_AUDIO_Sync(&hUsbDeviceFS, AUDIO_OFFSET_HALF);
    int16_t* pSafeZone = &Audio_Tx_Buffer[0];
    Process_Audio_Chunk(pSafeZone, TX_HALF_SAMPLES);
    Filter_Apply(pSafeZone, TX_HALF_SAMPLES);
}

void AudioStream_Process_Full_Transfer(void) {
    USBD_AUDIO_Sync(&hUsbDeviceFS, AUDIO_OFFSET_FULL);
    int16_t* pSafeZone = &Audio_Tx_Buffer[TX_HALF_SAMPLES];
    Process_Audio_Chunk(pSafeZone, TX_HALF_SAMPLES);
    Filter_Apply(pSafeZone, TX_HALF_SAMPLES);
}

/* --- YENİ EKLENEN HAL CALLBACKLERİ --- */
// BSP olmadığı için DMA kesmelerini biz yakalıyoruz

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
    // Hangi I2S hattını kullanıyorsanız kontrol edin (I2S2 -> SPI2)
    if(hi2s->Instance == SPI2) {
        AudioStream_Process_Half_Transfer();
    }
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
    if(hi2s->Instance == SPI2) {
        AudioStream_Process_Full_Transfer();
    }
}
