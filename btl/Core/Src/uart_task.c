#include "uart_task.h"
#include "app_data.h"
#include "alarm_task.h"
#include "display_task.h"
#include "setting.h"
#include "cmsis_os.h"
#include "main.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>

extern UART_HandleTypeDef huart1;

#define UART_TASK_STACK_SIZE       1024U
#define UART_RX_QUEUE_LENGTH       64U
#define UART_RX_BUFFER_SIZE        64U

#define UART_TELEMETRY_PERIOD      3000U
#define UART_TELEMETRY_PAUSE       3000U
#define UART_COMMAND_TIMEOUT       300U

#define UART_TX_TIMEOUT            100U

#define UART_TEMP_MIN              0.0f
#define UART_TEMP_MAX              70.0f
#define UART_HUM_MIN               0.0f
#define UART_HUM_MAX               100.0f
#define UART_MQ135_MIN             0.0f
#define UART_MQ135_MAX             10000.0f
#define UART_PRESS_MIN             800.0f
#define UART_PRESS_MAX             1200.0f

static osThreadId_t uartTaskHandle;
static osMessageQueueId_t uartRxQueueHandle;

static uint8_t uartRxByte;
static char uartRxBuffer[UART_RX_BUFFER_SIZE];
static uint8_t uartRxIndex = 0U;

static uint32_t uartLastRxTime = 0U;
static uint32_t uartLastTelemetryTime = 0U;
static uint32_t uartTelemetryResumeTime = 0U;

static SensorData_t latestSensorData;
static uint8_t sensorDataAvailable = 0U;

static void UART_SendString(const char *string)
{
    if (string == NULL)
    {
        return;
    }

    HAL_UART_Transmit(&huart1,
                      (uint8_t *)string,
                      (uint16_t)strlen(string),
                      UART_TX_TIMEOUT);
}

static void UART_SendReply(const char *message)
{
    UART_SendString("\r\n");
    UART_SendString(message);
}

static void UART_SendValue(const char *name, float value, const char *unit)
{
    char buffer[64];

    snprintf(buffer,
             sizeof(buffer),
             "%s: %.2f %s\r\n",
             name,
             (double)value,
             unit);

    UART_SendString(buffer);
}

static const char *UART_AlarmState(uint8_t alarmMask, uint8_t alarmBit)
{
    return ((alarmMask & alarmBit) != 0U) ? "ALARM" : "NORMAL";
}

static void UART_SendSensorData(const SensorData_t *data)
{
    char buffer[256];
    uint8_t alarmMask;

    if ((data == NULL) || (sensorDataAvailable == 0U))
    {
        return;
    }

    alarmMask = AlarmTask_GetAlarmMask();

    snprintf(buffer,
             sizeof(buffer),
             "BMP280: %s | Temp: %.2f C | Pressure: %.2f hPa\r\n",
             data->bmp280_ok ? "OK" : "ERROR",
             (double)data->temperature_bmp,
             (double)data->pressure);
    UART_SendString(buffer);

    snprintf(buffer,
             sizeof(buffer),
             "DHT11: %s | Temp: %.2f C | Humidity: %.2f %%\r\n",
             data->dht11_ok ? "OK" : "ERROR",
             (double)data->temperature_dht,
             (double)data->humidity);
    UART_SendString(buffer);

		snprintf(buffer,
						 sizeof(buffer),
						 "MQ135: %s | PPM: %.2f\r\n",
						 data->mq135_ok ? "OK" : "ERROR",
						 (double)data->mq135_ppm);
		UART_SendString(buffer);

    snprintf(buffer,
             sizeof(buffer),
             "ALARM STATUS | BMP_TEMP: %s | DHT_TEMP: %s | HUM: %s | MQ135: %s | PRESS: %s\r\n",
             UART_AlarmState(alarmMask, ALARM_BMP_TEMP),
             UART_AlarmState(alarmMask, ALARM_DHT_TEMP),
             UART_AlarmState(alarmMask, ALARM_HUMIDITY),
             UART_AlarmState(alarmMask, ALARM_MQ135),
             UART_AlarmState(alarmMask, ALARM_PRESSURE));
    UART_SendString(buffer);

    UART_SendString("----------------------------------------\r\n");
}

static void UART_SendThresholds(void)
{
    AlarmThresholds_t thresholds;
    char buffer[256];

    AppData_GetThresholds(&thresholds);

    snprintf(buffer,
             sizeof(buffer),
             "CURRENT LIMITS\r\n"
             "BMP_TEMP: %.2f C\r\n"
             "DHT_TEMP: %.2f C\r\n"
             "HUM: %.2f %%\r\n"
             "MQ135: %.2f ppm\r\n"
             "PRESS: %.2f hPa\r\n",
             (double)thresholds.bmp_temperature_limit,
             (double)thresholds.dht_temperature_limit,
             (double)thresholds.humidity_limit,
             (double)thresholds.mq135_limit,
             (double)thresholds.pressure_limit);

    UART_SendReply(buffer);
}

