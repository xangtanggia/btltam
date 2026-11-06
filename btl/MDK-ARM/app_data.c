#include "app_data.h"

#define APP_QUEUE_LENGTH                1

#define APP_DEFAULT_TEMP_LIMIT          35.0f
#define APP_DEFAULT_HUMIDITY_LIMIT      80.0f
#define APP_DEFAULT_MQ135_LIMIT         1800U
#define APP_DEFAULT_PRESSURE_LIMIT      1013.0f

osMessageQueueId_t displayQueueHandle;
osMessageQueueId_t alarmQueueHandle;
osMessageQueueId_t uartQueueHandle;

osMutexId_t thresholdMutexHandle;

static AlarmThresholds_t alarmThresholds;

static void QueueSendLatest(osMessageQueueId_t queue,
                            const SensorData_t *data)
{
    if (osMessageQueuePut(queue, data, 0, 0) != osOK)
    {
        SensorData_t oldData;

        osMessageQueueGet(queue, &oldData, NULL, 0);
        osMessageQueuePut(queue, data, 0, 0);
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
        return 0;
    }

    alarmThresholds.temperature_limit = APP_DEFAULT_TEMP_LIMIT;
    alarmThresholds.humidity_limit = APP_DEFAULT_HUMIDITY_LIMIT;
    alarmThresholds.mq135_limit = APP_DEFAULT_MQ135_LIMIT;
		alarmThresholds.pressure_limit = APP_DEFAULT_PRESSURE_LIMIT;
    return 1;
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
    if (thresholds == NULL)
    {
        return;
    }

    if (osMutexAcquire(thresholdMutexHandle, osWaitForever) == osOK)
    {
        *thresholds = alarmThresholds;

        osMutexRelease(thresholdMutexHandle);
    }
}

void AppData_SetThresholds(const AlarmThresholds_t *thresholds)
{
    if (thresholds == NULL)
    {
        return;
    }

    if (osMutexAcquire(thresholdMutexHandle, osWaitForever) == osOK)
    {
        alarmThresholds = *thresholds;

        osMutexRelease(thresholdMutexHandle);
    }
}