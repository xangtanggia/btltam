#ifndef ALARM_TASK_H
#define ALARM_TASK_H

#include "main.h"
void AlarmTask_SetMute(uint8_t muted);
void AlarmTask_Init(void);
uint8_t AlarmTask_ToggleMute(void);
uint8_t AlarmTask_GetMute(void);


#endif