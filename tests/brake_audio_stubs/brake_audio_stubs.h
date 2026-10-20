#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef int esp_err_t;
typedef unsigned TickType_t;
typedef int BaseType_t;
typedef struct fake_queue *QueueHandle_t;
typedef void (*TaskFunction_t)(void *);
#define ESP_OK 0
#define ESP_FAIL -1
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
QueueHandle_t xQueueCreate(unsigned count,unsigned size);
void vQueueDelete(QueueHandle_t q);
int xQueueSend(QueueHandle_t q,const void *data,unsigned wait);
int xQueueOverwrite(QueueHandle_t q,const void *data);
int xQueueReceive(QueueHandle_t q,void *data,unsigned wait);
void xQueueReset(QueueHandle_t q);
int xTaskCreate(TaskFunction_t entry,const char *name,unsigned stack,void *arg,unsigned priority,void *task);
void vTaskDelete(void *task);
esp_err_t bsp_audio_init(void);
esp_err_t bsp_audio_set_format(uint32_t hz,uint8_t bits,uint8_t channels);
esp_err_t bsp_audio_write(const void *pcm,size_t size);
void bsp_audio_set_volume(uint8_t volume);
