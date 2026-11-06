#include "display_task.h"
#include "app_data.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c2;

#define LCD_ADDR          (0x27 << 1)
#define LCD_BACKLIGHT     0x08
#define LCD_ENABLE        0x04
#define LCD_RS            0x01

#define DISPLAY_STACK_SIZE 1024
#define DISPLAY_PERIOD     100U
#define DISPLAY_TIMEOUT    180000U
#define DISPLAY_PAGE_TIME  3000U
#define BUZZER_STATUS_TIME 2000U



#define ALARM_PAGE_TIME 2000U

static osThreadId_t displayTaskHandle;

static volatile uint8_t buzzerStatusRequested = 0;
static volatile uint8_t buzzerMutedDisplay = 0;
static uint8_t buzzerStatusActive = 0;
static uint32_t buzzerStatusUntil = 0;

static volatile uint8_t wakeRequested = 1;
static volatile uint8_t alarmMask = 0;
static volatile uint8_t alarmRefresh = 0;
static uint8_t alarmPageIndex = 0;
static uint32_t lastAlarmChange = 0;

static volatile uint8_t settingMode = 0;
static volatile uint8_t selectedThreshold = 0;
static volatile uint8_t settingRefresh = 0;

static uint8_t lcdBacklight = LCD_BACKLIGHT;

static const osThreadAttr_t displayTaskAttributes = {
    .name = "DisplayTask",
    .stack_size = DISPLAY_STACK_SIZE,
    .priority = osPriorityNormal
};

static void LCD_WriteByte(uint8_t data)
{
    HAL_I2C_Master_Transmit(&hi2c2, LCD_ADDR, &data, 1, 100);
}

static void LCD_PulseEnable(uint8_t data)
{
    LCD_WriteByte(data | LCD_ENABLE);
    HAL_Delay(1);
    LCD_WriteByte(data & ~LCD_ENABLE);
    HAL_Delay(1);
}

static void LCD_Write4Bits(uint8_t data)
{
    LCD_WriteByte(data | lcdBacklight);
    LCD_PulseEnable(data | lcdBacklight);
}

static void LCD_Send(uint8_t value, uint8_t mode)
{
    uint8_t high = (value & 0xF0) | mode;
    uint8_t low = ((value << 4) & 0xF0) | mode;

    LCD_Write4Bits(high);
    LCD_Write4Bits(low);
}

static void LCD_Command(uint8_t command)
{
    LCD_Send(command, 0);
}

static void LCD_Data(uint8_t data)
{
    LCD_Send(data, LCD_RS);
}

static void LCD_Init(void)
{
    HAL_Delay(50);

    LCD_Write4Bits(0x30);
    HAL_Delay(5);

    LCD_Write4Bits(0x30);
    HAL_Delay(1);

    LCD_Write4Bits(0x30);
    HAL_Delay(1);

    LCD_Write4Bits(0x20);
    HAL_Delay(1);

    LCD_Command(0x28);
    LCD_Command(0x0C);
    LCD_Command(0x06);
    LCD_Command(0x01);

    HAL_Delay(2);
}

static void LCD_Clear(void)
{
    LCD_Command(0x01);
    HAL_Delay(2);
}

static void LCD_SetCursor(uint8_t row, uint8_t col)
{
    uint8_t address = (row == 0) ? 0x80 : 0xC0;
    LCD_Command(address + col);
}

static void LCD_Print(char *str)
{
    while (*str)
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
    lcdBacklight = 0;
    LCD_WriteByte(0);
}

static void LCD_DisplayOff(void)
{
    LCD_Command(0x08);
    LCD_BacklightOff();
}

static void LCD_DisplayOn(void)
{
    LCD_BacklightOn();
    LCD_Command(0x0C);
}

static void LCD_PrintLine(uint8_t row, char *str)
{
    LCD_SetCursor(row, 0);

    uint8_t i = 0;

    while (i < 16 && str[i] != '\0')
    {
        LCD_Data((uint8_t)str[i]);
        i++;
    }

    while (i < 16)
    {
        LCD_Data(' ');
        i++;
    }
}

