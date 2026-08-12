#ifndef ADC_H
#define ADC_H
#include <stdint.h>

/* Initializes ADC0 for Software Triggered readings (Tasks 1 & 2) */
void ADC_Init(void);

/* Reads the ambient light sensor (LDR) on PE3 */
uint32_t ADC_ReadLightSensor(void);

/* Reads the internal temperature sensor (TS0) and returns Celsius */
float ADC_ReadTemperatureSensor(void);

#endif // ADC_H
