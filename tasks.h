#ifndef TASKS_H
#define TASKS_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* =========================================================
 * TASK PRIORITIES
 * ========================================================= */
#define UART_TASK_PRIORITY       1
#define CONTROL_TASK_PRIORITY    2
#define OVERRIDE_TASK_PRIORITY   3

/* =========================================================
 * QUEUE CONFIGURATION
 * ========================================================= */
#define LOG_QUEUE_LENGTH         10
#define OVERRIDE_QUEUE_LENGTH    10
#define LOG_MESSAGE_SIZE         80

/* =========================================================
 * OVERRIDE ENUMS & STRUCTS
 * ========================================================= */
typedef enum {
    OVERRIDE_AUTO = 0,
    OVERRIDE_FORCE_OFF,
    OVERRIDE_FORCE_ON
} OverrideMode_t;

typedef enum {
    OVERRIDE_LIGHT = 0,
    OVERRIDE_OVEN
} OverrideSource_t;

typedef struct {
    OverrideSource_t source;
    OverrideMode_t mode;
} OverrideEvent_t;

typedef struct {
    char text[LOG_MESSAGE_SIZE];
} LogMessage_t;

/* =========================================================
 * RTOS OBJECTS
 * ========================================================= */
extern QueueHandle_t xLogQueue;
extern QueueHandle_t xOverrideQueue;
extern SemaphoreHandle_t xStateMutex;
extern SemaphoreHandle_t xUARTMutex;
extern SemaphoreHandle_t xOverrideSemaphore;

/* =========================================================
 * INITIALIZATION
 * ========================================================= */
BaseType_t Tasks_Init(void);

/* =========================================================
 * TASKS
 * ========================================================= */
void Tasks_UserOverride(void *pvParameters);
void Tasks_UARTLogging(void *pvParameters);
void Tasks_LightingControl(void *pvParameters);
void Tasks_OvenControl(void *pvParameters);

/* =========================================================
 * PUBLIC APIs
 * ========================================================= */
BaseType_t Tasks_LogSend(const char *message, TickType_t timeout);
OverrideMode_t Tasks_GetLightOverride(void);
OverrideMode_t Tasks_GetOvenOverride(void);
void Tasks_PostOverrideFromISR(OverrideSource_t source, OverrideMode_t mode);

#endif // TASKS_H
