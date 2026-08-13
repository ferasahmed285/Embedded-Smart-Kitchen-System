/**
 * @file main.c
 * @brief System entry point. Initializes hardware and starts the scheduler.
 * @note Role 4 hardware bring-up.
 *
 * Smart Kitchen System - CSE 323/411 Advanced Embedded Systems
 *
 * Task map (see tasks.c):
 *   Task 1  Lighting Control   priority 2   \ equal priorities, so these two
 *   Task 2  Oven Control       priority 2   / share the CPU round-robin
 *   Task 3  User Override      priority 3   highest, for low latency input
 *   Task 4  UART Logging       priority 1   lowest, never blocks control
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
    /*
     * UART first, so that any failure in the peripherals initialised after it
     * can still be reported to the PuTTY console.
     *
     * Note that LED_Init() and Switch_Init() both touch Port F. Each one uses
     * read-modify-write on its own pins only, so the order between them does
     * not matter, but neither may assign a whole Port F register outright.
     */
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

    /* Written directly: the logging task does not exist yet. */
    UART_SendString("\r\n\r\n=== Smart Kitchen System ===\r\n");
    UART_SendString("Hardware initialised.\r\n");

    if (Tasks_Init() != pdPASS)
    {
        /*
         * The usual cause is configTOTAL_HEAP_SIZE being too small for the
         * task stacks and queues, which is silent unless it is reported.
         */
        UART_SendString("FATAL: RTOS object creation failed.\r\n");
        UART_SendString("Check configTOTAL_HEAP_SIZE in FreeRTOSConfig.h.\r\n");

        while (1)
        {
        }
    }

    UART_SendString("Tasks created. Starting scheduler.\r\n");

    Tasks_LogSend("SYSTEM: SMART KITCHEN RTOS STARTED", 0);
    Tasks_LogSend("SYSTEM: AUTO MODE", 0);

    vTaskStartScheduler();

    /*
     * Only reached if the scheduler could not allocate the idle or timer task.
     */
    UART_SendString("FATAL: Scheduler exited.\r\n");

    while (1)
    {
    }
}

/* =========================================================
 * FREERTOS APPLICATION HOOKS
 *
 * These must live outside main(). The kernel calls them by name, so a missing
 * definition is a link error rather than a compile error.
 * ========================================================= */

/**
 * @brief Called if a task overflows its stack (configCHECK_FOR_STACK_OVERFLOW).
 *
 * The heating element is de-energised directly through the driver rather than
 * through the oven task, because at this point the RTOS can no longer be
 * trusted to schedule anything.
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;

    taskDISABLE_INTERRUPTS();

    LED_SetOvenElement(false);

    UART_SendString("\r\nFATAL: STACK OVERFLOW IN TASK: ");
    UART_SendString((pcTaskName != NULL) ? pcTaskName : "unknown");
    UART_SendString("\r\nOVEN ELEMENT FORCED OFF.\r\n");

    while (1)
    {
    }
}

/**
 * @brief Called when pvPortMalloc fails (configUSE_MALLOC_FAILED_HOOK).
 */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();

    LED_SetOvenElement(false);

    UART_SendString("\r\nFATAL: HEAP EXHAUSTED.\r\n");
    UART_SendString("Increase configTOTAL_HEAP_SIZE in FreeRTOSConfig.h.\r\n");

    while (1)
    {
    }
}
