#include "uart_task.h"
#include "app_data.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;

#define UART_TASK_STACK_SIZE 1024U

static osThreadId_t uartTaskHandle;

static const osThreadAttr_t uartTaskAttributes = {
    .name = "UARTTask",
    .stack_size = UART_TASK_STACK_SIZE,
    .priority = osPriorityNormal
};

static void UART_SendString(const char *str)
{
    HAL_UART_Transmit(&huart1,
                      (uint8_t *)str,
                      strlen(str),
                      500);
}

static void UARTTask(void *argument)
{
	
    SensorData_t data;
    char message[160];

    UART_SendString("\r\n=== ENVIRONMENT MONITOR ===\r\n");

    for (;;)
    {
        if (osMessageQueueGet(uartQueueHandle,
                              &data,
                              NULL,
                              osWaitForever) == osOK)
        {
            snprintf(message, sizeof(message),
                     "BMP280: %s, Temp=%d C, Pressure=%d hPa\r\n",
                     data.bmp280_ok ? "OK" : "ERROR",
                     (int)data.temperature_bmp,
                     (int)data.pressure);

            UART_SendString(message);

            snprintf(message, sizeof(message),
                     "DHT11: %s, Temp=%d C, Humidity=%d %%\r\n",
                     data.dht11_ok ? "OK" : "ERROR",
                     (int)data.temperature_dht,
                     (int)data.humidity);

            UART_SendString(message);

            snprintf(message, sizeof(message),
                     "MQ135: %s, ADC=%u\r\n",
                     data.mq135_ok ? "OK" : "ERROR",
                     data.mq135_adc);

            UART_SendString(message);

            UART_SendString("----------------------------\r\n");
        }
    }
}

void UARTTask_Init(void)
{
    uartTaskHandle = osThreadNew(UARTTask,
                                 NULL,
                                 &uartTaskAttributes);

    if (uartTaskHandle == NULL)
    {
        Error_Handler();
    }
}