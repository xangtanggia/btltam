#ifndef DISPLAY_TASK_H
#define DISPLAY_TASK_H

#include "main.h"
#include <stdint.h>
#define ALARM_BMP_TEMP    0x01U
#define ALARM_DHT_TEMP    0x02U
#define ALARM_HUMIDITY    0x04U
#define ALARM_MQ135       0x08U
#define ALARM_PRESSURE    0x10U
void DisplayTask_Init(void);
void DisplayTask_Wake(void);
void DisplayTask_SetAlarmMask(uint8_t alarmMask);
void DisplayTask_SetSetting(uint8_t active, uint8_t selected);
void DisplayTask_SetBuzzerStatus(uint8_t muted);
#endif