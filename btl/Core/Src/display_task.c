
#include "display_task.h"
#include "app_data.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "main.h"
extern I2C_HandleTypeDef hi2c2;

#define LCD_ADDR                  (0x27U << 1)
#define LCD_BACKLIGHT             0x08U
#define LCD_ENABLE                0x04U
#define LCD_RS                    0x01U

#define DISPLAY_STACK_SIZE        1024U
#define DISPLAY_PERIOD            100U
#define DISPLAY_TIMEOUT           180000U
#define DISPLAY_PAGE_TIME         3000U
#define BUZZER_STATUS_TIME         2000U
#define THRESHOLD_STATUS_TIME      2000U
#define ALARM_PAGE_TIME            2000U

#define DISPLAY_PAGE_COUNT        3U
#define ALARM_COUNT               5U

static osThreadId_t displayTaskHandle;

static osMutexId_t displayStateMutex = NULL;

static uint8_t wakeRequested = 1U;
static uint8_t alarmMask = 0U;
static uint8_t alarmRefresh = 0U;

static uint8_t buzzerStatusRequested = 0U;
static uint8_t buzzerMutedDisplay = 0U;

static uint8_t thresholdStatusRefresh = 0U;
static uint8_t thresholdStatusSelected = 0U;

static uint8_t buzzerStatusActive = 0U;
static uint8_t thresholdStatusActive = 0U;

static uint32_t buzzerStatusUntil = 0U;
static uint32_t thresholdStatusUntil = 0U;

static uint8_t alarmPageIndex = 0U;
static uint32_t lastAlarmChange = 0U;

static uint8_t lcdBacklight = LCD_BACKLIGHT;

static const osThreadAttr_t displayTaskAttributes =
{
    .name = "DisplayTask",
    .stack_size = DISPLAY_STACK_SIZE,
    .priority = osPriorityNormal
};



static void LCD_WriteByte(uint8_t data)
{
    HAL_I2C_Master_Transmit(&hi2c2,
                            LCD_ADDR,
                            &data,
                            1U,
                            100U);
}

static void LCD_PulseEnable(uint8_t data)
{
    LCD_WriteByte(data | LCD_ENABLE);
    HAL_Delay(1U);

    LCD_WriteByte(data & (uint8_t)~LCD_ENABLE);
    HAL_Delay(1U);
}

static void LCD_Write4Bits(uint8_t data)
{
    LCD_WriteByte(data | lcdBacklight);
    LCD_PulseEnable(data | lcdBacklight);
}

static void LCD_Send(uint8_t value, uint8_t mode)
{
    uint8_t high = (value & 0xF0U) | mode;
    uint8_t low = ((value << 4U) & 0xF0U) | mode;

    LCD_Write4Bits(high);
    LCD_Write4Bits(low);
}

static void LCD_Command(uint8_t command)
{
    LCD_Send(command, 0U);
}

static void LCD_Data(uint8_t data)
{
    LCD_Send(data, LCD_RS);
}

static void LCD_Init(void)
{
    HAL_Delay(50U);

    LCD_Write4Bits(0x30U);
    HAL_Delay(5U);

    LCD_Write4Bits(0x30U);
    HAL_Delay(1U);

    LCD_Write4Bits(0x30U);
    HAL_Delay(1U);

    LCD_Write4Bits(0x20U);
    HAL_Delay(1U);

    LCD_Command(0x28U);
    LCD_Command(0x0CU);
    LCD_Command(0x06U);
    LCD_Command(0x01U);

    HAL_Delay(2U);
}

static void LCD_Clear(void)
{
    LCD_Command(0x01U);
    HAL_Delay(2U);
}

static void LCD_SetCursor(uint8_t row, uint8_t col)
{
    uint8_t address = (row == 0U) ? 0x80U : 0xC0U;

    LCD_Command(address + col);
}

static void LCD_Print(const char *str)
{
    while (*str != '\0')
    {
        LCD_Data((uint8_t)*str++);
    }
}

static void LCD_BacklightOn(void)
{
    lcdBacklight = LCD_BACKLIGHT;
    LCD_WriteByte(lcdBacklight);
}

static void LCD_BacklightOff(void)
{
    lcdBacklight = 0U;
    LCD_WriteByte(0U);
}

static void LCD_DisplayOff(void)
{
    LCD_Command(0x08U);
    LCD_BacklightOff();
}

static void LCD_DisplayOn(void)
{
    LCD_BacklightOn();
    LCD_Command(0x0CU);
}


