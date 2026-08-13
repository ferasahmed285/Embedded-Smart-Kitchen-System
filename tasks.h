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

/* ---------------------------------------------------------
 * Role 2 - Oven control and safety parameters
 * Temperatures are in tenths of a degree Celsius (2000 == 200.0 C) so the
 * control task needs no floating point arithmetic.
 * --------------------------------------------------------- */
#define OVEN_SAMPLE_PERIOD_MS        500   /* Task 2 control loop period      */
#define OVEN_REPORT_EVERY_N_CYCLES   4     /* Telemetry cadence (~2 s)        */
#define OVEN_SETPOINT_TENTHS         2000  /* 200.0 C target                  */
#define OVEN_HYSTERESIS_TENTHS       50    /* 5.0 C band, stops chattering    */
#define OVEN_CRITICAL_TENTHS         2500  /* 250.0 C hard cut-off, beats manual */

/*
 * Raw 12-bit ADC codes that indicate a wiring fault rather than a temperature.
 * A disconnected or shorted LM35 pins the input to a rail; healthy readings
 * never sit at the extremes of the span.
 */
#define OVEN_RAW_FAULT_LOW           5     /* <= this: shorted to GND / open  */
#define OVEN_RAW_FAULT_HIGH          4090  /* >= this: shorted to 3.3 V       */

/*
 * Stuck / bouncing override switch detection: more than this many oven toggle
 * events inside the window is faster than a human can press the button.
 */
#define OVEN_SWITCH_WINDOW_TICKS     30    /* 300 ms at 100 Hz tick rate      */
#define OVEN_SWITCH_STUCK_EVENTS     5

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

/* ---------------------------------------------------------
 * Logging API (Role 3 - Task 4 formatting services)
 * --------------------------------------------------------- */

/* Queues a plain text line for transmission by the UART logging task. */
BaseType_t Tasks_LogSend(const char *message, TickType_t timeout);

/*
 * Queues a line carrying a numeric value, e.g.
 *   Tasks_LogSendNum("OVEN TEMP", 1874, 1, " C", 0)  ->  "OVEN TEMP: 187.4 C"
 * value      : the quantity, pre-scaled by 10^decimals
 * decimals   : how many of the low-order digits are fractional (0, 1 or 2)
 * unit       : appended verbatim, may be NULL
 * Implemented without <stdio.h> so the build needs no float formatting
 * support and no large printf machinery in a 4 KB RTOS heap.
 */
BaseType_t Tasks_LogSendNum(const char *label, int32_t value, uint8_t decimals,
                            const char *unit, TickType_t timeout);

/*
 * Writes a critical alert straight to the UART, bypassing the log queue so the
 * message cannot be dropped or delayed behind routine telemetry. Serialised
 * against the logging task with xUARTMutex.
 */
void Tasks_LogCritical(const char *message);

/*
 * Reads the shared state under xStateMutex.
 * Returns pdFALSE if the state could not be read; the outputs are then filled
 * with the fail-safe defaults (AUTO mode, both actuators off) so that callers
 * never act on uninitialised stack data.
 */
BaseType_t Tasks_GetSystemState(SystemMode_t *mode, bool *lightOn, bool *ovenOn);

/* Latches the manual oven request off (used by the Role 2 safety cut-off). */
void Tasks_ClearOvenManual(void);

void Tasks_PostButtonEventFromISR(ButtonEvent_t event);

#endif // TASKS_H
