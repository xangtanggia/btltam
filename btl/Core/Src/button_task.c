#include "button_task.h"
#include "app_data.h"
#include "display_task.h"
#include "cmsis_os.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>

#define BUTTON_STACK_SIZE       1024U
#define BUTTON_SCAN_PERIOD      20U
#define DEBOUNCE_COUNT          2U

#define SETTING_TIMEOUT         10000U
#define SETTING_SAVE_DELAY      3000U

#define BUTTON_COUNT            5U

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t debounceCount;
    uint8_t pressed;
    uint8_t event;
} Button_t;

static osThreadId_t buttonTaskHandle;

static Button_t buttons[BUTTON_COUNT] =
{
    {GPIOB, GPIO_PIN_12, 0U, 0U, 0U},
    {GPIOB, GPIO_PIN_13, 0U, 0U, 0U},
    {GPIOB, GPIO_PIN_15, 0U, 0U, 0U},
    {GPIOA, GPIO_PIN_1,  0U, 0U, 0U},
    {GPIOA, GPIO_PIN_2,  0U, 0U, 0U}
};

static uint8_t selectedThreshold = 0U;
static uint8_t settingMode = 0U;
static uint8_t thresholdDirty = 0U;

static uint32_t lastSettingTime = 0U;
static uint32_t thresholdChangeTime = 0U;

static const ThresholdType_t thresholdTypes[5] =
{
    THRESHOLD_BMP_TEMP,
    THRESHOLD_DHT_TEMP,
    THRESHOLD_HUMIDITY,
    THRESHOLD_MQ135,
    THRESHOLD_PRESSURE
};

static void Button_ReadPressed(void)
{
    uint8_t i;
    GPIO_PinState pinState;

    for (i = 0U; i < BUTTON_COUNT; i++)
    {
        pinState = HAL_GPIO_ReadPin(buttons[i].port,
                                    buttons[i].pin);

        if (pinState == GPIO_PIN_RESET)
        {
            if (buttons[i].debounceCount < DEBOUNCE_COUNT)
            {
                buttons[i].debounceCount++;
            }

            if ((buttons[i].debounceCount >= DEBOUNCE_COUNT) &&
                (buttons[i].pressed == 0U))
            {
                buttons[i].pressed = 1U;
                buttons[i].event = 1U;
            }
        }
        else
        {
            buttons[i].debounceCount = 0U;
            buttons[i].pressed = 0U;
        }
    }
}

static void Button_AdjustThreshold(int8_t direction)
{
    int16_t delta;

    if (settingMode == 0U)
    {
        return;
    }


    if (selectedThreshold == 3U)
    {
        delta = (int16_t)(direction * 50);
    }
    else
    {
        delta = (int16_t)direction;
    }

    if (AppData_AdjustThreshold(thresholdTypes[selectedThreshold],
                                delta) != 0U)
    {
        thresholdDirty = 1U;
        thresholdChangeTime = HAL_GetTick();

        lastSettingTime = HAL_GetTick();

        DisplayTask_Wake();
        DisplayTask_ShowThreshold(selectedThreshold);
    }
}

static void Button_CheckAutoSave(void)
{
    uint32_t currentTime = HAL_GetTick();

    if (thresholdDirty == 0U)
    {
        return;
    }

    if ((currentTime - thresholdChangeTime) >= SETTING_SAVE_DELAY)
    {
        if (AppData_SaveSettings() != 0U)
        {
            thresholdDirty = 0U;
        }
        else
        {
            thresholdChangeTime = currentTime;
        }
    }
}

static void ButtonTask(void *argument)
{
    (void)argument;

    for (;;)
    {
        Button_ReadPressed();

        
        if (buttons[0].event != 0U)
        {
            uint8_t muted;

            buttons[0].event = 0U;

            if (AppData_ToggleBuzzerMutedAndSave(&muted) != 0U)
            {
                DisplayTask_Wake();
                DisplayTask_SetBuzzerStatus(muted);
            }
        }


        if (buttons[1].event != 0U)
        {
            buttons[1].event = 0U;

            DisplayTask_Wake();
        }


        if (buttons[2].event != 0U)
        {
            buttons[2].event = 0U;

            settingMode = 1U;
            selectedThreshold++;

            if (selectedThreshold >= 5U)
            {
                selectedThreshold = 0U;
            }

            lastSettingTime = HAL_GetTick();

            DisplayTask_Wake();
            DisplayTask_ShowThreshold(selectedThreshold);
        }


        if (buttons[3].event != 0U)
        {
            buttons[3].event = 0U;

            Button_AdjustThreshold(1);
        }


        if (buttons[4].event != 0U)
        {
            buttons[4].event = 0U;

            Button_AdjustThreshold(-1);
        }


        if ((settingMode != 0U) &&
            ((HAL_GetTick() - lastSettingTime) >= SETTING_TIMEOUT))
        {
            settingMode = 0U;
        }

        Button_CheckAutoSave();

        osDelay(BUTTON_SCAN_PERIOD);
    }
}

void ButtonTask_Init(void)
{
    const osThreadAttr_t buttonTask_attributes =
    {
        .name = "ButtonTask",
        .stack_size = BUTTON_STACK_SIZE,
        .priority = osPriorityNormal
    };

    buttonTaskHandle = osThreadNew(ButtonTask,
                                   NULL,
                                   &buttonTask_attributes);
}