static void LCD_PrintLine(uint8_t row, const char *str)
{
    uint8_t i = 0U;

    LCD_SetCursor(row, 0U);

    while ((i < 16U) && (str[i] != '\0'))
    {
        LCD_Data((uint8_t)str[i]);
        i++;
    }

    while (i < 16U)
    {
        LCD_Data(' ');
        i++;
    }
}



static void Display_ShowPage(const SensorData_t *data, uint8_t page)
{
    char line1[17];
    char line2[17];

    if (data == NULL)
    {
        return;
    }

    switch (page)
    {
        case 0U:
            if (data->bmp280_ok)
            {
                snprintf(line1, sizeof(line1),
                         "BMP:%5.1f C",
                         (double)data->temperature_bmp);
            }
            else
            {
                snprintf(line1, sizeof(line1), "BMP: --.- C");
            }

            if (data->dht11_ok)
            {
                snprintf(line2, sizeof(line2),
                         "DHT:%5.1f C",
                         (double)data->temperature_dht);
            }
            else
            {
                snprintf(line2, sizeof(line2), "DHT: --.- C");
            }
            break;

        case 1U:
            if (data->dht11_ok)
            {
                snprintf(line1, sizeof(line1),
                         "HUM:%5.1f %%",
                         (double)data->humidity);
            }
            else
            {
                snprintf(line1, sizeof(line1), "HUM: --.- %%");
            }

            if (data->bmp280_ok)
            {
                snprintf(line2, sizeof(line2),
                         "P:%7.1f hPa",
                         (double)data->pressure);
            }
            else
            {
                snprintf(line2, sizeof(line2), "P: -------");
            }
            break;

						case 2U:
								if (data->mq135_ok)
								{
										snprintf(line1, sizeof(line1),
														 "MQ135:%6.1fppm",
														 (double)data->mq135_ppm);
								}
								else
								{
										snprintf(line1, sizeof(line1), "MQ135: ERROR");
								}

								snprintf(line2, sizeof(line2), "ENV MONITOR");
								break;

        default:
            return;
    }

    LCD_PrintLine(0U, line1);
    LCD_PrintLine(1U, line2);
}



static void Display_ShowBuzzerStatus(uint8_t muted)
{
    LCD_PrintLine(0U, "BUZZER STATUS");
    LCD_PrintLine(1U, muted ? "BUZZER: OFF" : "BUZZER: ON");
}



static void Display_ShowThreshold(uint8_t selected)
{
    AlarmThresholds_t thresholds;
    char line1[17];
    char line2[17];

    AppData_GetThresholds(&thresholds);

    switch (selected)
    {
        case 0U:
            snprintf(line1, sizeof(line1), "SET BMP TEMP");
            snprintf(line2, sizeof(line2),
                     "LIMIT:%5.1f C",
                     (double)thresholds.bmp_temperature_limit);
            break;

        case 1U:
            snprintf(line1, sizeof(line1), "SET DHT TEMP");
            snprintf(line2, sizeof(line2),
                     "LIMIT:%5.1f C",
                     (double)thresholds.dht_temperature_limit);
            break;

        case 2U:
            snprintf(line1, sizeof(line1), "SET HUM LIMIT");
            snprintf(line2, sizeof(line2),
                     "LIMIT:%5.1f %%",
                     (double)thresholds.humidity_limit);
            break;

				case 3U:
						snprintf(line1, sizeof(line1), "SET MQ135 LIMIT");
						snprintf(line2, sizeof(line2),
										 "LIMIT:%6.1fppm",
										 (double)thresholds.mq135_limit);
						break;
						
        case 4U:
            snprintf(line1, sizeof(line1), "SET PRESS LIMIT");
            snprintf(line2, sizeof(line2),
                     "LIMIT:%5.1f hPa",
                     (double)thresholds.pressure_limit);
            break;

        default:
            return;
    }

    LCD_PrintLine(0U, line1);
    LCD_PrintLine(1U, line2);
}



