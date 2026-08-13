# Smart Kitchen System: 5-Person Project Breakdown

### Role 1: Lighting System Developer (Software Only)
* Develops **Task 1 (Lighting Control Task)** to run at Normal Priority.
* Programs the logic to sample ambient light data and automatically request the lighting actuator to turn ON or OFF based on a threshold.
* Writes the logic to allow manual lighting overrides to take precedence over the sensor.
* **Files owned**: `tasks.c` (Lighting portion), `adc.c/h` (Light sensor), `led.c/h` (Lighting actuator)

### Role 2: Oven System & Safety Developer (Software Only)
* Develops **Task 2 (Oven Control Task)** to run at Normal Priority, equal to Task 1 for Round-Robin execution.
* Programs the thermal sensor sampling and the critical safety cut-offs that disable the heating element when the threshold is exceeded.
* Writes the logic for the manual oven override switch.
* **Files owned**: `tasks.c` (Oven portion), `adc.c/h` (Temp sensor), `led.c/h` (Heating element)

### Role 3: RTOS Architecture & Synchronization (Software Only)
* Develops **Task 3 (User Override Task)** at the Highest Priority to ensure deterministic, low-latency execution for the manual switches.
* Develops **Task 4 (UART Logging Task)** to format and transmit system status to the PuTTY terminal over UART.
* Implements the FreeRTOS Queues for message passing, Mutexes to guard the UART hardware, and Counting Semaphores for event notifications.
* **Files owned**: `tasks.c` (Override and Logging), `uart.c/h`

### Role 4: Hardware Integration & Faults (The Board Owner)
* Physically wires the LDR, thermal sensor, switches, actuators/LEDs, and the serial communication interface on the microcontroller board.
* Flashes the code provided by Roles 1, 2, and 3 onto the board and resolves any integration bugs.
* Programs the hardware-level error handling to detect disconnected sensors or stuck switches and safely forces the oven heating element OFF during a fault.
* Records raw video footage of the working prototype to send to Role 5.
* **Files owned**: `switch.c/h`, `main.c` (Hardware setup)

### Role 5: Technical Documentation & Demo (The Deliverables Owner)
* Compiles the complete technical report in one PDF file, including the abstract, body, individual contributions, and references.
* Draws the flow charts for the main program flow and generates the wiring diagrams based on Role 4's circuit.
* Edits the raw footage provided by Role 4 into the final 5-minute video demo explaining the project's work, and uploads it to an online server.

## Hardware Wiring & Pin Mapping (Role 4 & 5 Reference)
This section defines the physical hardware connections required on the TM4C123 Launchpad for the project.

### Sensors (Analog Inputs)
* **Ambient Light Sensor (LDR)**: Connect to **PE3 (AIN0)**.
  * Divider: `3.3V --- LDR --- PE3 --- 10k --- GND`. The reading therefore **rises with brightness**.
  * If you build the divider the other way round, set `LIGHT_SENSOR_INVERTED` to `1` in `tasks.h`. Do not edit the comparison logic.
* **Oven Temperature Sensor (LM35)**: Connect to **PE2 (AIN1)**. Vs to 3.3V, GND to GND, Vout to PE2.
  * 10 mV/°C, so the 3.3 V reference saturates at about 330 °C.

### User Inputs (Digital Switch Interrupts)
* **Kitchen Light Toggle Switch**: **PF4** (Onboard **SW1**). (Note: Only functions in Manual mode).
* **Oven Toggle Switch**: **PF0** (Onboard **SW2**). (Note: Only functions in Manual mode).
* **Master Mode Switch (Auto/Manual)**: Press **SW1** and **SW2** together, within 40 ms of each other, to toggle between Automatic and Manual modes.

### Actuators (Digital Outputs)
* **Kitchen Light Actuator (LED)**: Connect to **PF3** (Can use onboard **Green LED**).
* **Oven Heating Element (LED)**: Connect to **PF1** (Can use onboard **Red LED**).

### Serial Communication (UART)
* **PuTTY Logging Terminal**: Uses **UART0** on **PA0 (RX)** and **PA1 (TX)**. This is automatically routed through the Launchpad's USB debug cable (9600 Baud, 8 Data Bits, 1 Stop Bit, No Parity).

## System Behaviour

### Task set

| Task | Name | Priority | Period | Responsibility |
|------|------|----------|--------|----------------|
| 1 | `LightCtrl` | 2 (normal) | 500 ms | Samples the LDR, drives the lamp |
| 2 | `OvenCtrl` | 2 (normal) | 500 ms | Samples the LM35, drives the element, enforces safety |
| 3 | `Override` | 3 (highest) | event driven | Debounces and applies the manual switches |
| 4 | `UART` | 1 (lowest) | event driven | Drains the log queue to PuTTY |

Tasks 1 and 2 share a priority, so the kernel time-slices between them on every
tick. This is the round-robin behaviour the assignment asks the design to show.

### Synchronisation objects

| Object | Type | Purpose |
|--------|------|---------|
| `xLogQueue` | Queue, 10 x 80 B | Status and telemetry from every task to Task 4 |
| `xOverrideQueue` | Queue, 10 x 8 B | Switch snapshots from the Port F ISR to Task 3 |
| `xOverrideSemaphore` | Counting semaphore | ISR to task event notification |
| `xStateMutex` | Mutex | Guards the shared mode/override state |
| `xUARTMutex` | Mutex | Guards UART0 between Task 4 and critical alerts |

### Decision precedence

The oven applies these rules in strict order. A rule never overrides one above it.

1. **Sensor fault** (raw ADC pinned to a rail, or conversion timeout) → element OFF, manual request cleared
2. **Critical temperature** (250.0 °C) → element OFF, manual request cleared, even in MANUAL mode
3. **MANUAL mode** → element follows SW2
4. **AUTO mode** → heat below 200.0 °C, stop at 200.0 °C, 5.0 °C hysteresis

The lamp uses the same structure, except that its fail-safe is ON rather than
OFF: a dark kitchen is the more hazardous failure, and the lamp is not an
energy hazard the way the element is.

### Tuning constants

All thresholds live in one block in `tasks.h`:

| Constant | Default | Meaning |
|----------|---------|---------|
| `OVEN_SETPOINT_TENTHS` | 2000 | 200.0 °C target |
| `OVEN_CRITICAL_TENTHS` | 2500 | 250.0 °C hard cut-off |
| `OVEN_HYSTERESIS_TENTHS` | 50 | 5.0 °C band |
| `OVEN_RAW_FAULT_LOW/HIGH` | 5 / 4090 | Raw ADC codes that mean a wiring fault |
| `LIGHT_DARK_THRESHOLD_RAW` | 2000 | Below this the room counts as dark |
| `LIGHT_HYSTERESIS_RAW` | 200 | Band that stops flicker |

Calibrate `LIGHT_DARK_THRESHOLD_RAW` and the oven fault codes against the real
circuit before the demo. See `BUILD_AND_TEST.md`.