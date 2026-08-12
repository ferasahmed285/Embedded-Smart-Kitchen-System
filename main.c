/**
 * @file main.c
 * @brief System entry point. Initializes hardware and FreeRTOS scheduler.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "tasks.h"
#include "adc.h"
#include "uart.h"
#include "led.h"
#include "switch.h"


/* =========================================================
 * HARDWARE INITIALIZATION
 * ========================================================= */

static void Hardware_Init(void)
{
    UART_Init();
    ADC_Init();
    LED_Init();
    Switch_Init();
}
/* =========================================================
 * MAIN
 * ========================================================= */

int main(void)
{
    Hardware_Init();


    if (Tasks_Init() != pdPASS)
    {
        while (1)
        {
        }
    }


    Tasks_LogSend(
        "Smart Kitchen RTOS Started",
        0
    );


    vTaskStartScheduler();


    /*
     * Should never reach here.
     */

    while (1)
    {
    }
}


/* =========================================================
 * FREERTOS STACK OVERFLOW HOOK
 *
 * IMPORTANT:
 * This must be OUTSIDE main().
 * ========================================================= */

void vApplicationStackOverflowHook(
    TaskHandle_t xTask,
    char *pcTaskName
)
{
    (void)xTask;
    (void)pcTaskName;

    taskDISABLE_INTERRUPTS();

    while (1)
    {
    }
}