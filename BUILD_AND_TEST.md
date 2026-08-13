# Build, Flash and Test Procedure

Target: TI Tiva C Series **TM4C123GH6PM** LaunchPad (EK-TM4C123GXL)
Toolchain: **Keil uVision 5** with the ARM::CMSIS-FreeRTOS pack (11.3.0)

---

## 1. One-time setup

1. Install **Keil MDK-ARM** (the free Lite edition is enough; this project is
   well under the 32 KB limit).
2. In uVision open **Pack Installer** and install:
   * `Keil::TM4C_DFP` — the Tiva device family pack
   * `ARM::CMSIS-FreeRTOS` version **11.3.0** — the kernel itself
3. Open `SmartKitchenSystem.uvprojx`.
4. Open **Manage Run-Time Environment** and confirm these are ticked:
   * `CMSIS → CORE`
   * `Device → Startup`
   * `RTOS → FreeRTOS → Config`, `Core (Cortex-M)`, `Heap → Heap_1`

   The FreeRTOS kernel sources are supplied by the pack, which is why they are
   not in this repository.

TivaWare is **not** required. Every driver in this project declares its own
registers, so the project builds on any machine with just Keil and the two
packs above.

## 2. Build

**Project → Rebuild all target files.** Expect 0 errors and 0 warnings.

If the linker reports the image does not fit, or `Tasks_Init` fails at run
time, check `configTOTAL_HEAP_SIZE` in `RTE/RTOS/FreeRTOSConfig.h`
(currently 12288 bytes; the application needs roughly 6.4 KB).

## 3. Flash

1. Connect the LaunchPad by USB to the **DEBUG** port.
2. **Flash → Configure Flash Tools → Debug** tab, select **Stellaris ICDI**.
3. **Flash → Download** (F8).

## 4. Open the console

Find the LaunchPad's COM port in Device Manager under
*Ports (COM & LPT)* → "Stellaris Virtual Serial Port".

PuTTY settings:

| Setting | Value |
|---------|-------|
| Connection type | Serial |
| Serial line | COMx (from Device Manager) |
| Speed | **9600** |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |
| Flow control | None |

Press the LaunchPad **RESET** button. You should see:

```
=== Smart Kitchen System ===
Hardware initialised.
Tasks created. Starting scheduler.
SYSTEM: SMART KITCHEN RTOS STARTED
SYSTEM: AUTO MODE
LIGHT LEVEL: 2450 raw
OVEN TEMP: 24.3 C
```

If nothing appears, the baud rate is the first suspect. The divisors in
`uart.c` (IBRD 104, FBRD 11) assume a **16 MHz** system clock, which is what
this project runs at (`CLOCK_SETUP` is 0 in `system_TM4C123.c`, so the chip
stays on its 16 MHz internal oscillator).

---

## 5. Calibration before the demo

Do this once with the real circuit, and record the numbers for the report.

### Light threshold

1. Watch the `LIGHT LEVEL: nnnn raw` lines in PuTTY.
2. Note the value in normal room light, then with the LDR covered by a hand.
3. Set `LIGHT_DARK_THRESHOLD_RAW` in `tasks.h` roughly midway between them.
4. If covering the LDR makes the number go **up** instead of down, set
   `LIGHT_SENSOR_INVERTED` to 1 rather than swapping the comparison.

### Oven fault codes

1. With the LM35 **connected** at room temperature, note `OVEN TEMP`.
2. **Disconnect** the LM35 signal wire and note what the reading does.
3. Adjust `OVEN_RAW_FAULT_LOW` / `OVEN_RAW_FAULT_HIGH` so the disconnected
   case falls inside the fault band and the connected case does not.

### Demo-friendly thresholds

200 °C and 250 °C are realistic for an oven but impossible to reach safely on
a bench. For filming, temporarily lower them so a hairdryer or a fingertip can
trigger every branch, and say so on camera:

```c
#define OVEN_SETPOINT_TENTHS   350   /* 35.0 C */
#define OVEN_CRITICAL_TENTHS   400   /* 40.0 C */
```

---

## 6. Acceptance tests

Run all nine and tick them off. These map directly onto the marking criteria,
and each one produces a line in PuTTY that can be shown in the video.

| # | Test | Action | Expected |
|---|------|--------|----------|
| 1 | Auto lighting ON | Cover the LDR | Green LED on, `KITCHEN LIGHT: ON` |
| 2 | Auto lighting OFF | Uncover the LDR | Green LED off, `KITCHEN LIGHT: OFF` |
| 3 | No flicker | Hold the LDR right at the threshold | State does **not** oscillate (hysteresis) |
| 4 | Auto heating | Let the LM35 sit below setpoint | Red LED on, `OVEN ELEMENT: ON` |
| 5 | Safety cut-off | Warm the LM35 past the setpoint | Red LED off, `OVEN ELEMENT: OFF` |
| 6 | Critical cut-off beats manual | MANUAL mode, element ON, then heat past critical | `CRITICAL: OVEN OVER TEMPERATURE` and LED off **while the switch still says ON** |
| 7 | Mode toggle | Press SW1 + SW2 together | `SYSTEM: MANUAL MODE`, then `SYSTEM: AUTO MODE` |
| 8 | Manual override | In MANUAL, press SW1 then SW2 | `MANUAL LIGHT: ON`, `MANUAL OVEN: ON`, LEDs follow |
| 9 | Sensor fault | Pull the LM35 signal wire out | `FAULT: OVEN TEMP SENSOR INVALID` once, element OFF; reconnect gives `RECOVERED:` once |

Test 6 is the one worth dwelling on in the video: it is the difference between
a system that merely reads a sensor and one that is actually safe.

Test 9 should print the fault line **once**, not repeatedly. A stream of
repeated fault lines means the edge-detection latch has been broken.

---

## 7. Demonstrating the RTOS concepts

The report and the video need to show the RTOS mechanisms, not just the
kitchen behaviour. Point these out explicitly:

* **Round-robin** — Tasks 1 and 2 share priority 2. In the uVision debugger,
  open *Debug → OS Support → RTX/FreeRTOS Task List* while running and show
  both alternating between Ready and Running.
* **Priority pre-emption** — press a switch while both control tasks are
  running. Task 3 at priority 3 pre-empts them and its log line appears before
  the next periodic reading.
* **Queue** — every message in PuTTY passed through `xLogQueue`. Adding a
  `vTaskDelay` in Task 4 makes the queue back up visibly, which demonstrates
  the decoupling.
* **Mutex** — `xUARTMutex` is what stops a critical alert from interleaving
  mid-string with a routine log line.
* **Counting semaphore** — `xOverrideSemaphore` counts switch events posted
  from the ISR so none are lost between presses.
