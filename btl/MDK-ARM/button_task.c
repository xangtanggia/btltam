#include "button_task.h"
#include "app_data.h"
#include "display_task.h"
#include "alarm_task.h"
#include "cmsis_os.h"
#include "setting.h"

#define BUTTON_TASK_STACK_SIZE 1024U
#define BUTTON_SCAN_PERIOD    20U
#define DEBOUNCE_COUNT        2U
#define SETTING_TIMEOUT 10000U
#define SETTING_SAVE_DELAY 3000U



static uint32_t settingLastActivity = 0;
static uint32_t thresholdChangeTime = 0;
static uint8_t thresholdDirty = 0;

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState stableState;
    uint8_t counter;
} Button_t;

static osThreadId_t buttonTaskHandle;

static Button_t buttons[5] =
{
    {GPIOB, GPIO_PIN_12, GPIO_PIN_SET, 0},
    {GPIOB, GPIO_PIN_13, GPIO_PIN_SET, 0},
    {GPIOB, GPIO_PIN_15, GPIO_PIN_SET, 0},
    {GPIOA, GPIO_PIN_1,  GPIO_PIN_SET, 0},
    {GPIOA, GPIO_PIN_2,  GPIO_PIN_SET, 0}
};

static uint8_t selectedThreshold = 0;
static uint8_t settingMode = 0;
static const osThreadAttr_t buttonTaskAttributes =
{
    .name = "ButtonTask",
    .stack_size = BUTTON_TASK_STACK_SIZE,
    .priority = osPriorityNormal
};

static uint8_t Button_ReadPressed(Button_t *button)
{
    GPIO_PinState currentState =
        HAL_GPIO_ReadPin(button->port, button->pin);

    if (currentState != button->stableState)
    {
        button->counter++;

        if (button->counter >= DEBOUNCE_COUNT)
        {
            button->stableState = currentState;
            button->counter = 0;

            if (button->stableState == GPIO_PIN_RESET)
                return 1;
        }
    }
    else
    {
        button->counter = 0;
    }

    return 0;
}

static void Button_AdjustThreshold(int8_t direction)
{
    AlarmThresholds_t thresholds;

    AppData_GetThresholds(&thresholds);
		AlarmThresholds_t oldThresholds = thresholds;
    switch (selectedThreshold)
    {
        case 0:
            thresholds.temperature_limit += direction;

            if (thresholds.temperature_limit < 0.0f)
                thresholds.temperature_limit = 0.0f;

            if (thresholds.temperature_limit > 70.0f)
                thresholds.temperature_limit = 70.0f;
            break;

        case 1:
            if (direction > 0)
            {
                if (thresholds.humidity_limit < 100.0f)
                    thresholds.humidity_limit += 1.0f;
            }
            else
            {
                if (thresholds.humidity_limit > 0.0f)
                    thresholds.humidity_limit -= 1.0f;
            }
            break;

        case 2:
            if (direction > 0)
            {
                if (thresholds.mq135_limit <= 4045U)
                    thresholds.mq135_limit += 50U;
                else
                    thresholds.mq135_limit = 4095U;
            }
            else
            {
                if (thresholds.mq135_limit >= 50U)
                    thresholds.mq135_limit -= 50U;
                else
                    thresholds.mq135_limit = 0U;
            }
            break;
				case 3:
						thresholds.pressure_limit += direction;

						if (thresholds.pressure_limit < 0.0f)
								thresholds.pressure_limit = 0.0f;

						if (thresholds.pressure_limit > 1200.0f)
								thresholds.pressure_limit = 1200.0f;
						break;
    }
		uint8_t changed = 0;

if (thresholds.temperature_limit != oldThresholds.temperature_limit ||
    thresholds.humidity_limit != oldThresholds.humidity_limit ||
    thresholds.mq135_limit != oldThresholds.mq135_limit ||
    thresholds.pressure_limit != oldThresholds.pressure_limit)
{
    changed = 1;
}
AppData_SetThresholds(&thresholds);

if (changed)
{
    thresholdDirty = 1;
    thresholdChangeTime = osKernelGetTickCount();
}
}
static void Button_CheckAutoSave(void)
{
    if (thresholdDirty &&
        (osKernelGetTickCount() - thresholdChangeTime >= SETTING_SAVE_DELAY))
    {
        AlarmThresholds_t thresholds;

        AppData_GetThresholds(&thresholds);
Settings_Save(&thresholds, AlarmTask_GetMute());

        thresholdDirty = 0;
    }
}
static void ButtonTask(void *argument)
{
    for (;;)
    {
        if (Button_ReadPressed(&buttons[0]))
{
    uint8_t muted = AlarmTask_ToggleMute();

    DisplayTask_Wake();
    DisplayTask_SetBuzzerStatus(muted);
}

        if (Button_ReadPressed(&buttons[1]))
        {
            DisplayTask_Wake();
        }

if (Button_ReadPressed(&buttons[2]))
{
    if (!settingMode)
    {
        settingMode = 1;
        selectedThreshold = 0;
    }
    else
    {
        selectedThreshold++;

        if (selectedThreshold >= 4)
            selectedThreshold = 0;
    }

    settingLastActivity = osKernelGetTickCount();
    DisplayTask_SetSetting(1, selectedThreshold);
}

if (Button_ReadPressed(&buttons[3]) && settingMode)
{
    Button_AdjustThreshold(1);
    settingLastActivity = osKernelGetTickCount();
    DisplayTask_SetSetting(1, selectedThreshold);
}

if (Button_ReadPressed(&buttons[4]) && settingMode)
{
    Button_AdjustThreshold(-1);
    settingLastActivity = osKernelGetTickCount();
    DisplayTask_SetSetting(1, selectedThreshold);
}
Button_CheckAutoSave();

if (settingMode &&
    (osKernelGetTickCount() - settingLastActivity >= SETTING_TIMEOUT))
{
    settingMode = 0;
    DisplayTask_SetSetting(0, selectedThreshold);
}
        osDelay(BUTTON_SCAN_PERIOD);
    }
}

void ButtonTask_Init(void)
{
    buttonTaskHandle = osThreadNew(ButtonTask,
                                   NULL,
                                   &buttonTaskAttributes);

    if (buttonTaskHandle == NULL)
    {
        Error_Handler();
    }
}