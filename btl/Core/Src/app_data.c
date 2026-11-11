#include "app_data.h"
#include "setting.h"
#include <math.h>

#define APP_QUEUE_LENGTH                1U

#define APP_DEFAULT_BMP_TEMP_LIMIT      35.0f
#define APP_DEFAULT_DHT_TEMP_LIMIT      35.0f
#define APP_DEFAULT_HUMIDITY_LIMIT      80.0f
#define APP_DEFAULT_MQ135_LIMIT         1000.0f
#define APP_DEFAULT_PRESSURE_LIMIT      1013.0f

static osMessageQueueId_t displayQueueHandle;
static osMessageQueueId_t alarmQueueHandle;
static osMessageQueueId_t uartQueueHandle;

static osMutexId_t thresholdMutexHandle;

static AlarmThresholds_t alarmThresholds;
static uint8_t buzzerMutedState = 0U;

static void QueueSendLatest(osMessageQueueId_t queue,
                            const SensorData_t *data)
{
    SensorData_t oldData;

    if ((queue == NULL) || (data == NULL))
    {
        return;
    }

    if (osMessageQueuePut(queue, data, 0U, 0U) != osOK)
    {
        (void)osMessageQueueGet(queue, &oldData, NULL, 0U);
        (void)osMessageQueuePut(queue, data, 0U, 0U);
    }
}

static uint8_t ThresholdValueValid(ThresholdType_t type, float value)
{
    if (!isfinite(value))
    {
        return 0U;
    }

    switch (type)
    {
        case THRESHOLD_BMP_TEMP:
        case THRESHOLD_DHT_TEMP:
            return (value >= 0.0f && value <= 70.0f) ? 1U : 0U;

        case THRESHOLD_HUMIDITY:
            return (value >= 0.0f && value <= 100.0f) ? 1U : 0U;

        case THRESHOLD_MQ135:
						return (value >= 0.0f && value <= 10000.0f)
									 ? 1U : 0U;

        case THRESHOLD_PRESSURE:
            return (value >= 800.0f && value <= 1200.0f) ? 1U : 0U;

        default:
            return 0U;
    }
}

static void ThresholdSetValue(AlarmThresholds_t *thresholds,
                              ThresholdType_t type,
                              float value)
{
    switch (type)
    {
        case THRESHOLD_BMP_TEMP:
            thresholds->bmp_temperature_limit = value;
            break;

        case THRESHOLD_DHT_TEMP:
            thresholds->dht_temperature_limit = value;
            break;

        case THRESHOLD_HUMIDITY:
            thresholds->humidity_limit = value;
            break;

				case THRESHOLD_MQ135:
						thresholds->mq135_limit = value;
						break;

        case THRESHOLD_PRESSURE:
            thresholds->pressure_limit = value;
            break;

        default:
            break;
    }
}

uint8_t AppData_Init(void)
{
    displayQueueHandle = osMessageQueueNew(
        APP_QUEUE_LENGTH,
        sizeof(SensorData_t),
        NULL
    );

    alarmQueueHandle = osMessageQueueNew(
        APP_QUEUE_LENGTH,
        sizeof(SensorData_t),
        NULL
    );

    uartQueueHandle = osMessageQueueNew(
        APP_QUEUE_LENGTH,
        sizeof(SensorData_t),
        NULL
    );

    thresholdMutexHandle = osMutexNew(NULL);

    if ((displayQueueHandle == NULL) ||
        (alarmQueueHandle == NULL) ||
        (uartQueueHandle == NULL) ||
        (thresholdMutexHandle == NULL))
    {
        return 0U;
    }

    if (!Settings_Init())
    {
        return 0U;
    }

    alarmThresholds.bmp_temperature_limit = APP_DEFAULT_BMP_TEMP_LIMIT;
    alarmThresholds.dht_temperature_limit = APP_DEFAULT_DHT_TEMP_LIMIT;
    alarmThresholds.humidity_limit = APP_DEFAULT_HUMIDITY_LIMIT;
    alarmThresholds.mq135_limit = APP_DEFAULT_MQ135_LIMIT;
    alarmThresholds.pressure_limit = APP_DEFAULT_PRESSURE_LIMIT;

    buzzerMutedState = 0U;

    return 1U;
}

