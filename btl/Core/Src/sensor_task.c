#include "sensor_task.h"
#include "app_data.h"
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>

extern ADC_HandleTypeDef hadc1;
extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim3;
extern UART_HandleTypeDef huart1;

#define SENSOR_TASK_STACK_SIZE       (1024U * 4U)
#define SENSOR_TASK_PRIORITY         osPriorityAboveNormal
#define SENSOR_TASK_PERIOD_MS        2000U

#define BMP280_ADDR_1                (0x76U << 1)
#define BMP280_ADDR_2                (0x77U << 1)

#define BMP280_REG_ID                0xD0
#define BMP280_REG_RESET             0xE0
#define BMP280_REG_STATUS            0xF3
#define BMP280_REG_CTRL_MEAS         0xF4
#define BMP280_REG_DATA              0xF7
#define BMP280_REG_CALIB             0x88

#define DHT11_PORT                   GPIOB
#define DHT11_PIN                    GPIO_PIN_0
#define DHT11_OK                 0
#define DHT11_ERR_LINE_LOW       1
#define DHT11_ERR_NO_RESPONSE    2
#define DHT11_ERR_HANDSHAKE      3
#define DHT11_ERR_BIT_TIMEOUT    4
#define DHT11_ERR_CHECKSUM       5
#define DHT11_ERR_DATA           6

static uint8_t dht11_error = DHT11_OK;
static uint8_t dht11_error_bit = 0; //check
static uint8_t dht11_error_stage = 0;// check
#define MQ135_SAMPLE_COUNT           8U
#define MQ135_VC                     5.0f
#define MQ135_ADC_VREF               3.3f
#define MQ135_ADC_MAX                4095.0f

#define MQ135_R0_KOHM                7.3f
#define MQ135_RL_KOHM                1.0f

#define MQ135_DIVIDER_R1_KOHM        1.0f
#define MQ135_DIVIDER_R2_KOHM        1.0f

#define MQ135_PPM_COEFFICIENT        116.6020682f
#define MQ135_PPM_EXPONENT           (-2.769034857f)
typedef struct
{
    uint16_t T1;
    int16_t T2;
    int16_t T3;

    uint16_t P1;
    int16_t P2;
    int16_t P3;
    int16_t P4;
    int16_t P5;
    int16_t P6;
    int16_t P7;
    int16_t P8;
    int16_t P9;
} BMP280_Calib_t;

static BMP280_Calib_t bmpCalib;
static uint16_t bmpAddress;
static uint8_t bmpInitialized;

static osThreadId_t sensorTaskHandle;

static const osThreadAttr_t sensorTask_attributes =
{
    .name = "SensorTask",
    .stack_size = SENSOR_TASK_STACK_SIZE,
    .priority = SENSOR_TASK_PRIORITY
};