static void Display_ShowAlarm(const SensorData_t *data,
                              uint8_t mask,
                              uint8_t startIndex)
{
    AlarmThresholds_t thresholds;
    char line1[17];
    char line2[17];

    if (data == NULL)
    {
        return;
    }

    AppData_GetThresholds(&thresholds);

    for (uint8_t i = 0U; i < ALARM_COUNT; i++)
    {
        uint8_t index = (uint8_t)((startIndex + i) % ALARM_COUNT);

        if ((mask & (1U << index)) == 0U)
        {
            continue;
        }

        switch (index)
        {
            case 0U:
                snprintf(line1, sizeof(line1), "BMP TEMP ALARM");

                snprintf(line2, sizeof(line2),
                         "%.1f > %.1f C",
                         (double)data->temperature_bmp,
                         (double)thresholds.bmp_temperature_limit);
                break;

            case 1U:
                snprintf(line1, sizeof(line1), "DHT TEMP ALARM");

                snprintf(line2, sizeof(line2),
                         "%.1f > %.1f C",
                         (double)data->temperature_dht,
                         (double)thresholds.dht_temperature_limit);
                break;

            case 2U:
                snprintf(line1, sizeof(line1), "HUMIDITY ALARM");

                snprintf(line2, sizeof(line2),
                         "%.1f > %.1f %%",
                         (double)data->humidity,
                         (double)thresholds.humidity_limit);
                break;

						case 3U:
								snprintf(line1, sizeof(line1), "MQ135 ALARM");

								snprintf(line2, sizeof(line2),
												 "%5.1f>%5.1fppm",
												 (double)data->mq135_ppm,
												 (double)thresholds.mq135_limit);
								break;

            case 4U:
                snprintf(line1, sizeof(line1), "PRESSURE ALARM");

                snprintf(line2, sizeof(line2),
                         "%.1f > %.1f",
                         (double)data->pressure,
                         (double)thresholds.pressure_limit);
                break;

            default:
                continue;
        }

        LCD_PrintLine(0U, line1);
        LCD_PrintLine(1U, line2);

        alarmPageIndex = index;
        return;
    }
}




static void DisplayTask(void *argument)
{
    SensorData_t sensorData = {0};

    uint8_t page = 0U;
    uint8_t displayOn = 1U;
		uint8_t buzzerExpired = 0U;
		uint8_t thresholdExpired = 0U;
    uint32_t lastActivity = osKernelGetTickCount();
    uint32_t lastPageChange = lastActivity;

    (void)argument;

    LCD_Init();
    LCD_Clear();

    for (;;)
    {
        uint32_t now = osKernelGetTickCount();

        uint8_t wakeEvent = 0U;
        uint8_t alarmSnapshot = 0U;
        uint8_t alarmRefreshEvent = 0U;
        uint8_t buzzerRequest = 0U;
        uint8_t buzzerMutedSnapshot = 0U;
        uint8_t thresholdRequest = 0U;
        uint8_t thresholdSelectedSnapshot = 0U;

        if (osMutexAcquire(displayStateMutex, osWaitForever) == osOK)
        {
            wakeEvent = wakeRequested;
            wakeRequested = 0U;

            alarmSnapshot = alarmMask;

            alarmRefreshEvent = alarmRefresh;
            alarmRefresh = 0U;

            buzzerRequest = buzzerStatusRequested;
            buzzerStatusRequested = 0U;

            buzzerMutedSnapshot = buzzerMutedDisplay;

            thresholdRequest = thresholdStatusRefresh;
            thresholdStatusRefresh = 0U;

            thresholdSelectedSnapshot = thresholdStatusSelected;

            (void)osMutexRelease(displayStateMutex);
        }
        else
        {
            osDelay(DISPLAY_PERIOD);
            continue;
        }


        if ((wakeEvent != 0U) || (alarmSnapshot != 0U))
        {
            if (displayOn == 0U)
            {
                LCD_DisplayOn();
                LCD_Clear();
                displayOn = 1U;
            }

            if (wakeEvent != 0U)
            {
                lastActivity = now;
                lastPageChange = now;
            }
        }

        if (alarmRefreshEvent != 0U)
        {
            alarmPageIndex = 0U;
        }


        if (buzzerRequest != 0U)
        {
            buzzerStatusActive = 1U;
            buzzerStatusUntil = now + BUZZER_STATUS_TIME;

            if (displayOn != 0U)
            {
                Display_ShowBuzzerStatus(buzzerMutedSnapshot);
            }
        }


        if (thresholdRequest != 0U)
        {
            thresholdStatusActive = 1U;
            thresholdStatusUntil = now + THRESHOLD_STATUS_TIME;

            if ((displayOn != 0U) && (buzzerStatusActive == 0U))
            {
                Display_ShowThreshold(thresholdSelectedSnapshot);
            }
        }


				if ((buzzerStatusActive != 0U) &&
						((int32_t)(now - buzzerStatusUntil) >= 0))
				{
						buzzerStatusActive = 0U;
						buzzerExpired = 1U;
				}


				if ((thresholdStatusActive != 0U) &&
						((int32_t)(now - thresholdStatusUntil) >= 0))
				{
						thresholdStatusActive = 0U;
						thresholdExpired = 1U;
				}

				if ((buzzerExpired != 0U) &&
						(thresholdStatusActive != 0U) &&
						(displayOn != 0U))
				{
						Display_ShowThreshold(thresholdSelectedSnapshot);
				}

        if ((displayOn != 0U) &&
            (buzzerStatusActive == 0U) &&
            (thresholdStatusActive == 0U))
        {
            if (alarmSnapshot != 0U)
            {
                now = osKernelGetTickCount();

                if ((alarmRefreshEvent != 0U) ||
										(buzzerExpired != 0U) ||
										(thresholdExpired != 0U) ||
										((now - lastAlarmChange) >= ALARM_PAGE_TIME))
                {
                    if (alarmRefreshEvent == 0U)
                    {
                        alarmPageIndex =
                            (uint8_t)((alarmPageIndex + 1U) % ALARM_COUNT);
                    }

                    Display_ShowAlarm(&sensorData,
                                      alarmSnapshot,
                                      alarmPageIndex);

                    lastAlarmChange = now;
                }
            }
        }


        if (AppData_ReceiveDisplayData(&sensorData, 0U) != 0U)
        {
            if ((displayOn != 0U) &&
                (buzzerStatusActive == 0U) &&
                (thresholdStatusActive == 0U))
            {
                if (alarmSnapshot != 0U)
                {
                    Display_ShowAlarm(&sensorData,
                                      alarmSnapshot,
                                      alarmPageIndex);
                }
                else
                {
                    Display_ShowPage(&sensorData, page);
                }
            }
        }

        now = osKernelGetTickCount();


        if ((displayOn != 0U) &&
            (alarmSnapshot == 0U) &&
            (buzzerStatusActive == 0U) &&
            (thresholdStatusActive == 0U) &&
            ((now - lastActivity) >= DISPLAY_TIMEOUT))
        {
            LCD_DisplayOff();
            displayOn = 0U;
        }


        if ((displayOn != 0U) &&
            (alarmSnapshot == 0U) &&
            (buzzerStatusActive == 0U) &&
            (thresholdStatusActive == 0U) &&
            ((now - lastPageChange) >= DISPLAY_PAGE_TIME))
        {
            page++;

            if (page >= DISPLAY_PAGE_COUNT)
            {
                page = 0U;
            }

            lastPageChange = now;
            Display_ShowPage(&sensorData, page);
        }

        osDelay(DISPLAY_PERIOD);
    }
}




