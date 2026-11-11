#ifndef SETTINGS_H
#define SETTINGS_H

#include "app_data.h"
#include <stdint.h>

uint8_t Settings_Init(void);

uint8_t Settings_Load(AlarmThresholds_t *thresholds,
                      uint8_t *buzzerMuted);

uint8_t Settings_Save(const AlarmThresholds_t *thresholds,
                      uint8_t buzzerMuted);

#endif