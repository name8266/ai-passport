#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
int xTaskCreate(void (*fn)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *);
int xTaskNotify(TaskHandle_t,uint32_t,int);
int xTaskNotifyWait(uint32_t,uint32_t,uint32_t *,uint32_t);
void vTaskDelay(uint32_t);
