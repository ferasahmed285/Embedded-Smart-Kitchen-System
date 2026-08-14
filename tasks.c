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

/*
 * Minimal formatting primitives. Each appends at 'pos' and returns the new
 * write position, never writing past 'cap - 1' so the buffer stays terminated.
 */
static size_t Log_AppendStr(char *dst, size_t cap, size_t pos, const char *src)
{
    if (src == NULL) return pos;

    while ((*src != '\0') && (pos < (cap - 1)))
    {
        dst[pos++] = *src++;
    }

    dst[pos] = '\0';
    return pos;
}

static size_t Log_AppendUInt(char *dst, size_t cap, size_t pos, uint32_t value,
                             uint8_t minDigits)
{
    char digits[12];
    uint8_t count = 0;

    /* Extract the least significant digit first, then emit reversed. */
    do
    {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while ((value != 0u) && (count < sizeof(digits)));

    /* Zero pad the fractional field, so 5 hundredths prints ".05" not ".5". */
    while ((count < minDigits) && (count < sizeof(digits)))
    {
        digits[count++] = '0';
    }

    while ((count > 0u) && (pos < (cap - 1)))
    {
        dst[pos++] = digits[--count];
    }

    dst[pos] = '\0';
    return pos;
}

BaseType_t Tasks_LogSendNum(const char *label, int32_t value, uint8_t decimals,
                            const char *unit, TickType_t timeout)
{
    char line[LOG_MESSAGE_SIZE];
    uint32_t magnitude;
    uint32_t scale = 1u;
    uint8_t i;
    size_t pos = 0;

    for (i = 0; i < decimals; i++)
    {
        scale *= 10u;
    }

    pos = Log_AppendStr(line, sizeof(line), pos, label);
    pos = Log_AppendStr(line, sizeof(line), pos, ": ");

    if (value < 0)
    {
        pos = Log_AppendStr(line, sizeof(line), pos, "-");
        magnitude = (uint32_t)(-value);
    }
    else
    {
        magnitude = (uint32_t)value;
    }

    pos = Log_AppendUInt(line, sizeof(line), pos, magnitude / scale, 1);

    if (decimals > 0u)
    {
        pos = Log_AppendStr(line, sizeof(line), pos, ".");
        pos = Log_AppendUInt(line, sizeof(line), pos, magnitude % scale, decimals);
    }

    (void)Log_AppendStr(line, sizeof(line), pos, unit);

    return Tasks_LogSend(line, timeout);
}

void Tasks_LogCritical(const char *message)
{
    /*
     * Safety alerts must reach PuTTY even when the log queue is full, so they
     * are written straight to the peripheral. xUARTMutex is what makes that
     * safe: the logging task may be part way through a string when this call
     * arrives. If the mutex cannot be acquired promptly the alert is emitted
     * anyway, because garbled output beats a silent thermal fault.
     */
    BaseType_t haveMutex = pdFALSE;

    if (xUARTMutex != NULL)
    {
        haveMutex = xSemaphoreTake(xUARTMutex, pdMS_TO_TICKS(50));
    }

    UART_SendString(message);
    UART_SendString("\r\n");

    if (haveMutex == pdTRUE)
    {
        xSemaphoreGive(xUARTMutex);
    }
}

/*
 * A mechanically stuck or badly bouncing switch produces a burst of edges far
 * faster than a person can press it. Counting events in a short sliding window
 * lets the override handler reject the burst instead of thrashing the heating
 * element. The count resets as soon as a normally spaced press arrives, so a
 * healthy switch is never locked out.
 */
static bool Oven_SwitchIsStuck(void)
{
    static TickType_t lastEventTick = 0;
    static uint32_t burstCount = 0;

    TickType_t now = xTaskGetTickCount();

    if ((now - lastEventTick) < OVEN_SWITCH_WINDOW_TICKS)
    {
        burstCount++;
    }
    else
    {
        burstCount = 1;
    }

    lastEventTick = now;

    return (burstCount >= OVEN_SWITCH_STUCK_EVENTS);
}

#define PIN_SW2    (1 << 0) /* PF0 */
#define PIN_SW1    (1 << 4) /* PF4 */

/*
 * Double-click software debouncer. 
 * Enforces a strict 50ms cooldown and rejects release-bounces.
 */
static bool Button_IsBouncing(int buttonIndex)
{
    static TickType_t lastEventTick[2] = {0, 0};
    TickType_t now = xTaskGetTickCount();

    if ((now - lastEventTick[buttonIndex]) < pdMS_TO_TICKS(50)) {
        return true; /* Still bouncing / pressed too fast */
    }

    /* Reject release-bounces by confirming the button is actually still pressed (LOW) */
    if (buttonIndex == 0 && (GPIO_PORTF_DATA_R & PIN_SW1)) return true; 
    if (buttonIndex == 1 && (GPIO_PORTF_DATA_R & PIN_SW2)) return true; 

    lastEventTick[buttonIndex] = now;
    return false;
}


/* =========================================================
 * TASK: USER OVERRIDE (Task 3)
 * ========================================================= */
void Tasks_UserOverride(void *pvParameters)
{
    ButtonEvent_t event;
    (void)pvParameters;

    /* Initialize mode state on boot based on physical wire */
    globalSystemMode = (GPIO_PORTB_DATA_R & 0x01) ? SYSTEM_MODE_MANUAL : SYSTEM_MODE_AUTO;

    for (;;)
    {
        if (xSemaphoreTake(xOverrideSemaphore, portMAX_DELAY) == pdTRUE)
        {
            if (xQueueReceive(xOverrideQueue, &event, 0) == pdPASS)
            {
                if (xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE)
                {
                    if (event == EVENT_UPDATE_MODE) {
                        bool isManual = (GPIO_PORTB_DATA_R & 0x01);
                        SystemMode_t newMode = isManual ? SYSTEM_MODE_MANUAL : SYSTEM_MODE_AUTO;
                        if (globalSystemMode != newMode) {
                            globalSystemMode = newMode;
                            Tasks_LogSend((globalSystemMode == SYSTEM_MODE_AUTO) ? "SYSTEM: AUTO MODE" : "SYSTEM: MANUAL MODE", 0);
                        }
                    }
                    else if (event == EVENT_TOGGLE_LIGHT) {
                        if (!Button_IsBouncing(0)) {
                            if (globalSystemMode == SYSTEM_MODE_MANUAL) {
                                manualLightOn = !manualLightOn;
                                Tasks_LogSend(manualLightOn ? "MANUAL LIGHT: ON" : "MANUAL LIGHT: OFF", 0);
                            } else {
                                Tasks_LogSend("REJECTED: CANNOT TOGGLE LIGHT IN AUTO MODE", 0);
                            }
                        }
                    }
                    else if (event == EVENT_TOGGLE_OVEN) {
                        /* Role 2: reject bursts too fast to be a human press. */
                        if (Oven_SwitchIsStuck()) {
                            Tasks_LogSend("FAULT: OVEN OVERRIDE SWITCH BOUNCING - EVENT IGNORED", 0);
                        }
                        else if (!Button_IsBouncing(1)) {
                            if (globalSystemMode == SYSTEM_MODE_MANUAL) {
                                manualOvenOn = !manualOvenOn;
                                Tasks_LogSend(manualOvenOn ? "MANUAL OVEN: ON" : "MANUAL OVEN: OFF", 0);
                            } else {
                                Tasks_LogSend("REJECTED: CANNOT TOGGLE OVEN IN AUTO MODE", 0);
                            }
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
    const uint32_t LIGHT_THRESHOLD = 3500; 
    
    /* Stagger the start time by 100ms so it never reads the ADC at the exact same time as the Oven Task */
    vTaskDelay(pdMS_TO_TICKS(100));

    for(;;) {
        uint32_t lightLevel = ADC_ReadLightSensor();
        SystemMode_t mode;
        bool lightManual;
        bool ovenManual;
        Tasks_GetSystemState(&mode, &lightManual, &ovenManual);
        
        bool turnOn = false;
        static bool wasInFault = false;

        /* Hardware Fault Detection: If LDR is unplugged, ADC reads ~0 (pulled down by internal PDR) or ~4095 (short) */
        if (lightLevel > 4090 || lightLevel < 10) {
            if (!wasInFault) {
                Tasks_LogSend("FAULT: LIGHT SENSOR DISCONNECTED. LIGHT FORCED OFF.", 0);
                wasInFault = true;
            }
            turnOn = false;
        } else {
            if (wasInFault) {
                Tasks_LogSend("SYSTEM: LIGHT SENSOR FAULT CLEARED.", 0);
                wasInFault = false;
            }

            if (mode == SYSTEM_MODE_MANUAL) {
                turnOn = lightManual;
            } else {
                /* AUTO mode: turn on if dark (Low ADC because LDR resistance is high) */
                if (lightLevel < LIGHT_THRESHOLD) {
                    turnOn = true;
                } else {
                    turnOn = false;
                }
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
void Tasks_OvenControl(void *pvParameters)
{
    bool ovenIsOn = false;      /* Last command sent to the heating element   */
    bool inFault = false;       /* Latched sensor fault, for edge logging     */
    bool overTemp = false;      /* Latched critical cut-off, for edge logging */
    uint32_t cyclesSinceReport = 0;

    (void)pvParameters;

    for (;;)
    {
        SystemMode_t mode;
        bool lightManual;
        bool ovenManual;
        int32_t tempTenths;
        bool sensorFault;
        bool turnOn = false;

        /* ---- 1. Sample the thermal sensor ------------------------------ */
        tempTenths = ADC_ReadTemperatureTenths();
        sensorFault = (tempTenths == ADC_TEMP_TENTHS_INVALID);

        Tasks_GetSystemState(&mode, &lightManual, &ovenManual);

        /* ---- 2. Decide the element state ------------------------------- */
        if (sensorFault)
        {
            /*
             * Highest precedence: with no trustworthy temperature the element
             * is unconditionally de-energised, and the manual request is
             * latched off so that reconnecting the sensor cannot silently
             * restart heating.
             */
            turnOn = false;

            if (!inFault)
            {
                inFault = true;
                Tasks_ClearOvenManual();
                Tasks_LogCritical("FAULT: OVEN TEMP SENSOR INVALID - ELEMENT FORCED OFF");
            }
        }
        else
        {
            if (inFault)
            {
                inFault = false;
                Tasks_LogSend("SYSTEM: OVEN SENSOR FAULT CLEARED", 0);
            }

            if (tempTenths >= OVEN_CRITICAL_TENTHS)
            {
                /*
                 * Critical cut-off. This deliberately outranks the manual
                 * override. The specification lets the user command the
                 * element regardless of the sensor, but it also requires the
                 * element to be disabled once the threshold is exceeded.
                 * Safety wins: previously MANUAL mode assigned the element
                 * state straight from the switch with no temperature test at
                 * all, so the element could be held on at any temperature.
                 */
                turnOn = false;

                if (!overTemp)
                {
                    overTemp = true;
                    Tasks_ClearOvenManual();
                    Tasks_LogCritical("CRITICAL: OVEN OVER TEMPERATURE - ELEMENT FORCED OFF");
                    Tasks_LogSendNum("CRITICAL TEMP", tempTenths, 1, " C", 0);
                }
            }
            else if (overTemp && (tempTenths > (OVEN_CRITICAL_TENTHS - OVEN_HYSTERESIS_TENTHS)))
            {
                /* Still inside the critical band, stay latched off. */
                turnOn = false;
            }
            else
            {
                if (overTemp)
                {
                    overTemp = false;
                    Tasks_LogSend("SYSTEM: OVEN TEMPERATURE BACK IN RANGE", 0);
                }

                if (mode == SYSTEM_MODE_MANUAL)
                {
                    /* Manual override, still fenced by both checks above. */
                    turnOn = ovenManual;
                }
                else
                {
                    /*
                     * AUTO mode with hysteresis: heat until the setpoint is
                     * reached, then stay off until the temperature has fallen
                     * a full band below it. Without the band the element and
                     * its log line toggle on every sample while sitting at
                     * the threshold.
                     */
                    if (ovenIsOn)
                    {
                        turnOn = (tempTenths < OVEN_SETPOINT_TENTHS);
                    }
                    else
                    {
                        turnOn = (tempTenths < (OVEN_SETPOINT_TENTHS - OVEN_HYSTERESIS_TENTHS));
                    }
                }
            }
        }

        /* ---- 3. Drive the actuator ------------------------------------- */
        LED_SetOvenElement(turnOn);

        /* ---- 4. Report ------------------------------------------------- */
        if (turnOn != ovenIsOn)
        {
            ovenIsOn = turnOn;
            Tasks_LogSend(turnOn ? "OVEN ELEMENT: ON" : "OVEN ELEMENT: OFF", 0);
        }

        /* Continuous temperature logging, once per second. */
        if (++cyclesSinceReport >= OVEN_REPORT_EVERY_N_CYCLES)
        {
            cyclesSinceReport = 0;

            if (!sensorFault)
            {
                Tasks_LogSendNum("CURRENT TEMP", tempTenths, 1, " C", 0);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(OVEN_SAMPLE_PERIOD_MS));
    }
}

/* =========================================================
 * GETTERS & SETTERS
 * ========================================================= */
BaseType_t Tasks_GetSystemState(SystemMode_t *mode, bool *lightOn, bool *ovenOn)
{
    /*
     * Fail-safe defaults are written first. If the mutex is unavailable the
     * caller still receives a defined, safe state instead of whatever
     * happened to be on its stack.
     */
    *mode = SYSTEM_MODE_AUTO;
    *lightOn = false;
    *ovenOn = false;

    if (xStateMutex == NULL)
    {
        return pdFALSE;
    }

    if (xSemaphoreTake(xStateMutex, portMAX_DELAY) != pdTRUE)
    {
        return pdFALSE;
    }

    *mode = globalSystemMode;
    *lightOn = manualLightOn;
    *ovenOn = manualOvenOn;
    xSemaphoreGive(xStateMutex);

    return pdTRUE;
}

void Tasks_ClearOvenManual(void)
{
    /*
     * Called by the oven safety cut-offs. Without this latch the manual
     * request would survive the fault and re-energise the element as soon as
     * the temperature dropped back into range.
     */
    if (xStateMutex != NULL && xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE)
    {
        manualOvenOn = false;
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
