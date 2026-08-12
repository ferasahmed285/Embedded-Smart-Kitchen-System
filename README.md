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