void AppData_PublishSensorData(const SensorData_t *data)
{
    if (data == NULL)
    {
        return;
    }

    QueueSendLatest(displayQueueHandle, data);
    QueueSendLatest(alarmQueueHandle, data);
    QueueSendLatest(uartQueueHandle, data);
}

void AppData_GetThresholds(AlarmThresholds_t *thresholds)
{
    if ((thresholds == NULL) || (thresholdMutexHandle == NULL))
    {
        return;
    }

    if (osMutexAcquire(thresholdMutexHandle, osWaitForever) == osOK)
    {
        *thresholds = alarmThresholds;
        (void)osMutexRelease(thresholdMutexHandle);
    }
}

void AppData_SetThresholds(const AlarmThresholds_t *thresholds)
{
    if ((thresholds == NULL) || (thresholdMutexHandle == NULL))
    {
        return;
    }

    if (osMutexAcquire(thresholdMutexHandle, osWaitForever) == osOK)
    {
        alarmThresholds = *thresholds;
        (void)osMutexRelease(thresholdMutexHandle);
    }
}

uint8_t AppData_AdjustThreshold(ThresholdType_t type, int16_t delta)
{
    AlarmThresholds_t newThresholds;
    float newValue = 0.0f;
    float minValue;
    float maxValue;

    if (thresholdMutexHandle == NULL)
    {
        return 0U;
    }

    if (osMutexAcquire(thresholdMutexHandle, osWaitForever) != osOK)
    {
        return 0U;
    }

    newThresholds = alarmThresholds;

    switch (type)
    {
        case THRESHOLD_BMP_TEMP:
            newValue = alarmThresholds.bmp_temperature_limit + (float)delta;
            minValue = 0.0f;
            maxValue = 70.0f;
            break;

        case THRESHOLD_DHT_TEMP:
            newValue = alarmThresholds.dht_temperature_limit + (float)delta;
            minValue = 0.0f;
            maxValue = 70.0f;
            break;

        case THRESHOLD_HUMIDITY:
            newValue = alarmThresholds.humidity_limit + (float)delta;
            minValue = 0.0f;
            maxValue = 100.0f;
            break;

				case THRESHOLD_MQ135:
						newValue = alarmThresholds.mq135_limit + (float)delta;
						minValue = 0.0f;
						maxValue = 10000.0f;
						break;

        case THRESHOLD_PRESSURE:
            newValue = alarmThresholds.pressure_limit + (float)delta;
            minValue = 800.0f;
            maxValue = 1200.0f;
            break;

        default:
            (void)osMutexRelease(thresholdMutexHandle);
            return 0U;
    }

    if (newValue < minValue)
    {
        newValue = minValue;
    }

    if (newValue > maxValue)
    {
        newValue = maxValue;
    }

    if (!ThresholdValueValid(type, newValue))
    {
        (void)osMutexRelease(thresholdMutexHandle);
        return 0U;
    }

    ThresholdSetValue(&newThresholds, type, newValue);

 uint8_t unchanged = 0U;

switch (type)
{
    case THRESHOLD_BMP_TEMP:
        unchanged = (fabsf(newThresholds.bmp_temperature_limit -
                           alarmThresholds.bmp_temperature_limit) < 0.001f);
        break;

    case THRESHOLD_DHT_TEMP:
        unchanged = (fabsf(newThresholds.dht_temperature_limit -
                           alarmThresholds.dht_temperature_limit) < 0.001f);
        break;

    case THRESHOLD_HUMIDITY:
        unchanged = (fabsf(newThresholds.humidity_limit -
                           alarmThresholds.humidity_limit) < 0.001f);
        break;

		case THRESHOLD_MQ135:
				unchanged = (fabsf(newThresholds.mq135_limit -
													 alarmThresholds.mq135_limit) < 0.001f);
				break;

    case THRESHOLD_PRESSURE:
        unchanged = (fabsf(newThresholds.pressure_limit -
                           alarmThresholds.pressure_limit) < 0.001f);
        break;

    default:
        unchanged = 1U;
        break;
}

if (unchanged != 0U)
{
    (void)osMutexRelease(thresholdMutexHandle);
    return 0U;
}

    alarmThresholds = newThresholds;

    (void)osMutexRelease(thresholdMutexHandle);
    return 1U;
}

