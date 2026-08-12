#include "role3.h"

#include <string.h>


/* =========================================================
 * RTOS OBJECTS
 * ========================================================= */

QueueHandle_t xLogQueue = NULL;

QueueHandle_t xOverrideQueue = NULL;


SemaphoreHandle_t xStateMutex = NULL;

SemaphoreHandle_t xUARTMutex = NULL;

SemaphoreHandle_t xOverrideSemaphore = NULL;


/* =========================================================
 * SHARED SYSTEM STATE
 * =========================================================
 *
 * These variables store the current manual override modes.
 *
 * They are protected by xStateMutex because multiple tasks
 * may access them.
 */

static OverrideMode_t lightOverrideMode = OVERRIDE_AUTO;

static OverrideMode_t ovenOverrideMode = OVERRIDE_AUTO;


/* =========================================================
 * ROLE 3 INITIALIZATION
 * ========================================================= */

BaseType_t Role3_Init(void)
{
    /*
     * -----------------------------------------------------
     * Create UART logging queue.
     *
     * Task 1, Task 2 and Task 3 can send messages here.
     * Task 4 receives them.
     * -----------------------------------------------------
     */

    xLogQueue = xQueueCreate(
        LOG_QUEUE_LENGTH,
        sizeof(LogMessage_t)
    );


    /*
     * -----------------------------------------------------
     * Create manual override event queue.
     *
     * GPIO interrupt handlers send override events here.
     * Task 3 receives them.
     * -----------------------------------------------------
     */

    xOverrideQueue = xQueueCreate(
        OVERRIDE_QUEUE_LENGTH,
        sizeof(OverrideEvent_t)
    );


    /*
     * -----------------------------------------------------
     * Mutex protecting shared lighting / oven state.
     * -----------------------------------------------------
     */

    xStateMutex = xSemaphoreCreateMutex();


    /*
     * -----------------------------------------------------
     * Mutex protecting UART.
     *
     * Prevents multiple parts of the application from
     * writing to UART simultaneously.
     * -----------------------------------------------------
     */

    xUARTMutex = xSemaphoreCreateMutex();


    /*
     * -----------------------------------------------------
     * Counting semaphore.
     *
     * Keeps track of the number of pending manual
     * override events.
     * -----------------------------------------------------
     */

    xOverrideSemaphore = xSemaphoreCreateCounting(
        OVERRIDE_QUEUE_LENGTH,
        0
    );


    /*
     * -----------------------------------------------------
     * Check that all RTOS objects were created.
     * -----------------------------------------------------
     */

    if ((xLogQueue == NULL) ||
        (xOverrideQueue == NULL) ||
        (xStateMutex == NULL) ||
        (xUARTMutex == NULL) ||
        (xOverrideSemaphore == NULL))
    {
        return pdFAIL;
    }


    /*
     * -----------------------------------------------------
     * Create Task 3
     *
     * User Override Task
     * HIGHEST priority.
     * -----------------------------------------------------
     */

    if (xTaskCreate(
            UserOverrideTask,
            "Override",
            configMINIMAL_STACK_SIZE * 2,
            NULL,
            OVERRIDE_TASK_PRIORITY,
            NULL
        ) != pdPASS)
    {
        return pdFAIL;
    }


    /*
     * -----------------------------------------------------
     * Create Task 4
     *
     * UART Logging Task
     * LOWER priority.
     * -----------------------------------------------------
     */

    if (xTaskCreate(
            UARTLoggingTask,
            "UART",
            configMINIMAL_STACK_SIZE * 2,
            NULL,
            UART_TASK_PRIORITY,
            NULL
        ) != pdPASS)
    {
        return pdFAIL;
    }


    return pdPASS;
}


/* =========================================================
 * SEND MESSAGE TO UART LOGGING QUEUE
 * ========================================================= */

BaseType_t Role3_LogSend(
    const char *message,
    TickType_t timeout
)
{
    LogMessage_t logMessage;


    /*
     * Make sure the queue exists.
     */

    if (xLogQueue == NULL)
    {
        return pdFAIL;
    }


    /*
     * Clear the message structure.
     */

    memset(
        &logMessage,
        0,
        sizeof(logMessage)
    );


    /*
     * Copy string safely.
     */

    strncpy(
        logMessage.text,
        message,
        LOG_MESSAGE_SIZE - 1
    );


    /*
     * Guarantee null termination.
     */

    logMessage.text[LOG_MESSAGE_SIZE - 1] = '\0';


    /*
     * Send message to Task 4.
     */

    return xQueueSend(
        xLogQueue,
        &logMessage,
        timeout
    );
}


/* =========================================================
 * TASK 3
 *
 * USER OVERRIDE TASK
 *
 * Highest Priority
 * ========================================================= */