static uint16_t BMP280_U16(uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int16_t BMP280_S16(uint8_t *data)
{
    return (int16_t)BMP280_U16(data);
}

static uint8_t BMP280_Init(void)
{
    uint8_t id;
    uint8_t calib[24];

    bmpAddress = BMP280_ADDR_1;

    if (HAL_I2C_Mem_Read(&hi2c1, bmpAddress,
                         BMP280_REG_ID, I2C_MEMADD_SIZE_8BIT,
                         &id, 1, 100) != HAL_OK ||
        id != 0x58)
    {
        bmpAddress = BMP280_ADDR_2;

        if (HAL_I2C_Mem_Read(&hi2c1, bmpAddress,
                             BMP280_REG_ID, I2C_MEMADD_SIZE_8BIT,
                             &id, 1, 100) != HAL_OK ||
            id != 0x58)
        {
            return 0;
        }
    }

    if (HAL_I2C_Mem_Read(&hi2c1, bmpAddress,
                         BMP280_REG_CALIB, I2C_MEMADD_SIZE_8BIT,
                         calib, 24, 100) != HAL_OK)
    {
        return 0;
    }

    bmpCalib.T1 = BMP280_U16(&calib[0]);
    bmpCalib.T2 = BMP280_S16(&calib[2]);
    bmpCalib.T3 = BMP280_S16(&calib[4]);

    bmpCalib.P1 = BMP280_U16(&calib[6]);
    bmpCalib.P2 = BMP280_S16(&calib[8]);
    bmpCalib.P3 = BMP280_S16(&calib[10]);
    bmpCalib.P4 = BMP280_S16(&calib[12]);
    bmpCalib.P5 = BMP280_S16(&calib[14]);
    bmpCalib.P6 = BMP280_S16(&calib[16]);
    bmpCalib.P7 = BMP280_S16(&calib[18]);
    bmpCalib.P8 = BMP280_S16(&calib[20]);
    bmpCalib.P9 = BMP280_S16(&calib[22]);

    if (bmpCalib.P1 == 0)
    {
        return 0;
    }

    return 1;
}

static uint8_t BMP280_ReadData(float *temperature, float *pressure)
{
    uint8_t control = 0x25;
    uint8_t status;
    uint8_t raw[6];

    uint32_t timeout = HAL_GetTick();

    if (HAL_I2C_Mem_Write(&hi2c1, bmpAddress,
                          BMP280_REG_CTRL_MEAS,
                          I2C_MEMADD_SIZE_8BIT,
                          &control, 1, 100) != HAL_OK)
    {
        return 0;
    }

    do
    {
        if (HAL_I2C_Mem_Read(&hi2c1, bmpAddress,
                             BMP280_REG_STATUS,
                             I2C_MEMADD_SIZE_8BIT,
                             &status, 1, 100) != HAL_OK)
        {
            return 0;
        }

        if ((HAL_GetTick() - timeout) > 100U)
        {
            return 0;
        }

    } while (status & 0x08U);

    if (HAL_I2C_Mem_Read(&hi2c1, bmpAddress,
                         BMP280_REG_DATA,
                         I2C_MEMADD_SIZE_8BIT,
                         raw, 6, 100) != HAL_OK)
    {
        return 0;
    }

    int32_t adcP = ((int32_t)raw[0] << 12) |
                   ((int32_t)raw[1] << 4) |
                   ((int32_t)raw[2] >> 4);

    int32_t adcT = ((int32_t)raw[3] << 12) |
                   ((int32_t)raw[4] << 4) |
                   ((int32_t)raw[5] >> 4);

    double var1, var2, tFine;
    double temp, press;

    var1 = ((double)adcT / 16384.0 -
            (double)bmpCalib.T1 / 1024.0) *
            bmpCalib.T2;

    var2 = ((double)adcT / 131072.0 -
            (double)bmpCalib.T1 / 8192.0);

    var2 = var2 * var2 * bmpCalib.T3;

    tFine = var1 + var2;
    temp = tFine / 5120.0;

    var1 = tFine / 2.0 - 64000.0;

    var2 = var1 * var1 * bmpCalib.P6 / 32768.0;
    var2 = var2 + var1 * bmpCalib.P5 * 2.0;
    var2 = var2 / 4.0 + bmpCalib.P4 * 65536.0;

    var1 = (bmpCalib.P3 * var1 * var1 / 524288.0 +
            bmpCalib.P2 * var1) / 524288.0;

    var1 = (1.0 + var1 / 32768.0) * bmpCalib.P1;

    if (var1 == 0.0)
    {
        return 0;
    }

    press = 1048576.0 - adcP;
    press = (press - var2 / 4096.0) * 6250.0 / var1;

    var1 = bmpCalib.P9 * press * press / 2147483648.0;
    var2 = press * bmpCalib.P8 / 32768.0;

    press = press + (var1 + var2 + bmpCalib.P7) / 16.0;

    *temperature = (float)temp;
    *pressure = (float)(press / 100.0);

    return 1;
}



static void DHT11_DelayUs(uint16_t us)
{
    uint16_t start = __HAL_TIM_GET_COUNTER(&htim3);

    while ((uint16_t)(__HAL_TIM_GET_COUNTER(&htim3) - start) < us)
    {
    }
}

static void DHT11_SetOutput(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = DHT11_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(DHT11_PORT, &gpio);
}

static void DHT11_SetInput(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = DHT11_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;

    HAL_GPIO_Init(DHT11_PORT, &gpio);
}

static uint8_t DHT11_WaitLevel(GPIO_PinState level,
                              uint16_t timeout)
{
    uint16_t start = __HAL_TIM_GET_COUNTER(&htim3);

    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) != level)
    {
        if ((uint16_t)(__HAL_TIM_GET_COUNTER(&htim3) - start) >= timeout)
        {
            return 0;
        }
    }

    return 1;
}