uint8_t AppData_UpdateThresholdAndSave(ThresholdType_t type,
                                       float value)
{
    AlarmThresholds_t newThresholds;

    if (!ThresholdValueValid(type, value) ||
        (thresholdMutexHandle == NULL))
    {
        return 0U;
    }

    if (osMutexAcquire(thresholdMutexHandle, osWaitForever) != osOK)
    {
        return 0U;
    }

    newThresholds = alarmThresholds;
    ThresholdSetValue(&newThresholds, type, value);

    if (!Settings_Save(&newThresholds, buzzerMutedState))
    {
        (void)osMutexRelease(thresholdMutexHandle);
        return 0U;
    }

    alarmThresholds = newThresholds;

    (void)osMutexRelease(thresholdMutexHandle);
    return 1U;
}

uint8_t AppData_SaveSettings(void)
{
    uint8_t result;

    if (thresholdMutexHandle == NULL)
    {
        return 0U;
    }

    if (osMutexAcquire(thresholdMutexHandle, osWaitForever) != osOK)
    {
        return 0U;
    }

    result = Settings_Save(&alarmThresholds, buzzerMutedState);

    (void)osMutexRelease(thresholdMutexHandle);

    return result;
}

uint8_t AppData_GetBuzzerMuted(void)
{
    uint8_t muted = 0U;

    if (thresholdMutexHandle == NULL)
    {
        return 0U;
    }

    if (osMutexAcquire(thresholdMutexHandle, osWaitForever) == osOK)
    {
        muted = buzzerMutedState;
        (void)osMutexRelease(thresholdMutexHandle);
    }

    return muted;
}

void AppData_SetBuzzerMuted(uint8_t muted)
{
    if ((muted > 1U) || (thresholdMutexHandle == NULL))
    {
        return;
    }

    if (osMutexAcquire(thresholdMutexHandle, osWaitForever) == osOK)
    {
        buzzerMutedState = muted;
        (void)osMutexRelease(thresholdMutexHandle);
    }
}

uint8_t AppData_SetBuzzerMutedAndSave(uint8_t muted)
{
    if ((muted > 1U) || (thresholdMutexHandle == NULL))
    {
        return 0U;
    }

    if (osMutexAcquire(thresholdMutexHandle, osWaitForever) != osOK)
    {
        return 0U;
    }

    if (!Settings_Save(&alarmThresholds, muted))
    {
        (void)osMutexRelease(thresholdMutexHandle);
        return 0U;
    }

    buzzerMutedState = muted;

    (void)osMutexRelease(thresholdMutexHandle);
    return 1U;
}

uint8_t AppData_ToggleBuzzerMutedAndSave(uint8_t *muted)
{
    uint8_t newMuted;

    if ((muted == NULL) || (thresholdMutexHandle == NULL))
    {
        return 0U;
    }

    if (osMutexAcquire(thresholdMutexHandle, osWaitForever) != osOK)
    {
        return 0U;
    }

    newMuted = buzzerMutedState ? 0U : 1U;

    if (!Settings_Save(&alarmThresholds, newMuted))
    {
        (void)osMutexRelease(thresholdMutexHandle);
        return 0U;
    }

    buzzerMutedState = newMuted;
    *muted = newMuted;

    (void)osMutexRelease(thresholdMutexHandle);
    return 1U;
}
uint8_t AppData_ReceiveDisplayData(SensorData_t *data,
                                   uint32_t timeout)
{
    if ((data == NULL) || (displayQueueHandle == NULL))
    {
        return 0U;
    }

    return (osMessageQueueGet(displayQueueHandle,
                              data,
                              NULL,
                              timeout) == osOK) ? 1U : 0U;
}

uint8_t AppData_ReceiveAlarmData(SensorData_t *data,
                                 uint32_t timeout)
{
    if ((data == NULL) || (alarmQueueHandle == NULL))
    {
        return 0U;
    }

    return (osMessageQueueGet(alarmQueueHandle,
                              data,
                              NULL,
                              timeout) == osOK) ? 1U : 0U;
}

uint8_t AppData_ReceiveUARTData(SensorData_t *data,
                                uint32_t timeout)
{
    if ((data == NULL) || (uartQueueHandle == NULL))
    {
        return 0U;
    }

    return (osMessageQueueGet(uartQueueHandle,
                              data,
                              NULL,
                              timeout) == osOK) ? 1U : 0U;
}