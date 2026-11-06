
#include "setting.h"
#include "main.h"
#include <string.h>

#define SETTINGS_FLASH_ADDRESS  0x0800FC00U
#define SETTINGS_MAGIC          0x53455433U

typedef struct
{
    uint32_t magic;
    float temperature_limit;
    float humidity_limit;
    float pressure_limit;
    uint16_t mq135_limit;
    uint8_t buzzer_muted;
    uint8_t reserved;
    uint32_t checksum;
} SettingsData_t;

static uint32_t Settings_Checksum(const SettingsData_t *data)
{
    const uint32_t *words = (const uint32_t *)data;

    return words[0] ^ words[1] ^ words[2] ^
           words[3] ^ words[4] ^ 0xA5A55A5AU;
}

uint8_t Settings_Load(AlarmThresholds_t *thresholds,
                      uint8_t *buzzerMuted)
{
    if (thresholds == NULL || buzzerMuted == NULL)
        return 0;

    const SettingsData_t *flashData =
        (const SettingsData_t *)SETTINGS_FLASH_ADDRESS;

    SettingsData_t data;
    memcpy(&data, flashData, sizeof(SettingsData_t));

    if (data.magic != SETTINGS_MAGIC)
        return 0;

    if (data.checksum != Settings_Checksum(&data))
        return 0;

    if (data.temperature_limit < 0.0f ||
        data.temperature_limit > 70.0f)
        return 0;

    if (data.humidity_limit < 0.0f ||
        data.humidity_limit > 100.0f)
        return 0;

    if (data.mq135_limit > 4095U)
        return 0;

    if (data.pressure_limit < 0.0f ||
        data.pressure_limit > 1200.0f)
        return 0;

    if (data.buzzer_muted > 1U)
        return 0;

    thresholds->temperature_limit = data.temperature_limit;
    thresholds->humidity_limit = data.humidity_limit;
    thresholds->mq135_limit = data.mq135_limit;
    thresholds->pressure_limit = data.pressure_limit;

    *buzzerMuted = data.buzzer_muted;

    return 1;
}

uint8_t Settings_Save(const AlarmThresholds_t *thresholds,
                      uint8_t buzzerMuted)
{
    if (thresholds == NULL || buzzerMuted > 1U)
        return 0;

    if (thresholds->temperature_limit < 0.0f ||
        thresholds->temperature_limit > 70.0f ||
        thresholds->humidity_limit < 0.0f ||
        thresholds->humidity_limit > 100.0f ||
        thresholds->mq135_limit > 4095U ||
        thresholds->pressure_limit < 0.0f ||
        thresholds->pressure_limit > 1200.0f)
    {
        return 0;
    }

    SettingsData_t data = {0};

    data.magic = SETTINGS_MAGIC;
    data.temperature_limit = thresholds->temperature_limit;
    data.humidity_limit = thresholds->humidity_limit;
    data.pressure_limit = thresholds->pressure_limit;
    data.mq135_limit = thresholds->mq135_limit;
    data.buzzer_muted = buzzerMuted;
    data.reserved = 0xFFU;
    data.checksum = Settings_Checksum(&data);

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase;
    uint32_t pageError = 0;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = SETTINGS_FLASH_ADDRESS;
    erase.NbPages = 1;

    if (HAL_FLASHEx_Erase(&erase, &pageError) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return 0;
    }

    const uint16_t *source = (const uint16_t *)&data;

    for (uint32_t i = 0; i < sizeof(SettingsData_t) / 2; i++)
    {
        if (HAL_FLASH_Program(
                FLASH_TYPEPROGRAM_HALFWORD,
                SETTINGS_FLASH_ADDRESS + i * 2,
                source[i]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return 0;
        }
    }

    HAL_FLASH_Lock();

    return 1;
}
