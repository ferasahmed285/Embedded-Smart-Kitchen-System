#ifndef ADC_H
#define ADC_H
#include <stdint.h>

/*
 * Returned by the raw read functions when the ADC never asserted its
 * end-of-conversion flag (peripheral not clocked / sample sequencer stalled).
 */
#define ADC_RAW_INVALID          0xFFFFFFFFu

/* Returned by ADC_ReadTemperatureTenths() when the conversion failed. */
#define ADC_TEMP_TENTHS_INVALID  ((int32_t)-30000)

/* Initializes ADC0 for Software Triggered readings (Tasks 1 & 2) */
void ADC_Init(void);

/* Reads the ambient light sensor (LDR) on PE3/AIN0. ADC_RAW_INVALID on failure */
uint32_t ADC_ReadLightSensor(void);

/*
 * Reads the external oven thermal sensor (LM35 on PE2/AIN1) and returns the
 * unconverted 12-bit ADC code. Role 2 fault detection works on this raw code
 * because a disconnected sensor pins the input rail-to-rail, which is only
 * distinguishable before the linear conversion is applied.
 * Returns ADC_RAW_INVALID if the conversion did not complete.
 */
uint32_t ADC_ReadTemperatureRaw(void);

/*
 * Oven temperature in tenths of a degree Celsius (e.g. 1874 == 187.4 C).
 * Integer scaling keeps the control task free of floating point, so no
 * float formatting support is needed in the UART logging path.
 * Returns ADC_TEMP_TENTHS_INVALID if the conversion did not complete.
 */
int32_t ADC_ReadTemperatureTenths(void);

/* Converts a raw 12-bit LM35 code into tenths of a degree Celsius. */
int32_t ADC_RawToTenths(uint32_t raw);

/* Legacy floating point accessor, kept for compatibility. */
float ADC_ReadTemperatureSensor(void);

#endif // ADC_H
