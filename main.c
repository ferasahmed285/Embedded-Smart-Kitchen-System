#include "FreeRTOS.h"
#include "task.h"
#include "role3.h"


/* =========================================================
 * HARDWARE INITIALIZATION
 * ========================================================= */

static void Hardware_Init(void)
{
    /*
     * Hardware initialization will be added later.
     */
}


/* =========================================================
 * TEMPORARY UART FUNCTION
 * ========================================================= */

void Role3_UART_WriteString(const char *text)
{
    /*
     * Temporary function.
     * Real UART code will be added later.
     */

    (void)text;
}


/* =========================================================
 * MAIN
 * ========================================================= */

int main(void)
{
    Hardware_Init();


    if (Role3_Init() != pdPASS)
    {
        while (1)
        {
        }
    }


    Role3_LogSend(
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