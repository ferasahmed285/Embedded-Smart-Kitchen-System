/**
 * @file tasks.c
 * @brief FreeRTOS tasks for system control, logging, and overrides.
 * @note Role 1, Role 2, and Role 3 responsibilities.
 */
#include "tasks.h"
#include "uart.h"
#include "adc.h"
#include "led.h"
#include "switch.h"
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
    xOverrideQueue = xQueueCreate(OVERRIDE_QUEUE_LENGTH, sizeof(SwitchEvent_t));
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
 * LOG SEND / FORMATTING  (Role 3 - Task 4 services)
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

    /* Extract least significant digit first, then emit reversed. */
    do
    {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while ((value != 0u) && (count < sizeof(digits)));

    /* Zero pad the fractional field, so 5 hundredths prints as ".05" not ".5". */
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
     * are written directly to the peripheral. xUARTMutex is what makes that
     * safe: the logging task may be mid-string when this call arrives.
     * If the mutex cannot be acquired promptly the alert is still emitted -
     * garbled output is preferable to a silent thermal fault.
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

/* =========================================================
 * OVEN OVERRIDE SWITCH HEALTH  (Role 2)
 * ========================================================= */

/*
 * A mechanically stuck or badly bouncing switch produces a burst of edges far
 * faster than a person can press it. Counting events inside a short sliding
 * window lets the override handler reject the burst instead of thrashing the
 * heating element. The count decays as soon as a normal, well spaced press
 * arrives, so a healthy switch is never locked out.
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

/* =========================================================
 * TASK: USER OVERRIDE (Task 3)
 * ========================================================= */
/* Applies one classified override event. Returns the line to log. */
static const char *Override_Apply(ButtonEvent_t event)
{
    const char *message = NULL;

    /*
     * The mutex is held only for the state update itself. Logging happens
     * after it is released, so the highest priority task in the system never
     * holds the shared state lock across a queue operation.
     */
    if (xSemaphoreTake(xStateMutex, portMAX_DELAY) != pdTRUE)
    {
        return "ERROR: STATE LOCK UNAVAILABLE";
    }

    switch (event)
    {
        case EVENT_TOGGLE_MODE:
            globalSystemMode = (globalSystemMode == SYSTEM_MODE_AUTO)
                             ? SYSTEM_MODE_MANUAL : SYSTEM_MODE_AUTO;
            message = (globalSystemMode == SYSTEM_MODE_AUTO)
                    ? "SYSTEM: AUTO MODE" : "SYSTEM: MANUAL MODE";
            break;

        case EVENT_TOGGLE_LIGHT:
            if (globalSystemMode == SYSTEM_MODE_MANUAL)
            {
                manualLightOn = !manualLightOn;
                message = manualLightOn ? "MANUAL LIGHT: ON" : "MANUAL LIGHT: OFF";
            }
            else
            {
                message = "REJECTED: CANNOT TOGGLE LIGHT IN AUTO MODE";
            }
            break;

        case EVENT_TOGGLE_OVEN:
            if (globalSystemMode == SYSTEM_MODE_MANUAL)
            {
                manualOvenOn = !manualOvenOn;
                message = manualOvenOn ? "MANUAL OVEN: ON" : "MANUAL OVEN: OFF";
            }
            else
            {
                message = "REJECTED: CANNOT TOGGLE OVEN IN AUTO MODE";
            }
            break;

        default:
            break;
    }

    xSemaphoreGive(xStateMutex);
    return message;
}

/*
 * Waits for both switches to return to the released state.
 * Returns false if they are still held after SWITCH_STUCK_TIMEOUT_MS, which
 * is the stuck switch fault the specification asks for.
 */
static bool Override_WaitForRelease(void)
{
    uint32_t waited = 0;

    while (waited < SWITCH_STUCK_TIMEOUT_MS)
    {
        if (Switch_Read() == SWITCH_ALL_PINS)   /* Both lines back high */
        {
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(SWITCH_RELEASE_POLL_MS));
        waited += SWITCH_RELEASE_POLL_MS;
    }

    return false;
}

void Tasks_UserOverride(void *pvParameters)
{
    (void)pvParameters;

    /*
     * Switch_Init() enables the Port F interrupt before the RTOS objects
     * exist. A button pressed in that window reaches an ISR that masks the
     * interrupt and then finds a NULL queue, so nothing would ever re-arm it.
     * Re-arming here closes that startup race.
     */
    Switch_EnableInterrupts();

    for (;;)
    {
        SwitchEvent_t event;
        uint32_t settled;
        bool sw1;
        bool sw2;
        const char *message = NULL;

        /*
         * Counting semaphore: the ISR to task event notification. The payload
         * itself travels through xOverrideQueue, so the semaphore count and
         * the queue depth stay in step.
         */
        if (xSemaphoreTake(xOverrideSemaphore, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        if (xQueueReceive(xOverrideQueue, &event, 0) != pdPASS)
        {
            continue;
        }

        /*
         * Debounce in the task, not in the ISR. This delay also gives the user
         * a window in which to complete a two button press: pressing SW1 and
         * SW2 together within SWITCH_DEBOUNCE_MS is what toggles AUTO/MANUAL.
         */
        vTaskDelay(pdMS_TO_TICKS(SWITCH_DEBOUNCE_MS));

        settled = Switch_Read();
        sw1 = Switch_IsPressed(SWITCH_SW1_PIN, settled);
        sw2 = Switch_IsPressed(SWITCH_SW2_PIN, settled);

        if (sw1 && sw2)
        {
            message = Override_Apply(EVENT_TOGGLE_MODE);
        }
        else if (sw1)
        {
            message = Override_Apply(EVENT_TOGGLE_LIGHT);
        }
        else if (sw2)
        {
            /* Role 2: reject bursts too fast to be a human press. */
            if (Oven_SwitchIsStuck())
            {
                message = "FAULT: OVEN OVERRIDE SWITCH BOUNCING - EVENT IGNORED";
            }
            else
            {
                message = Override_Apply(EVENT_TOGGLE_OVEN);
            }
        }
        else
        {
            /* The line settled high again: the edge was pure contact noise. */
            message = NULL;
        }

        if (message != NULL)
        {
            Tasks_LogSend(message, 0);
        }

        if (!Override_WaitForRelease())
        {
            Tasks_LogCritical("FAULT: OVERRIDE SWITCH STUCK CLOSED");
        }

        /*
         * Discard events that piled up from contact bounce while this one was
         * being handled, so a single press cannot produce a burst of toggles.
         */
        while (xSemaphoreTake(xOverrideSemaphore, 0) == pdTRUE)
        {
            (void)xQueueReceive(xOverrideQueue, &event, 0);
        }

        Switch_EnableInterrupts();
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
void Tasks_LightingControl(void *pvParameters)
{
    bool lightIsOn = false;       /* Last command sent to the lamp        */
    bool inFault = false;         /* Latched fault, for edge logging      */
    uint32_t railSamples = 0;     /* Consecutive readings stuck on a rail */
    uint32_t cyclesSinceReport = 0;

    (void)pvParameters;

    for (;;)
    {
        SystemMode_t mode;
        bool lightManual;
        bool ovenManual;
        uint32_t raw;
        uint32_t level;
        bool sensorFault;
        bool turnOn = false;

        /* ---- 1. Sample the ambient light sensor ------------------------ */
        raw = ADC_ReadLightSensor();

        /*
         * Unlike the oven's LM35, an LDR can genuinely read at either rail in
         * full darkness or direct sunlight, so a single extreme sample is not
         * evidence of a fault. Only a reading pinned at exactly 0 or 4095 for
         * LIGHT_FAULT_PERSIST_SAMPLES in a row is treated as a disconnected
         * or shorted sensor.
         */
        if (raw == ADC_RAW_INVALID)
        {
            railSamples = LIGHT_FAULT_PERSIST_SAMPLES;  /* ADC itself failed */
            level = 0;
        }
        else
        {
            level = raw;

            if ((raw == 0u) || (raw >= 4095u))
            {
                if (railSamples < LIGHT_FAULT_PERSIST_SAMPLES)
                {
                    railSamples++;
                }
            }
            else
            {
                railSamples = 0;
            }
        }

        sensorFault = (railSamples >= LIGHT_FAULT_PERSIST_SAMPLES);

#if LIGHT_SENSOR_INVERTED
        level = 4095u - level;
#endif

        Tasks_GetSystemState(&mode, &lightManual, &ovenManual);

        /* ---- 2. Decide the lamp state ---------------------------------- */
        if (sensorFault)
        {
            /*
             * Fail safe for lighting is ON. A kitchen left dark because a
             * sensor failed is the more hazardous outcome, and the lamp draws
             * no dangerous energy, unlike the oven element which fails OFF.
             */
            turnOn = true;

            if (!inFault)
            {
                inFault = true;
                Tasks_LogCritical("FAULT: LIGHT SENSOR INVALID - LAMP FORCED ON");
            }
        }
        else
        {
            if (inFault)
            {
                inFault = false;
                Tasks_LogSend("RECOVERED: LIGHT SENSOR VALID", 0);
            }

            if (mode == SYSTEM_MODE_MANUAL)
            {
                turnOn = lightManual;
            }
            else
            {
                /*
                 * AUTO mode with hysteresis: switch on once the room is darker
                 * than the threshold, and only switch off again once it is
                 * clearly brighter, so passing shadows cannot make the lamp
                 * flicker at every sample.
                 */
                if (lightIsOn)
                {
                    turnOn = (level < (LIGHT_DARK_THRESHOLD_RAW + LIGHT_HYSTERESIS_RAW));
                }
                else
                {
                    turnOn = (level < LIGHT_DARK_THRESHOLD_RAW);
                }
            }
        }

        /* ---- 3. Drive the actuator ------------------------------------- */
        LED_SetKitchenLight(turnOn);

        /* ---- 4. Report ------------------------------------------------- */
        if (turnOn != lightIsOn)
        {
            lightIsOn = turnOn;
            Tasks_LogSend(turnOn ? "KITCHEN LIGHT: ON" : "KITCHEN LIGHT: OFF", 0);
        }

        if (++cyclesSinceReport >= LIGHT_REPORT_EVERY_N_CYCLES)
        {
            cyclesSinceReport = 0;

            if (!sensorFault)
            {
                Tasks_LogSendNum("LIGHT LEVEL", (int32_t)level, 0, " raw", 0);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LIGHT_SAMPLE_PERIOD_MS));
    }
}

/* =========================================================
 * TASK: OVEN CONTROL (Task 2)
 * ========================================================= */
void Tasks_OvenControl(void *pvParameters)
{
    bool ovenIsOn = false;      /* Last command sent to the heating element   */
    bool inFault = false;       /* Latched fault state, for edge logging      */
    bool overTemp = false;      /* Latched critical cut-off, for edge logging */
    uint32_t cyclesSinceReport = 0;

    (void)pvParameters;

    for (;;)
    {
        SystemMode_t mode;
        bool lightManual;
        bool ovenManual;
        uint32_t raw;
        int32_t tempTenths;
        bool sensorFault;
        bool turnOn = false;

        /* ---- 1. Sample the thermal sensor ------------------------------ */
        raw = ADC_ReadTemperatureRaw();
        tempTenths = ADC_RawToTenths(raw);

        /*
         * Fault detection is performed on the raw ADC code, not on the
         * converted temperature. A disconnected or short circuited LM35 pins
         * the input to a supply rail, which produces codes at the very ends of
         * the 12-bit span; after conversion those same codes look like
         * plausible temperatures, so the fault would go unnoticed.
         */
        sensorFault = (raw == ADC_RAW_INVALID) ||
                      (raw <= OVEN_RAW_FAULT_LOW) ||
                      (raw >= OVEN_RAW_FAULT_HIGH);

        Tasks_GetSystemState(&mode, &lightManual, &ovenManual);

        /* ---- 2. Decide the element state ------------------------------- */
        if (sensorFault)
        {
            /*
             * Highest precedence: with no trustworthy temperature the element
             * is unconditionally de-energised and the manual request latched
             * off, so restoring the sensor cannot silently restart heating.
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
                Tasks_LogSend("RECOVERED: OVEN TEMP SENSOR VALID", 0);
            }

            if (tempTenths >= OVEN_CRITICAL_TENTHS)
            {
                /*
                 * Critical cut-off. This deliberately outranks the manual
                 * override: the specification allows the user to command the
                 * element regardless of the sensor, but the safety section
                 * requires the element to be disabled once the critical
                 * threshold is exceeded. Safety wins.
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
                /* Still inside the critical band - stay latched off. */
                turnOn = false;
            }
            else
            {
                if (overTemp)
                {
                    overTemp = false;
                    Tasks_LogSend("RECOVERED: OVEN TEMPERATURE BACK IN RANGE", 0);
                }

                if (mode == SYSTEM_MODE_MANUAL)
                {
                    /* Manual override, still fenced by the checks above. */
                    turnOn = ovenManual;
                }
                else
                {
                    /*
                     * AUTO mode with hysteresis: heat until the setpoint is
                     * reached, then stay off until the temperature has fallen
                     * a full band below it. Without the band the element and
                     * its log line would toggle on every 500 ms sample while
                     * sitting at the threshold.
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

        /*
         * Periodic telemetry. The temperature is reported on a slower cadence
         * than the control loop so the queue is not saturated with readings
         * that the low priority logging task cannot drain in time.
         */
        if (++cyclesSinceReport >= OVEN_REPORT_EVERY_N_CYCLES)
        {
            cyclesSinceReport = 0;

            if (!sensorFault)
            {
                Tasks_LogSendNum("OVEN TEMP", tempTenths, 1, " C", 0);
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
     * caller still receives a defined, safe state (AUTO, everything off)
     * instead of whatever happened to be on its stack.
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
     * Called by the oven safety cut-off. Without this latch the manual request
     * would survive the fault and re-energise the element the instant the
     * temperature drops back into range.
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
void Tasks_PostSwitchEventFromISR(uint32_t pinSnapshot)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    SwitchEvent_t event;

    if ((xOverrideQueue == NULL) || (xOverrideSemaphore == NULL))
        return;

    event.pins = pinSnapshot;
    event.tick = xTaskGetTickCountFromISR();

    /*
     * The semaphore is only given when the payload was actually queued, so the
     * count can never run ahead of the number of events waiting to be read.
     */
    if (xQueueSendFromISR(xOverrideQueue, &event, &xHigherPriorityTaskWoken) == pdPASS)
    {
        xSemaphoreGiveFromISR(xOverrideSemaphore, &xHigherPriorityTaskWoken);
    }

    /* Task 3 runs at the highest priority, so switch to it on ISR exit. */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
