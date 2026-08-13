#ifndef ADC_H
#define ADC_H
#include <stdint.h>

/*
 * Returned by ADC_ReadTemperatureTenths() when the reading cannot be trusted:
 * either the PE1 disconnect jumper has been pulled, or the ADC failed to
 * complete a conversion.
 */
#define ADC_TEMP_TENTHS_INVALID  ((int32_t)-30000)

/* Value reported by the legacy float accessor for the same fault condition. */
#define ADC_TEMP_FAULT_C         999.0f

/* Initializes ADC0 for Software Triggered readings (Tasks 1 & 2) */
void ADC_Init(void);

/* Reads the ambient light sensor (LDR) on PE3 */
uint32_t ADC_ReadLightSensor(void);

/*
 * Oven temperature in tenths of a degree Celsius (e.g. 304 == 30.4 C).
 * Integer scaling keeps the oven control task free of floating point, so the
 * safety comparisons are exact and no float formatting is needed to log them.
 * Returns ADC_TEMP_TENTHS_INVALID on a sensor fault.
 */
int32_t ADC_ReadTemperatureTenths(void);

/* Legacy floating point accessor. Returns ADC_TEMP_FAULT_C on a fault. */
float ADC_ReadTemperatureSensor(void);

#endif // ADC_H