static uint8_t UART_ParseFloat(const char *text, float *value)
{
    char *endPointer;
    float parsedValue;

    if ((text == NULL) || (value == NULL) || (*text == '\0'))
    {
        return 0U;
    }

    parsedValue = strtof(text, &endPointer);

    if ((endPointer == text) || (*endPointer != '\0'))
    {
        return 0U;
    }

    if (!isfinite(parsedValue))
    {
        return 0U;
    }

    *value = parsedValue;

    return 1U;
}

static void UART_SetThreshold(ThresholdType_t type,
                              float value,
                              uint8_t displayIndex)
{
    char response[80];

    if (AppData_UpdateThresholdAndSave(type, value) == 0U)
    {
        UART_SendReply("ERROR: Invalid value or Flash save failed.\r\n");
        return;
    }

    snprintf(response,
             sizeof(response),
             "OK: Threshold updated and saved.\r\n");

    UART_SendReply(response);

    DisplayTask_Wake();
    DisplayTask_ShowThreshold(displayIndex);
}

static void UART_ProcessCommand(char *command)
{
    char *argument;
    char *endPointer;
    float floatValue;



    if (command == NULL)
    {
        return;
    }

    while ((*command != '\0') && isspace((unsigned char)*command))
    {
        command++;
    }

    if (*command == '\0')
    {
        return;
    }

    for (char *p = command; *p != '\0'; p++)
    {
        *p = (char)toupper((unsigned char)*p);
    }

    if (strcmp(command, "GET LIMITS") == 0)
    {
        UART_SendThresholds();
        return;
    }

    if (strcmp(command, "MUTE ON") == 0)
    {
        if (AppData_SetBuzzerMutedAndSave(1U) != 0U)
        {
            UART_SendReply("OK: BUZZER MUTED.\r\n");

            DisplayTask_Wake();
            DisplayTask_SetBuzzerStatus(1U);
        }
        else
        {
            UART_SendReply("ERROR: Flash save failed.\r\n");
        }

        return;
    }

    if (strcmp(command, "MUTE OFF") == 0)
    {
        if (AppData_SetBuzzerMutedAndSave(0U) != 0U)
        {
            UART_SendReply("OK: BUZZER UNMUTED.\r\n");

            DisplayTask_Wake();
            DisplayTask_SetBuzzerStatus(0U);
        }
        else
        {
            UART_SendReply("ERROR: Flash save failed.\r\n");
        }

        return;
    }

    if (strncmp(command, "SET ", 4U) != 0)
    {
        UART_SendReply("ERROR: Unknown command.\r\n");
        return;
    }

    argument = command + 4U;

    while ((*argument != '\0') && isspace((unsigned char)*argument))
    {
        argument++;
    }

    endPointer = argument;

    while ((*endPointer != '\0') &&
           !isspace((unsigned char)*endPointer))
    {
        endPointer++;
    }

    if (*endPointer == '\0')
    {
        UART_SendReply("ERROR: Missing value.\r\n");
        return;
    }

    *endPointer = '\0';
    endPointer++;

    while ((*endPointer != '\0') &&
           isspace((unsigned char)*endPointer))
    {
        endPointer++;
    }

    if (*endPointer == '\0')
    {
        UART_SendReply("ERROR: Missing value.\r\n");
        return;
    }

    if (strcmp(argument, "BMP_TEMP") == 0)
    {
        if ((UART_ParseFloat(endPointer, &floatValue) == 0U) ||
            (floatValue < UART_TEMP_MIN) ||
            (floatValue > UART_TEMP_MAX))
        {
            UART_SendReply("ERROR: BMP_TEMP range 0 to 70 C.\r\n");
            return;
        }

        UART_SetThreshold(THRESHOLD_BMP_TEMP, floatValue, 0U);
    }
    else if (strcmp(argument, "DHT_TEMP") == 0)
    {
        if ((UART_ParseFloat(endPointer, &floatValue) == 0U) ||
            (floatValue < UART_TEMP_MIN) ||
            (floatValue > UART_TEMP_MAX))
        {
            UART_SendReply("ERROR: DHT_TEMP range 0 to 70 C.\r\n");
            return;
        }

        UART_SetThreshold(THRESHOLD_DHT_TEMP, floatValue, 1U);
    }
    else if (strcmp(argument, "HUM") == 0)
    {
        if ((UART_ParseFloat(endPointer, &floatValue) == 0U) ||
            (floatValue < UART_HUM_MIN) ||
            (floatValue > UART_HUM_MAX))
        {
            UART_SendReply("ERROR: HUM range 0 to 100 percent.\r\n");
            return;
        }

        UART_SetThreshold(THRESHOLD_HUMIDITY, floatValue, 2U);
    }
		else if (strcmp(argument, "MQ135") == 0)
		{
				if ((UART_ParseFloat(endPointer, &floatValue) == 0U) ||
						(floatValue < UART_MQ135_MIN) ||
						(floatValue > UART_MQ135_MAX))
				{
						UART_SendReply("ERROR: MQ135 range 0 to 10000 ppm.\r\n");
						return;
				}

				UART_SetThreshold(THRESHOLD_MQ135, floatValue, 3U);
		}
    else if (strcmp(argument, "PRESS") == 0)
    {
        if ((UART_ParseFloat(endPointer, &floatValue) == 0U) ||
            (floatValue < UART_PRESS_MIN) ||
            (floatValue > UART_PRESS_MAX))
        {
            UART_SendReply("ERROR: PRESS range 800 to 1200 hPa.\r\n");
            return;
        }

        UART_SetThreshold(THRESHOLD_PRESSURE, floatValue, 4U);
    }
    else
    {
        UART_SendReply("ERROR: Unknown threshold name.\r\n");
        return;
    }
}

