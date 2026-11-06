#ifndef APP_DATA_H
#define APP_DATA_H

#include "cmsis_os2.h"
#include <stdint.h>

typedef struct
{
    float temperature_bmp;
    float temperature_dht;
    float humidity;
    float pressure;
    uint16_t mq135_adc;

    uint8_t bmp280_ok;
    uint8_t dht11_ok;
		uint8_t mq135_ok;
} SensorData_t;

typedef struct
{
    float temperature_limit;
    float humidity_limit;
    uint16_t mq135_limit;
	  float pressure_limit;
} AlarmThresholds_t;

typedef enum
{
    BUTTON_NONE = 0,
    BUTTON_MUTE,
    BUTTON_WAKE,
    BUTTON_SET,
    BUTTON_UP,
    BUTTON_DOWN
} ButtonEvent_t;

extern osMessageQueueId_t displayQueueHandle;
extern osMessageQueueId_t alarmQueueHandle;
extern osMessageQueueId_t uartQueueHandle;

extern osMutexId_t thresholdMutexHandle;

uint8_t AppData_Init(void);

void AppData_PublishSensorData(const SensorData_t *data);

void AppData_GetThresholds(AlarmThresholds_t *thresholds);
void AppData_SetThresholds(const AlarmThresholds_t *thresholds);

#endif