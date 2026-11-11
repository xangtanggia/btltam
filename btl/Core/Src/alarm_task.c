#include "alarm_task.h"
#include "app_data.h"
#include "display_task.h"
#include "cmsis_os.h"
#include "main.h"
#define ALARM_STACK_SIZE       1024U
#define ALARM_TASK_PRIORITY    osPriorityNormal

static osThreadId_t alarmTaskHandle;
static uint8_t currentAlarmMask = 0U;
static osMutexId_t alarmMaskMutex = NULL;

static uint8_t CheckAlarm(const SensorData_t *data,
                          const AlarmThresholds_t *thresholds)
{
    uint8_t alarmMask = 0U;

    if (data == NULL || thresholds == NULL)
    {
        return 0U;
    }


    if (data->bmp280_ok &&
        data->temperature_bmp > thresholds->bmp_temperature_limit)
    {
        alarmMask |= ALARM_BMP_TEMP;
    }


    if (data->dht11_ok &&
        data->temperature_dht > thresholds->dht_temperature_limit)
    {
        alarmMask |= ALARM_DHT_TEMP;
    }


    if (data->dht11_ok &&
        data->humidity > thresholds->humidity_limit)
    {
        alarmMask |= ALARM_HUMIDITY;
    }


		if (data->mq135_ok &&
				data->mq135_ppm > thresholds->mq135_limit)
		{
				alarmMask |= ALARM_MQ135;
		}


    if (data->bmp280_ok &&
        data->pressure > thresholds->pressure_limit)
    {
        alarmMask |= ALARM_PRESSURE;
    }

    return alarmMask;
}

static void AlarmTask(void *argument)
{
		uint8_t activeAlarmMask = 0U;
    SensorData_t sensorData;
    AlarmThresholds_t thresholds;
    uint8_t newAlarmMask;
    uint8_t buzzerMuted;

    (void)argument;

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);

    for (;;)
    {
		if (AppData_ReceiveAlarmData(&sensorData, 50U) != 0U)
        {
            AppData_GetThresholds(&thresholds);

            newAlarmMask = CheckAlarm(&sensorData, &thresholds);

						activeAlarmMask = newAlarmMask;

						if (osMutexAcquire(alarmMaskMutex, osWaitForever) == osOK)
						{
								currentAlarmMask = newAlarmMask;
								(void)osMutexRelease(alarmMaskMutex);
						}

						DisplayTask_SetAlarmMask(newAlarmMask);
        }

        buzzerMuted = AppData_GetBuzzerMuted();

        if ((activeAlarmMask != 0U) && (buzzerMuted == 0U))
        {
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
        }
        else
        {
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
        }
    }
}

void AlarmTask_Init(void)
{
    const osThreadAttr_t alarmTask_attributes = {
        .name = "AlarmTask",
        .stack_size = ALARM_STACK_SIZE,
        .priority = ALARM_TASK_PRIORITY
    };

    alarmMaskMutex = osMutexNew(NULL);

    if (alarmMaskMutex == NULL)
    {
        Error_Handler();
    }

    alarmTaskHandle = osThreadNew(AlarmTask,
                                  NULL,
                                  &alarmTask_attributes);

    if (alarmTaskHandle == NULL)
    {
        Error_Handler();
    }
}

uint8_t AlarmTask_GetAlarmMask(void)
{
    uint8_t mask = 0U;

    if (alarmMaskMutex == NULL)
    {
        return 0U;
    }

    if (osMutexAcquire(alarmMaskMutex, osWaitForever) == osOK)
    {
        mask = currentAlarmMask;
        (void)osMutexRelease(alarmMaskMutex);
    }

    return mask;
}