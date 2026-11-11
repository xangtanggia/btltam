#ifndef APP_DATA_H
#define APP_DATA_H

#include "cmsis_os.h"
#include <stdint.h>

typedef struct
{
    float temperature_bmp;
    float temperature_dht;
    float humidity;
    float pressure;
		float mq135_ppm;
    uint8_t bmp280_ok;
    uint8_t dht11_ok;
    uint8_t mq135_ok;
} SensorData_t;

typedef struct
{
    float bmp_temperature_limit;
    float dht_temperature_limit;
    float humidity_limit;
    float mq135_limit;
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

typedef enum
{
    THRESHOLD_BMP_TEMP = 0,
    THRESHOLD_DHT_TEMP,
    THRESHOLD_HUMIDITY,
    THRESHOLD_MQ135,
    THRESHOLD_PRESSURE
} ThresholdType_t;

uint8_t AppData_ReceiveDisplayData(SensorData_t *data,uint32_t timeout);

uint8_t AppData_ReceiveAlarmData(SensorData_t *data,uint32_t timeout);

uint8_t AppData_ReceiveUARTData(SensorData_t *data,uint32_t timeout);

uint8_t AppData_Init(void);

void AppData_PublishSensorData(const SensorData_t *data);

void AppData_GetThresholds(AlarmThresholds_t *thresholds);
void AppData_SetThresholds(const AlarmThresholds_t *thresholds);

uint8_t AppData_AdjustThreshold(ThresholdType_t type, int16_t delta);

uint8_t AppData_UpdateThresholdAndSave(ThresholdType_t type,
                                       float value);

uint8_t AppData_SaveSettings(void);

uint8_t AppData_GetBuzzerMuted(void);
void AppData_SetBuzzerMuted(uint8_t muted);

uint8_t AppData_SetBuzzerMutedAndSave(uint8_t muted);
uint8_t AppData_ToggleBuzzerMutedAndSave(uint8_t *muted);

#endif