static uint8_t DHT11_Read(float *temperature, float *humidity)
{
    uint8_t data[5] = {0};
    uint8_t success = 1;

    dht11_error = DHT11_OK;
dht11_error_bit = 0; //ch
dht11_error_stage = 0; //ch
    DHT11_SetOutput();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
    DHT11_DelayUs(10);

    if (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET)
    {
        dht11_error = DHT11_ERR_LINE_LOW;

        return 0;
    }

    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);

    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
    DHT11_DelayUs(30);

    DHT11_SetInput();

    if (!DHT11_WaitLevel(GPIO_PIN_RESET, 120))
    {
        dht11_error = DHT11_ERR_NO_RESPONSE;
        success = 0;
    }
    else if (!DHT11_WaitLevel(GPIO_PIN_SET, 120))
    {
        dht11_error = DHT11_ERR_HANDSHAKE;
        success = 0;
    }
    else if (!DHT11_WaitLevel(GPIO_PIN_RESET, 120))
    {
        dht11_error = DHT11_ERR_HANDSHAKE;
        success = 0;
    }

    if (success)
    {
        for (uint8_t i = 0; i < 40; i++)
        {
            if (!DHT11_WaitLevel(GPIO_PIN_SET, 150))
            {
							dht11_error_bit = i;
    dht11_error_stage = 1;
                dht11_error = DHT11_ERR_BIT_TIMEOUT;
                success = 0;
                break;
            }

            uint16_t start = __HAL_TIM_GET_COUNTER(&htim3);

            if (!DHT11_WaitLevel(GPIO_PIN_RESET, 100))
            {
							    dht11_error_bit = i;
    dht11_error_stage = 2;
                dht11_error = DHT11_ERR_BIT_TIMEOUT;
                success = 0;
                break;
            }

            uint16_t duration =
                (uint16_t)(__HAL_TIM_GET_COUNTER(&htim3) - start);

            data[i / 8] <<= 1;

            if (duration > 40)
            {
                data[i / 8] |= 1;
            }
        }
    }

    DHT11_SetOutput();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);

    if (!success){

        return 0;
    }

    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])
    {
        dht11_error = DHT11_ERR_CHECKSUM;

        return 0;
    }

    if (data[1] > 9 || data[3] > 9)
    {
        dht11_error = DHT11_ERR_DATA;

        return 0;
    }

    *humidity = data[0] + data[1] * 0.1f;

    if (data[2] & 0x80U)
    {
        *temperature = -(float)(data[2] & 0x7FU) - data[3] * 0.1f;
    }
    else
    {
        *temperature = data[2] + data[3] * 0.1f;
    }

    dht11_error = DHT11_OK;

    return 1;
}







static uint8_t MQ135_ReadADC(uint16_t *value)
{
    uint32_t sum = 0;

    for (uint8_t i = 0; i < MQ135_SAMPLE_COUNT; i++)
    {
        if (HAL_ADC_Start(&hadc1) != HAL_OK)
        {
            return 0;
        }

        if (HAL_ADC_PollForConversion(&hadc1, 20) != HAL_OK)
        {
            HAL_ADC_Stop(&hadc1);
            return 0;
        }

        sum += HAL_ADC_GetValue(&hadc1);

        HAL_ADC_Stop(&hadc1);
    }

    *value = (uint16_t)(sum / MQ135_SAMPLE_COUNT);

    return 1;
}

