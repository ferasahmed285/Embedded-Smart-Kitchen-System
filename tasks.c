/**
 * @file tasks.c
 * @brief FreeRTOS tasks for system control, logging, and overrides.
 * @note Role 1, Role 2, and Role 3 responsibilities.
 */
#include "tasks.h"
#include "uart.h"
#include "adc.h"
#include "led.h"
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
static SystemMode_t globalSystemMode = SYSTEM_MODE_AUTO;
static bool manualLightOn = false;
static bool manualOvenOn = false;

/* =========================================================
 * INITIALIZATION
 * ========================================================= */
BaseType_t Tasks_Init(void)
{
    xLogQueue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(LogMessage_t));
    xOverrideQueue = xQueueCreate(OVERRIDE_QUEUE_LENGTH, sizeof(ButtonEvent_t));
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
    ButtonEvent_t event;
    (void)pvParameters;

    for (;;)
    {
        if (xSemaphoreTake(xOverrideSemaphore, portMAX_DELAY) == pdTRUE)
        {
            if (xQueueReceive(xOverrideQueue, &event, 0) == pdPASS)
            {
                if (xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE)
                {
                    if (event == EVENT_TOGGLE_MODE) {
                        globalSystemMode = (globalSystemMode == SYSTEM_MODE_AUTO) ? SYSTEM_MODE_MANUAL : SYSTEM_MODE_AUTO;
                        Tasks_LogSend((globalSystemMode == SYSTEM_MODE_AUTO) ? "SYSTEM: AUTO MODE" : "SYSTEM: MANUAL MODE", 0);
                    }
                    else if (event == EVENT_TOGGLE_LIGHT) {
                        if (globalSystemMode == SYSTEM_MODE_MANUAL) {
                            manualLightOn = !manualLightOn;
                            Tasks_LogSend(manualLightOn ? "MANUAL LIGHT: ON" : "MANUAL LIGHT: OFF", 0);
                        } else {
                            Tasks_LogSend("REJECTED: CANNOT TOGGLE LIGHT IN AUTO MODE", 0);
                        }
                    }
                    else if (event == EVENT_TOGGLE_OVEN) {
                        if (globalSystemMode == SYSTEM_MODE_MANUAL) {
                            manualOvenOn = !manualOvenOn;
                            Tasks_LogSend(manualOvenOn ? "MANUAL OVEN: ON" : "MANUAL OVEN: OFF", 0);
                        } else {
                            Tasks_LogSend("REJECTED: CANNOT TOGGLE OVEN IN AUTO MODE", 0);
                        }
                    }
                    xSemaphoreGive(xStateMutex);
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
    bool lightIsOn = false;
    const uint32_t LIGHT_THRESHOLD = 2000; 
    
    for(;;) {
        uint32_t lightLevel = ADC_ReadLightSensor();
        SystemMode_t mode;
        bool lightManual;
        bool ovenManual;
        Tasks_GetSystemState(&mode, &lightManual, &ovenManual);
        
        bool turnOn = false;

        if (mode == SYSTEM_MODE_MANUAL) {
            turnOn = lightManual;
        } else {
            /* AUTO mode: turn on if dark */
            if (lightLevel > LIGHT_THRESHOLD) {
                turnOn = true;
            } else {
                turnOn = false;
            }
        }

        /* State change logging */
        if (turnOn != lightIsOn) {
            lightIsOn = turnOn;
            if (turnOn) Tasks_LogSend("KITCHEN LIGHT: ON", 0);
            else        Tasks_LogSend("KITCHEN LIGHT: OFF", 0);
        }

        LED_SetKitchenLight(lightIsOn);
        vTaskDelay(pdMS_TO_TICKS(500)); 
    }
}

/* =========================================================
 * TASK: OVEN CONTROL (Task 2)
 * ========================================================= */
void Tasks_OvenControl(void *pvParameters) {
    (void)pvParameters;
    bool ovenIsOn = false;
    const float OVEN_TARGET_TEMP = 200.0f; 
    const float FAULT_TEMP_MAX = 300.0f; 
    const float FAULT_TEMP_MIN = -20.0f; 
    
    for(;;) {
        float currentTemp = ADC_ReadTemperatureSensor();
        SystemMode_t mode;
        bool lightManual;
        bool ovenManual;
        Tasks_GetSystemState(&mode, &lightManual, &ovenManual);
        
        bool turnOn = false;

        /* Role 4 Hardware Fault Detection */
        if (currentTemp > FAULT_TEMP_MAX || currentTemp < FAULT_TEMP_MIN) {
            Tasks_LogSend("FAULT: OVEN TEMP SENSOR INVALID. OVEN FORCED OFF.", 0);
            turnOn = false; 
        } else {
            if (mode == SYSTEM_MODE_MANUAL) {
                turnOn = ovenManual;
            } else {
                /* AUTO mode */
                if (currentTemp < OVEN_TARGET_TEMP) {
                    turnOn = true;
                } else {
                    turnOn = false;
                }
            }
        }

        /* State change logging */
        if (turnOn != ovenIsOn) {
            ovenIsOn = turnOn;
            if (turnOn) Tasks_LogSend("OVEN ELEMENT: ON", 0);
            else        Tasks_LogSend("OVEN ELEMENT: OFF", 0);
        }

        LED_SetOvenElement(ovenIsOn);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* =========================================================
 * GETTERS & SETTERS
 * ========================================================= */
void Tasks_GetSystemState(SystemMode_t *mode, bool *lightOn, bool *ovenOn)
{
    if (xStateMutex != NULL && xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE)
    {
        *mode = globalSystemMode;
        *lightOn = manualLightOn;
        *ovenOn = manualOvenOn;
        xSemaphoreGive(xStateMutex);
    }
}

/* =========================================================
 * POST EVENT FROM ISR
 * ========================================================= */
void Tasks_PostButtonEventFromISR(ButtonEvent_t event)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if ((xOverrideQueue == NULL) || (xOverrideSemaphore == NULL))
        return;

    if (xQueueSendFromISR(xOverrideQueue, &event, &xHigherPriorityTaskWoken) == pdPASS)
    {
        xSemaphoreGiveFromISR(xOverrideSemaphore, &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
