/**
 * @file tasks.h
 * @brief Core FreeRTOS Tasks and IPC Objects for the Smart Kitchen System
 */
#ifndef TASKS_H
#define TASKS_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define UART_TASK_PRIORITY       1
#define CONTROL_TASK_PRIORITY    2
#define OVERRIDE_TASK_PRIORITY   3

#define LOG_QUEUE_LENGTH         10
#define OVERRIDE_QUEUE_LENGTH    10
#define LOG_MESSAGE_SIZE         80

typedef enum {
    EVENT_TOGGLE_MODE = 0,
    EVENT_TOGGLE_LIGHT,
    EVENT_TOGGLE_OVEN
} ButtonEvent_t;

typedef enum {
    SYSTEM_MODE_AUTO = 0,
    SYSTEM_MODE_MANUAL
} SystemMode_t;

typedef struct {
    char text[LOG_MESSAGE_SIZE];
} LogMessage_t;

extern QueueHandle_t xLogQueue;
extern QueueHandle_t xOverrideQueue;
extern SemaphoreHandle_t xStateMutex;
extern SemaphoreHandle_t xUARTMutex;
extern SemaphoreHandle_t xOverrideSemaphore;

BaseType_t Tasks_Init(void);

void Tasks_UserOverride(void *pvParameters);
void Tasks_UARTLogging(void *pvParameters);
void Tasks_LightingControl(void *pvParameters);
void Tasks_OvenControl(void *pvParameters);

BaseType_t Tasks_LogSend(const char *message, TickType_t timeout);
void Tasks_GetSystemState(SystemMode_t *mode, bool *lightOn, bool *ovenOn);
void Tasks_PostButtonEventFromISR(ButtonEvent_t event);

#endif // TASKS_H