static void Display_ShowPage(const SensorData_t *data, uint8_t page)
{
    char line1[17];
    char line2[17];

    if (page == 0)
    {
        if (data->bmp280_ok)
            snprintf(line1, sizeof(line1), "BMP:%5.1f C", data->temperature_bmp);
        else
            snprintf(line1, sizeof(line1), "BMP: --.- C");

        if (data->dht11_ok)
            snprintf(line2, sizeof(line2), "DHT:%5.1f C", data->temperature_dht);
        else
            snprintf(line2, sizeof(line2), "DHT: --.- C");
    }
    else if (page == 1)
    {
        if (data->dht11_ok)
            snprintf(line1, sizeof(line1), "HUM:%5.1f %%", data->humidity);
        else
            snprintf(line1, sizeof(line1), "HUM: --.- %%");

        if (data->bmp280_ok)
            snprintf(line2, sizeof(line2), "P:%7.1f hPa", data->pressure);
        else
            snprintf(line2, sizeof(line2), "P: -------");
    }
    else
    {
        if (data->mq135_ok)
            snprintf(line1, sizeof(line1), "MQ135 ADC:%4u", data->mq135_adc);
        else
            snprintf(line1, sizeof(line1), "MQ135: ERROR");

        snprintf(line2, sizeof(line2), "ENV MONITOR");
    }

    LCD_PrintLine(0, line1);
    LCD_PrintLine(1, line2);
}
static void Display_ShowBuzzerStatus(uint8_t muted)
{
    LCD_Clear();
    LCD_SetCursor(0, 0);
    LCD_Print("BUZZER STATUS");

    LCD_SetCursor(1, 0);
    LCD_Print(muted ? "OFF" : "ON");
}
static void Display_ShowSetting(uint8_t selected)
{
    AlarmThresholds_t thresholds;
    char line1[17];
    char line2[17];

    AppData_GetThresholds(&thresholds);

    switch (selected)
    {
        case 0:
            snprintf(line1, sizeof(line1), "SET TEMP LIMIT");
            snprintf(line2, sizeof(line2), "VALUE:%5.1f C",
                     thresholds.temperature_limit);
            break;

        case 1:
            snprintf(line1, sizeof(line1), "SET HUM LIMIT");
            snprintf(line2, sizeof(line2), "VALUE:%5.1f %%",
                     thresholds.humidity_limit);
            break;

        case 2:
            snprintf(line1, sizeof(line1), "SET MQ135 LIMIT");
            snprintf(line2, sizeof(line2), "VALUE:%4u ADC",
                     thresholds.mq135_limit);
            break;
				case 3:
							snprintf(line1, sizeof(line1), "SET PRESS LIMIT");
							snprintf(line2, sizeof(line2), "VALUE:%7.1f", 
											 thresholds.pressure_limit);
							break;
        default:
            return;
    }

    LCD_PrintLine(0, line1);
    LCD_PrintLine(1, line2);
}
static void Display_ShowAlarm(const SensorData_t *data,
                              uint8_t mask,
                              uint8_t startIndex)
{
    AlarmThresholds_t thresholds;
    char line1[17];
    char line2[17];

    AppData_GetThresholds(&thresholds);

    for (uint8_t i = 0; i < 5; i++)
    {
        uint8_t index = (startIndex + i) % 5;

        if (mask & (1U << index))
        {
            switch (index)
            {
                case 0:
                    snprintf(line1, sizeof(line1), "BMP TEMP ALARM");
                    snprintf(line2, sizeof(line2), "%.1f>%.1f C",
                             data->temperature_bmp,
                             thresholds.temperature_limit);
                    break;

                case 1:
                    snprintf(line1, sizeof(line1), "DHT TEMP ALARM");
                    snprintf(line2, sizeof(line2), "%.1f>%.1f C",
                             data->temperature_dht,
                             thresholds.temperature_limit);
                    break;

                case 2:
                    snprintf(line1, sizeof(line1), "HUMIDITY ALARM");
                    snprintf(line2, sizeof(line2), "%.1f>%.1f %%",
                             data->humidity,
                             thresholds.humidity_limit);
                    break;

                case 3:
                    snprintf(line1, sizeof(line1), "MQ135 ALARM");
                    snprintf(line2, sizeof(line2), "%u>%u ADC",
                             data->mq135_adc,
                             thresholds.mq135_limit);
                    break;
										case 4:
    snprintf(line1, sizeof(line1), "PRESSURE ALARM");
    snprintf(line2, sizeof(line2), "%.1f>%.1f hPa",
             data->pressure,
             thresholds.pressure_limit);
    break;
            }

            LCD_PrintLine(0, line1);
            LCD_PrintLine(1, line2);
            alarmPageIndex = index;
            return;
        }
    }
}