static void UART_ProcessReceivedByte(uint8_t receivedByte)
{
    uint32_t currentTime = HAL_GetTick();

    if ((receivedByte == '\r') || (receivedByte == '\n'))
    {
        return;
    }

    if (uartRxIndex < (UART_RX_BUFFER_SIZE - 1U))
    {
        uartRxBuffer[uartRxIndex++] = (char)receivedByte;
        uartLastRxTime = currentTime;
    }
    else
    {
        uartRxIndex = 0U;
        uartRxBuffer[0] = '\0';

        UART_SendReply("ERROR: Command too long.\r\n");
        uartTelemetryResumeTime = currentTime + UART_TELEMETRY_PAUSE;
    }
}

static void UART_CheckCommandTimeout(void)
{
    uint32_t currentTime = HAL_GetTick();

    if ((uartRxIndex > 0U) &&
        ((currentTime - uartLastRxTime) >= UART_COMMAND_TIMEOUT))
    {
        uartRxBuffer[uartRxIndex] = '\0';

        UART_ProcessCommand(uartRxBuffer);

        uartRxIndex = 0U;
        uartRxBuffer[0] = '\0';

        uartTelemetryResumeTime = currentTime + UART_TELEMETRY_PAUSE;
        uartLastTelemetryTime = currentTime;
    }
}

static void UARTTask(void *argument)
{
    uint8_t receivedByte;
    uint32_t currentTime;

    (void)argument;

    for (;;)
    {
        if (osMessageQueueGet(uartRxQueueHandle,
                              &receivedByte,
                              NULL,
                              10U) == osOK)
        {
            UART_ProcessReceivedByte(receivedByte);
        }

        UART_CheckCommandTimeout();

				while (AppData_ReceiveUARTData(&latestSensorData, 0U) != 0U)
				{
						sensorDataAvailable = 1U;
				}

        currentTime = HAL_GetTick();

        if ((sensorDataAvailable != 0U) &&
            ((int32_t)(currentTime - uartTelemetryResumeTime) >= 0) &&
            ((currentTime - uartLastTelemetryTime) >= UART_TELEMETRY_PERIOD))
        {
            UART_SendSensorData(&latestSensorData);
            uartLastTelemetryTime = currentTime;
        }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        if (uartRxQueueHandle != NULL)
        {
            (void)osMessageQueuePut(uartRxQueueHandle,
                                    &uartRxByte,
                                    0U,
                                    0U);
        }

        (void)HAL_UART_Receive_IT(&huart1, &uartRxByte, 1U);
    }
}

void UARTTask_Init(void)
{
    const osThreadAttr_t uartTask_attributes =
    {
        .name = "UARTTask",
        .stack_size = UART_TASK_STACK_SIZE,
        .priority = osPriorityNormal
    };

    uartRxQueueHandle = osMessageQueueNew(UART_RX_QUEUE_LENGTH,
                                          sizeof(uint8_t),
                                          NULL);

    if (uartRxQueueHandle == NULL)
    {
        return;
    }

    if (HAL_UART_Receive_IT(&huart1, &uartRxByte, 1U) != HAL_OK)
    {
        return;
    }

    uartTaskHandle = osThreadNew(UARTTask,
                                 NULL,
                                 &uartTask_attributes);
}