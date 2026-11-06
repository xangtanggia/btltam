#ifndef UART_TASK_H
#define UART_TASK_H

#include "main.h"
#include "cmsis_os.h"
void UARTTask_Init(void);
extern osThreadId_t uartTaskHandle;
#endif