static void DisplayTask(void *argument)
{
    SensorData_t sensorData = {0};

    uint8_t page = 0;
    uint8_t displayOn = 1;

    uint32_t lastActivity = osKernelGetTickCount();
    uint32_t lastPageChange = lastActivity;

    LCD_Init();
    LCD_Clear();

    for (;;)
    {
        uint32_t now = osKernelGetTickCount();

        if (wakeRequested || alarmMask)
        {
            if (!displayOn)
            {
                LCD_DisplayOn();
                displayOn = 1;
                LCD_Clear();
            }

            if (wakeRequested)
            {
                lastActivity = now;
                wakeRequested = 0;
            }
        }
						if (buzzerStatusRequested)
						{
								buzzerStatusRequested = 0;
								buzzerStatusActive = 1;
								buzzerStatusUntil =
										osKernelGetTickCount() + BUZZER_STATUS_TIME;

								Display_ShowBuzzerStatus(buzzerMutedDisplay);
						}

if (buzzerStatusActive &&
    (int32_t)(osKernelGetTickCount() - buzzerStatusUntil) >= 0)
{
    buzzerStatusActive = 0;

if (settingMode)
{
    Display_ShowSetting(selectedThreshold);
}
else if (alarmMask)
{
    Display_ShowAlarm(&sensorData, alarmMask, alarmPageIndex);
}
else
{
    Display_ShowPage(&sensorData, page);
}
}
if (alarmMask && displayOn && !settingMode && !buzzerStatusActive)
{
    now = osKernelGetTickCount();

    if (alarmRefresh ||
        (now - lastAlarmChange >= ALARM_PAGE_TIME))
    {
        if (!alarmRefresh)
            alarmPageIndex = (alarmPageIndex + 1U) % 5U;

        Display_ShowAlarm(&sensorData, alarmMask, alarmPageIndex);

        alarmRefresh = 0;
        lastAlarmChange = now;
    }
}
 if (osMessageQueueGet(displayQueueHandle,
                      &sensorData,
                      NULL,
                      0) == osOK)
{
    if (displayOn && !settingMode && !buzzerStatusActive)
    {
        if (alarmMask)
            Display_ShowAlarm(&sensorData, alarmMask, alarmPageIndex);
        else
            Display_ShowPage(&sensorData, page);
    }
}
        
if (displayOn && settingMode &&
    settingRefresh && !buzzerStatusActive)
{
    Display_ShowSetting(selectedThreshold);
    settingRefresh = 0;
}

        now = osKernelGetTickCount();

        if (displayOn && !alarmMask && !settingMode &&(now - lastActivity >= DISPLAY_TIMEOUT))
        {
            LCD_DisplayOff();
            displayOn = 0;
        }
if (displayOn && !alarmMask &&
    !settingMode && !buzzerStatusActive &&
    (now - lastPageChange >= DISPLAY_PAGE_TIME))
        {
            page++;
            if (page >= 3)
                page = 0;

            lastPageChange = now;
            Display_ShowPage(&sensorData, page);
        }

        osDelay(DISPLAY_PERIOD);
    }
}

void DisplayTask_SetSetting(uint8_t active, uint8_t selected)
{
    settingMode = active ? 1 : 0;
    selectedThreshold = selected;

    if (settingMode)
        settingRefresh = 1;

    wakeRequested = 1;
}

void DisplayTask_SetAlarmMask(uint8_t mask)
{
    if (alarmMask != mask)
    {
        alarmMask = mask;
        alarmPageIndex = 0;
        alarmRefresh = 1;
        wakeRequested = 1;
    }
}

void DisplayTask_SetBuzzerStatus(uint8_t muted)
{
    buzzerMutedDisplay = muted;
    buzzerStatusRequested = 1;
    wakeRequested = 1;
}
void DisplayTask_Init(void)
{
    displayTaskHandle = osThreadNew(DisplayTask,
                                    NULL,
                                    &displayTaskAttributes);

    if (displayTaskHandle == NULL)
        Error_Handler();
}
void DisplayTask_Wake(void)
{
    wakeRequested = 1;
}