void UserOverrideTask(void *pvParameters)
{
    OverrideEvent_t event;

    (void)pvParameters;


    for (;;)
    {
        /*
         * -------------------------------------------------
         * Wait until a switch interrupt signals that
         * an override event occurred.
         *
         * Task becomes BLOCKED here and therefore does not
         * consume CPU time.
         * -------------------------------------------------
         */

        if (xSemaphoreTake(
                xOverrideSemaphore,
                portMAX_DELAY
            ) == pdTRUE)
        {

            /*
             * ---------------------------------------------
             * Receive information about the switch event.
             * ---------------------------------------------
             */

            if (xQueueReceive(
                    xOverrideQueue,
                    &event,
                    0
                ) == pdPASS)
            {

                /*
                 * -----------------------------------------
                 * Lock shared state.
                 * -----------------------------------------
                 */

                if (xSemaphoreTake(
                        xStateMutex,
                        portMAX_DELAY
                    ) == pdTRUE)
                {

                    /*
                     * LIGHTING OVERRIDE
                     */

                    if (event.source == OVERRIDE_LIGHT)
                    {
                        lightOverrideMode = event.mode;
                    }


                    /*
                     * OVEN OVERRIDE
                     */

                    else if (event.source == OVERRIDE_OVEN)
                    {
                        ovenOverrideMode = event.mode;
                    }


                    /*
                     * Release shared state.
                     */

                    xSemaphoreGive(xStateMutex);
                }


                /*
                 * =========================================
                 * SEND STATUS TO UART LOGGING TASK
                 * =========================================
                 */


                /*
                 * LIGHT OVERRIDE
                 */

                if (event.source == OVERRIDE_LIGHT)
                {
                    if (event.mode == OVERRIDE_AUTO)
                    {
                        Role3_LogSend(
                            "LIGHT OVERRIDE: AUTO",
                            0
                        );
                    }

                    else if (event.mode == OVERRIDE_FORCE_ON)
                    {
                        Role3_LogSend(
                            "LIGHT OVERRIDE: FORCE ON",
                            0
                        );
                    }

                    else if (event.mode == OVERRIDE_FORCE_OFF)
                    {
                        Role3_LogSend(
                            "LIGHT OVERRIDE: FORCE OFF",
                            0
                        );
                    }
                }


                /*
                 * OVEN OVERRIDE
                 */

                else if (event.source == OVERRIDE_OVEN)
                {
                    if (event.mode == OVERRIDE_AUTO)
                    {
                        Role3_LogSend(
                            "OVEN OVERRIDE: AUTO",
                            0
                        );
                    }

                    else if (event.mode == OVERRIDE_FORCE_ON)
                    {
                        Role3_LogSend(
                            "OVEN OVERRIDE: FORCE ON",
                            0
                        );
                    }

                    else if (event.mode == OVERRIDE_FORCE_OFF)
                    {
                        Role3_LogSend(
                            "OVEN OVERRIDE: FORCE OFF",
                            0
                        );
                    }
                }
            }
        }
    }
}


/* =========================================================
 * TASK 4
 *
 * UART LOGGING TASK
 *
 * Lower Priority
 * ========================================================= */

void UARTLoggingTask(void *pvParameters)
{
    LogMessage_t receivedMessage;

    (void)pvParameters;


    for (;;)
    {
        /*
         * -------------------------------------------------
         * Wait indefinitely until another task sends a
         * message into xLogQueue.
         * -------------------------------------------------
         */

        if (xQueueReceive(
                xLogQueue,
                &receivedMessage,
                portMAX_DELAY
            ) == pdPASS)
        {

            /*
             * ---------------------------------------------
             * Obtain UART mutex before using UART hardware.
             * ---------------------------------------------
             */

            if (xSemaphoreTake(
                    xUARTMutex,
                    portMAX_DELAY
                ) == pdTRUE)
            {

                /*
                 * Send message to UART.
                 */

                Role3_UART_WriteString(
                    receivedMessage.text
                );


                /*
                 * New line for PuTTY.
                 */

                Role3_UART_WriteString(
                    "\r\n"
                );


                /*
                 * Release UART.
                 */

                xSemaphoreGive(xUARTMutex);
            }
        }
    }
}


/* =========================================================
 * GET LIGHT OVERRIDE STATE
 *
 * Called by Task 1.
 * ========================================================= */

OverrideMode_t Role3_GetLightOverride(void)
{
    OverrideMode_t mode = OVERRIDE_AUTO;


    if (xStateMutex == NULL)
    {
        return OVERRIDE_AUTO;
    }


    if (xSemaphoreTake(
            xStateMutex,
            portMAX_DELAY
        ) == pdTRUE)
    {
        mode = lightOverrideMode;

        xSemaphoreGive(xStateMutex);
    }


    return mode;
}


/* =========================================================
 * GET OVEN OVERRIDE STATE
 *
 * Called by Task 2.
 * ========================================================= */

OverrideMode_t Role3_GetOvenOverride(void)
{
    OverrideMode_t mode = OVERRIDE_AUTO;


    if (xStateMutex == NULL)
    {
        return OVERRIDE_AUTO;
    }


    if (xSemaphoreTake(
            xStateMutex,
            portMAX_DELAY
        ) == pdTRUE)
    {
        mode = ovenOverrideMode;

        xSemaphoreGive(xStateMutex);
    }


    return mode;
}


/* =========================================================
 * GPIO INTERRUPT -> TASK 3
 *
 * This function MUST only be called from an ISR.
 * ========================================================= */

void Role3_PostOverrideFromISR(
    OverrideSource_t source,
    OverrideMode_t mode
)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    OverrideEvent_t event;


    /*
     * Make sure objects have already been created.
     */

    if ((xOverrideQueue == NULL) ||
        (xOverrideSemaphore == NULL))
    {
        return;
    }


    /*
     * Store event information.
     */

    event.source = source;

    event.mode = mode;


    /*
     * -----------------------------------------------------
     * Send event into queue.
     *
     * IMPORTANT:
     * Use xQueueSendFromISR(), not normal xQueueSend().
     * -----------------------------------------------------
     */

    if (xQueueSendFromISR(
            xOverrideQueue,
            &event,
            &xHigherPriorityTaskWoken
        ) == pdPASS)
    {

        /*
         * -------------------------------------------------
         * Increment counting semaphore.
         *
         * This wakes Task 3.
         * -------------------------------------------------
         */

        xSemaphoreGiveFromISR(
            xOverrideSemaphore,
            &xHigherPriorityTaskWoken
        );
    }


    /*
     * -----------------------------------------------------
     * Because Task 3 has the highest priority, immediately
     * switch to it when necessary.
     * -----------------------------------------------------
     */

    portYIELD_FROM_ISR(
        xHigherPriorityTaskWoken
    );
}