void DisplayTask_SetAlarmMask(uint8_t mask)
{
    if (displayStateMutex == NULL)
    {
        return;
    }

    if (osMutexAcquire(displayStateMutex, osWaitForever) == osOK)
    {
        if (alarmMask != mask)
        {
            alarmMask = mask;
            alarmRefresh = 1U;
            wakeRequested = 1U;
        }

        (void)osMutexRelease(displayStateMutex);
    }
}

void DisplayTask_SetBuzzerStatus(uint8_t muted)
{
    if (displayStateMutex == NULL)
    {
        return;
    }

    if (osMutexAcquire(displayStateMutex, osWaitForever) == osOK)
    {
        buzzerMutedDisplay = (muted != 0U) ? 1U : 0U;
        buzzerStatusRequested = 1U;
        wakeRequested = 1U;

        (void)osMutexRelease(displayStateMutex);
    }
}

void DisplayTask_ShowThreshold(uint8_t selected)
{
    if ((selected >= 5U) || (displayStateMutex == NULL))
    {
        return;
    }

    if (osMutexAcquire(displayStateMutex, osWaitForever) == osOK)
    {
        thresholdStatusSelected = selected;
        thresholdStatusRefresh = 1U;
        wakeRequested = 1U;

        (void)osMutexRelease(displayStateMutex);
    }
}

void DisplayTask_Wake(void)
{
    if (displayStateMutex == NULL)
    {
        return;
    }

    if (osMutexAcquire(displayStateMutex, osWaitForever) == osOK)
    {
        wakeRequested = 1U;

        (void)osMutexRelease(displayStateMutex);
    }
}

void DisplayTask_Init(void)
{
    displayStateMutex = osMutexNew(NULL);

    if (displayStateMutex == NULL)
    {
        Error_Handler();
    }

    displayTaskHandle = osThreadNew(DisplayTask,
                                    NULL,
                                    &displayTaskAttributes);

    if (displayTaskHandle == NULL)
    {
        Error_Handler();
    }
}
