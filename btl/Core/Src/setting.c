
#include "setting.h"
#include "main.h"
#include "cmsis_os.h"
#include <string.h>
#include <math.h>

#define SETTINGS_FLASH_ADDRESS  0x0800FC00U
#define SETTINGS_MAGIC          0x53455435U

typedef struct
{
    uint32_t magic;

    float bmp_temperature_limit;
    float dht_temperature_limit;
    float humidity_limit;
    float pressure_limit;
    float mq135_limit;

    uint8_t buzzer_muted;
    uint8_t reserved[3];

    uint32_t checksum;
} SettingsData_t;

static osMutexId_t flashMutexHandle = NULL;

uint8_t Settings_Init(void)
{
    flashMutexHandle = osMutexNew(NULL);

    return (flashMutexHandle != NULL) ? 1U : 0U;
}

static uint32_t Settings_Checksum(const SettingsData_t *data)
{
    uint32_t words[7];

    memcpy(words, data, sizeof(words));

    return words[0] ^
           words[1] ^
           words[2] ^
           words[3] ^
           words[4] ^
           words[5] ^
           words[6] ^
           0xA5A55A5AU;
}

static uint8_t Settings_Validate(const SettingsData_t *data)
{
    if (!isfinite(data->bmp_temperature_limit) ||
        data->bmp_temperature_limit < 0.0f ||
        data->bmp_temperature_limit > 70.0f)
    {
        return 0U;
    }

    if (!isfinite(data->dht_temperature_limit) ||
        data->dht_temperature_limit < 0.0f ||
        data->dht_temperature_limit > 70.0f)
    {
        return 0U;
    }

    if (!isfinite(data->humidity_limit) ||
        data->humidity_limit < 0.0f ||
        data->humidity_limit > 100.0f)
    {
        return 0U;
    }

    if (!isfinite(data->pressure_limit) ||
        data->pressure_limit < 800.0f ||
        data->pressure_limit > 1200.0f)
    {
        return 0U;
    }

    if (!isfinite(data->mq135_limit) ||
        data->mq135_limit < 0.0f ||
        data->mq135_limit > 10000.0f)
    {
        return 0U;
    }

    if (data->buzzer_muted > 1U)
    {
        return 0U;
    }

    return 1U;
}

uint8_t Settings_Load(AlarmThresholds_t *thresholds,
                      uint8_t *buzzerMuted)
{
    SettingsData_t data;

    if ((thresholds == NULL) || (buzzerMuted == NULL))
    {
        return 0U;
    }

    memcpy(&data,
           (const void *)SETTINGS_FLASH_ADDRESS,
           sizeof(SettingsData_t));

    if (data.magic != SETTINGS_MAGIC)
    {
        return 0U;
    }

    if (data.checksum != Settings_Checksum(&data))
    {
        return 0U;
    }

    if (!Settings_Validate(&data))
    {
        return 0U;
    }

    thresholds->bmp_temperature_limit = data.bmp_temperature_limit;
    thresholds->dht_temperature_limit = data.dht_temperature_limit;
    thresholds->humidity_limit = data.humidity_limit;
    thresholds->mq135_limit = data.mq135_limit;
    thresholds->pressure_limit = data.pressure_limit;

    *buzzerMuted = data.buzzer_muted;

    return 1U;
}

uint8_t Settings_Save(const AlarmThresholds_t *thresholds,
                      uint8_t buzzerMuted)
{
    SettingsData_t data = {0};
    FLASH_EraseInitTypeDef erase;
    uint32_t pageError = 0U;
    uint8_t result = 1U;

    if ((thresholds == NULL) || (buzzerMuted > 1U))
    {
        return 0U;
    }

    data.magic = SETTINGS_MAGIC;
    data.bmp_temperature_limit = thresholds->bmp_temperature_limit;
    data.dht_temperature_limit = thresholds->dht_temperature_limit;
    data.humidity_limit = thresholds->humidity_limit;
    data.pressure_limit = thresholds->pressure_limit;
    data.mq135_limit = thresholds->mq135_limit;
    data.buzzer_muted = buzzerMuted;
    data.reserved[0] = 0xFFU;
    data.reserved[1] = 0xFFU;
    data.reserved[2] = 0xFFU;

    if (!Settings_Validate(&data))
    {
        return 0U;
    }

    data.checksum = Settings_Checksum(&data);

    if ((flashMutexHandle == NULL) ||
        (osMutexAcquire(flashMutexHandle, osWaitForever) != osOK))
    {
        return 0U;
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        (void)osMutexRelease(flashMutexHandle);
        return 0U;
    }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = SETTINGS_FLASH_ADDRESS;
    erase.NbPages = 1U;

    if (HAL_FLASHEx_Erase(&erase, &pageError) != HAL_OK)
    {
        result = 0U;
    }

    if (result != 0U)
    {
        const uint16_t *source = (const uint16_t *)&data;

        for (uint32_t i = 0U;
             i < (sizeof(SettingsData_t) / sizeof(uint16_t));
             i++)
        {
            if (HAL_FLASH_Program(
                    FLASH_TYPEPROGRAM_HALFWORD,
                    SETTINGS_FLASH_ADDRESS + (i * 2U),
                    source[i]) != HAL_OK)
            {
                result = 0U;
                break;
            }
        }
    }

    HAL_FLASH_Lock();
    (void)osMutexRelease(flashMutexHandle);

    return result;
}
