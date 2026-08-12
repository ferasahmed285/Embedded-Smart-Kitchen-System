#ifndef ROLE3_H
#define ROLE3_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"


/* =========================================================
 * TASK PRIORITIES
 * =========================================================
 *
 * Task 3 - User Override : Highest Priority
 * Task 1 - Lighting      : Normal Priority
 * Task 2 - Oven          : Normal Priority
 * Task 4 - UART Logging  : Lower Priority
 */

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
 * OVERRIDE MODES
 * ========================================================= */

typedef enum
{
    OVERRIDE_AUTO = 0,

    OVERRIDE_FORCE_OFF,

    OVERRIDE_FORCE_ON

} OverrideMode_t;


/* =========================================================
 * OVERRIDE SOURCES
 * ========================================================= */

typedef enum
{
    OVERRIDE_LIGHT = 0,

    OVERRIDE_OVEN

} OverrideSource_t;


/* =========================================================
 * OVERRIDE EVENT
 * ========================================================= */

typedef struct
{
    OverrideSource_t source;

    OverrideMode_t mode;

} OverrideEvent_t;


/* =========================================================
 * UART LOG MESSAGE
 * ========================================================= */

typedef struct
{
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

BaseType_t Role3_Init(void);


/* =========================================================
 * TASKS
 * ========================================================= */

void UserOverrideTask(void *pvParameters);

void UARTLoggingTask(void *pvParameters);


/* =========================================================
 * LOGGING
 * ========================================================= */

BaseType_t Role3_LogSend(
    const char *message,
    TickType_t timeout
);


/* =========================================================
 * OVERRIDE STATE FUNCTIONS
 * ========================================================= */

OverrideMode_t Role3_GetLightOverride(void);

OverrideMode_t Role3_GetOvenOverride(void);


/* =========================================================
 * ISR FUNCTION
 *
 * Called from the GPIO switch interrupt handlers.
 * ========================================================= */

void Role3_PostOverrideFromISR(
    OverrideSource_t source,
    OverrideMode_t mode
);


/* =========================================================
 * UART HARDWARE FUNCTION
 *
 * We will replace this later with the real UART driver.
 * ========================================================= */

void Role3_UART_WriteString(const char *text);


#endif