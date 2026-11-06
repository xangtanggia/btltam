#include "alarm_task.h"
#include "app_data.h"
#include "display_task.h"
#include "cmsis_os.h"
#include "setting.h"
#define ALARM_STACK_SIZE 1024U

static osThreadId_t alarmTaskHandle;

static volatile uint8_t buzzerMuted = 0;
static volatile uint8_t alarmActive = 0;
static const osThreadAttr_t alarmTaskAttributes = {
    .name = "AlarmTask",
    .stack_size = ALARM_STACK_SIZE,
    .priority = osPriorityNormal
};

static uint8_t CheckAlarm(const SensorData_t *data,
                          const AlarmThresholds_t *thresholds)
{
    uint8_t mask = 0;

    if (data->bmp280_ok &&
        data->temperature_bmp > thresholds->temperature_limit)
        mask |= ALARM_BMP_TEMP;

    if (data->dht11_ok &&
        data->temperature_dht > thresholds->temperature_limit)
        mask |= ALARM_DHT_TEMP;

    if (data->dht11_ok &&
        data->humidity > thresholds->humidity_limit)
        mask |= ALARM_HUMIDITY;

    if (data->mq135_ok &&
        data->mq135_adc > thresholds->mq135_limit)
        mask |= ALARM_MQ135;
		if (data->bmp280_ok &&
    data->pressure > thresholds->pressure_limit)
{
    mask |= ALARM_PRESSURE;
}
    return mask;
}

static void AlarmTask(void *argument)
{
    SensorData_t sensorData;
    AlarmThresholds_t thresholds;
    uint8_t currentAlarmMask = 0;

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    DisplayTask_SetAlarmMask(0);

    for (;;)
    {
        if (osMessageQueueGet(alarmQueueHandle,
                              &sensorData,
                              NULL,
                              20U) == osOK)
        {
            AppData_GetThresholds(&thresholds);

            uint8_t newAlarmMask = CheckAlarm(&sensorData, &thresholds);

            if (newAlarmMask != currentAlarmMask)
            {
                currentAlarmMask = newAlarmMask;
                DisplayTask_SetAlarmMask(currentAlarmMask);
            }
        }

        if (currentAlarmMask && !buzzerMuted)
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
        else
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    }
}
uint8_t AlarmTask_GetMute(void)
{
    return buzzerMuted;
}
void AlarmTask_SetMute(uint8_t muted)
{
    buzzerMuted = muted ? 1U : 0U;
}
uint8_t AlarmTask_ToggleMute(void)
{
    AlarmThresholds_t thresholds;

    buzzerMuted = !buzzerMuted;

    AppData_GetThresholds(&thresholds);

    Settings_Save(&thresholds, buzzerMuted);

    return buzzerMuted;
}

void AlarmTask_Init(void)
{
    AlarmThresholds_t thresholds;
    uint8_t savedMute = 0;

    AppData_GetThresholds(&thresholds);

    if (Settings_Load(&thresholds, &savedMute))
    {
        AppData_SetThresholds(&thresholds);
        buzzerMuted = savedMute;
    }
    else
    {
        buzzerMuted = 0;
    }

    alarmTaskHandle = osThreadNew(AlarmTask,
                                  NULL,
                                  &alarmTaskAttributes);

    if (alarmTaskHandle == NULL)
    {
        Error_Handler();
    }
}