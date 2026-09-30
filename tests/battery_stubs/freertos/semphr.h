#pragma once
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t mutex,int timeout);
void xSemaphoreGive(SemaphoreHandle_t mutex);