static uint8_t MQ135_ADCToPPM(uint16_t adc, float *ppm)
{
    float voltage_adc;
    float voltage_sensor;
    float rl_effective;
    float rs;
    float ratio;

    if ((ppm == NULL) || (adc == 0U) || (adc >= 4095U))
    {
        return 0;
    }


    voltage_adc = ((float)adc * MQ135_ADC_VREF) /
                  MQ135_ADC_MAX;


    voltage_sensor = voltage_adc *
                     (MQ135_DIVIDER_R1_KOHM +
                      MQ135_DIVIDER_R2_KOHM) /
                     MQ135_DIVIDER_R2_KOHM;

    if ((voltage_sensor <= 0.01f) ||
        (voltage_sensor >= MQ135_VC))
    {
        return 0;
    }


    rl_effective =
        (MQ135_RL_KOHM *
         (MQ135_DIVIDER_R1_KOHM + MQ135_DIVIDER_R2_KOHM)) /
        (MQ135_RL_KOHM +
         MQ135_DIVIDER_R1_KOHM +
         MQ135_DIVIDER_R2_KOHM);


    rs = rl_effective *
         (MQ135_VC / voltage_sensor - 1.0f);

    if (rs <= 0.0f)
    {
        return 0;
    }


    ratio = rs / MQ135_R0_KOHM;


    *ppm = MQ135_PPM_COEFFICIENT *
           powf(ratio, MQ135_PPM_EXPONENT);

    if (!isfinite(*ppm) || (*ppm <= 0.0f))
    {
        return 0;
    }

    return 1;
}




static void SensorTask(void *argument)
{
    SensorData_t sensorData = {0};

    for (;;)
    {
        if (!bmpInitialized)
        {
            bmpInitialized = BMP280_Init();
        }

        if (bmpInitialized)
        {
            sensorData.bmp280_ok =
                BMP280_ReadData(
                    &sensorData.temperature_bmp,
                    &sensorData.pressure
                );
        }
        else
        {
            sensorData.bmp280_ok = 0;
        }

        if (!sensorData.bmp280_ok)
        {
            sensorData.temperature_bmp = 0;
            sensorData.pressure = 0;
        }

        sensorData.dht11_ok =
            DHT11_Read(
                &sensorData.temperature_dht,
                &sensorData.humidity
            );

        if (!sensorData.dht11_ok)
        {
          //  sensorData.temperature_dht = 0;
            //sensorData.humidity = 0;
					char msg[80];

snprintf(msg, sizeof(msg),
         "DHT ERR=%d BIT=%d STAGE=%d\r\n",
         dht11_error,
         dht11_error_bit,
         dht11_error_stage);


    HAL_UART_Transmit(&huart1,
                      (uint8_t *)msg,
                      strlen(msg),
                      100);
        }

        uint16_t mq135_adc = 0;

				sensorData.mq135_ok = MQ135_ReadADC(&mq135_adc);

				if (sensorData.mq135_ok)
				{
						sensorData.mq135_ok =
								MQ135_ADCToPPM(mq135_adc, &sensorData.mq135_ppm);
				}

				if (!sensorData.mq135_ok)
				{
						sensorData.mq135_ppm = 0.0f;
				}

        AppData_PublishSensorData(&sensorData);

        osDelay(SENSOR_TASK_PERIOD_MS);
    }
}

void SensorTask_Init(void)
{
    sensorTaskHandle = osThreadNew(
        SensorTask,
        NULL,
        &sensorTask_attributes
    );

    if (sensorTaskHandle == NULL)
    {
        Error_Handler();
    }
}