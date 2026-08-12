/**
 * @file tasks.c
 * @brief FreeRTOS tasks for system control, logging, and overrides.
 * @note Role 1, Role 2, and Role 3 responsibilities.
 */
#include "tasks.h"
#include "uart.h"
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
 * ========================================================= */
static OverrideMode_t lightOverrideMode = OVERRIDE_AUTO;
static OverrideMode_t ovenOverrideMode = OVERRIDE_AUTO;

/* =========================================================
 * INITIALIZATION
 * ========================================================= */
BaseType_t Tasks_Init(void)
{
    xLogQueue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(LogMessage_t));
    xOverrideQueue = xQueueCreate(OVERRIDE_QUEUE_LENGTH, sizeof(OverrideEvent_t));
    xStateMutex = xSemaphoreCreateMutex();
    xUARTMutex = xSemaphoreCreateMutex();
    xOverrideSemaphore = xSemaphoreCreateCounting(OVERRIDE_QUEUE_LENGTH, 0);

    if ((xLogQueue == NULL) || (xOverrideQueue == NULL) || (xStateMutex == NULL) ||
        (xUARTMutex == NULL) || (xOverrideSemaphore == NULL))
    {
        return pdFAIL;
    }

    if (xTaskCreate(Tasks_UserOverride, "Override", configMINIMAL_STACK_SIZE * 2, NULL, OVERRIDE_TASK_PRIORITY, NULL) != pdPASS)
        return pdFAIL;

    if (xTaskCreate(Tasks_UARTLogging, "UART", configMINIMAL_STACK_SIZE * 2, NULL, UART_TASK_PRIORITY, NULL) != pdPASS)
        return pdFAIL;
        
    if (xTaskCreate(Tasks_LightingControl, "LightCtrl", configMINIMAL_STACK_SIZE * 2, NULL, CONTROL_TASK_PRIORITY, NULL) != pdPASS)
        return pdFAIL;
        
    if (xTaskCreate(Tasks_OvenControl, "OvenCtrl", configMINIMAL_STACK_SIZE * 2, NULL, CONTROL_TASK_PRIORITY, NULL) != pdPASS)
        return pdFAIL;

    return pdPASS;
}

/* =========================================================
 * LOG SEND
 * ========================================================= */
BaseType_t Tasks_LogSend(const char *message, TickType_t timeout)
{
    LogMessage_t logMessage;
    if (xLogQueue == NULL) return pdFAIL;

    memset(&logMessage, 0, sizeof(logMessage));
    strncpy(logMessage.text, message, LOG_MESSAGE_SIZE - 1);
    logMessage.text[LOG_MESSAGE_SIZE - 1] = '\0';

    return xQueueSend(xLogQueue, &logMessage, timeout);
}

/* =========================================================
 * TASK: USER OVERRIDE (Task 3)
 * ========================================================= */
void Tasks_UserOverride(void *pvParameters)
{
    OverrideEvent_t event;
    (void)pvParameters;

    for (;;)
    {
        if (xSemaphoreTake(xOverrideSemaphore, portMAX_DELAY) == pdTRUE)
        {
            if (xQueueReceive(xOverrideQueue, &event, 0) == pdPASS)
            {
                if (xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE)
                {
                    if (event.source == OVERRIDE_LIGHT)
                        lightOverrideMode = event.mode;
                    else if (event.source == OVERRIDE_OVEN)
                        ovenOverrideMode = event.mode;
                        
                    xSemaphoreGive(xStateMutex);
                }

                if (event.source == OVERRIDE_LIGHT)
                {
                    if (event.mode == OVERRIDE_AUTO) Tasks_LogSend("LIGHT OVERRIDE: AUTO", 0);
                    else if (event.mode == OVERRIDE_FORCE_ON) Tasks_LogSend("LIGHT OVERRIDE: FORCE ON", 0);
                    else if (event.mode == OVERRIDE_FORCE_OFF) Tasks_LogSend("LIGHT OVERRIDE: FORCE OFF", 0);
                }
                else if (event.source == OVERRIDE_OVEN)
                {
                    if (event.mode == OVERRIDE_AUTO) Tasks_LogSend("OVEN OVERRIDE: AUTO", 0);
                    else if (event.mode == OVERRIDE_FORCE_ON) Tasks_LogSend("OVEN OVERRIDE: FORCE ON", 0);
                    else if (event.mode == OVERRIDE_FORCE_OFF) Tasks_LogSend("OVEN OVERRIDE: FORCE OFF", 0);
                }
            }
        }
    }
}

/* =========================================================
 * TASK: UART LOGGING (Task 4)
 * ========================================================= */
void Tasks_UARTLogging(void *pvParameters)
{
    LogMessage_t receivedMessage;
    (void)pvParameters;

    for (;;)
    {
        if (xQueueReceive(xLogQueue, &receivedMessage, portMAX_DELAY) == pdPASS)
        {
            if (xSemaphoreTake(xUARTMutex, portMAX_DELAY) == pdTRUE)
            {
                UART_SendString(receivedMessage.text);
                UART_SendString("\r\n");
                xSemaphoreGive(xUARTMutex);
            }
        }
    }
}

/* =========================================================
 * TASK: LIGHTING CONTROL (Task 1)
 * ========================================================= */
void Tasks_LightingControl(void *pvParameters) {
    (void)pvParameters;
    for(;;) {
        // Lighting logic
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* =========================================================
 * TASK: OVEN CONTROL (Task 2)
 * ========================================================= */
void Tasks_OvenControl(void *pvParameters) {
    (void)pvParameters;
    for(;;) {
        // Oven logic
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* =========================================================
 * GETTERS
 * ========================================================= */
OverrideMode_t Tasks_GetLightOverride(void)
{
    OverrideMode_t mode = OVERRIDE_AUTO;
    if (xStateMutex == NULL) return OVERRIDE_AUTO;

    if (xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE)
    {
        mode = lightOverrideMode;
        xSemaphoreGive(xStateMutex);
    }
    return mode;
}

OverrideMode_t Tasks_GetOvenOverride(void)
{
    OverrideMode_t mode = OVERRIDE_AUTO;
    if (xStateMutex == NULL) return OVERRIDE_AUTO;

    if (xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE)
    {
        mode = ovenOverrideMode;
        xSemaphoreGive(xStateMutex);
    }
    return mode;
}

/* =========================================================
 * POST OVERRIDE FROM ISR
 * ========================================================= */
void Tasks_PostOverrideFromISR(OverrideSource_t source, OverrideMode_t mode)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    OverrideEvent_t event;

    if ((xOverrideQueue == NULL) || (xOverrideSemaphore == NULL))
        return;

    event.source = source;
    event.mode = mode;

    if (xQueueSendFromISR(xOverrideQueue, &event, &xHigherPriorityTaskWoken) == pdPASS)
    {
        xSemaphoreGiveFromISR(xOverrideSemaphore, &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
