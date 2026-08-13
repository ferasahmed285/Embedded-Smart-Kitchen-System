/**
 * @file adc.c
 * @brief ADC configuration for Ambient Light and Oven Temperature sensors.
 * @note Role 2 and Role 4 responsibilities.
 */
#include "adc.h"
#include <stdbool.h>

/* ================= REGISTERS ================= */
#define SYSCTL_RCGCGPIO_R  (*((volatile unsigned long *)0x400FE608))
#define SYSCTL_RCGCADC_R   (*((volatile unsigned long *)0x400FE638))

/* GPIO E for AIN0 (PE3) and AIN1 (PE2) */
#define GPIO_PORTE_DIR_R   (*((volatile unsigned long *)0x40024400))
#define GPIO_PORTE_AFSEL_R (*((volatile unsigned long *)0x40024420))
#define GPIO_PORTE_DEN_R   (*((volatile unsigned long *)0x4002451C))
#define GPIO_PORTE_AMSEL_R (*((volatile unsigned long *)0x40024528))

#define ADC0_ACTSS_R   (*((volatile unsigned long *)0x40038000))
#define ADC0_EMUX_R    (*((volatile unsigned long *)0x40038014))
#define ADC0_PSSI_R    (*((volatile unsigned long *)0x40038028))
#define ADC0_RIS_R     (*((volatile unsigned long *)0x40038004))
#define ADC0_ISC_R     (*((volatile unsigned long *)0x4003800C))

/* SS3 (Temperature on AIN1/PE2) */
#define ADC0_SSMUX3_R  (*((volatile unsigned long *)0x400380A0))
#define ADC0_SSCTL3_R  (*((volatile unsigned long *)0x400380A4))
#define ADC0_SSFIFO3_R (*((volatile unsigned long *)0x400380A8))

/* SS2 (Light on AIN0/PE3) */
#define ADC0_SSMUX2_R  (*((volatile unsigned long *)0x40038080))
#define ADC0_SSCTL2_R  (*((volatile unsigned long *)0x40038084))
#define ADC0_SSFIFO2_R (*((volatile unsigned long *)0x40038088))

/*
 * Upper bound on the end-of-conversion poll. A software triggered conversion
 * completes in a few microseconds, so this loop count is several orders of
 * magnitude longer than the worst case; reaching it means the peripheral is
 * faulty. Bounding the wait stops a dead ADC from hanging the task forever
 * (the original unbounded 'while' would have frozen the whole subsystem).
 */
#define ADC_WAIT_LIMIT  100000u

/* ================= TEMP CONVERT ================= */

/*
 * LM35 outputs 10 mV per degree Celsius.
 * Vref = 3.3 V, ADC resolution = 4096 (12-bit).
 *   Temp(C)      = (raw * 3.3 / 4096) / 0.010
 *   Temp(0.1 C)  = (raw * 3300) / 4096
 * raw is at most 4095, so the intermediate product stays well inside int32.
 */
int32_t ADC_RawToTenths(uint32_t raw)
{
    if (raw == ADC_RAW_INVALID)
    {
        return ADC_TEMP_TENTHS_INVALID;
    }

    return (int32_t)((raw * 3300u) / 4096u);
}

/* Polls the given sample sequencer's completion flag with a bounded wait. */
static bool ADC_WaitForConversion(uint32_t ssMask)
{
    uint32_t guard = ADC_WAIT_LIMIT;

    while ((ADC0_RIS_R & ssMask) == 0)
    {
        if (--guard == 0u)
        {
            return false;
        }
    }

    return true;
}

/* ================= INIT ADC ================= */
void ADC_Init(void)
{
    /* 1. Enable ADC0 and GPIOE clocks */
    SYSCTL_RCGCADC_R |= 1;
    SYSCTL_RCGCGPIO_R |= 0x10; 
    for(volatile int i=0; i<1000; i++); 

    /* 2. Configure PE2 (AIN1) and PE3 (AIN0) for analog inputs */
    GPIO_PORTE_DIR_R &= ~0x0C;     /* PE2, PE3 as inputs */
    GPIO_PORTE_AFSEL_R |= 0x0C;    /* Enable alt function on PE2, PE3 */
    GPIO_PORTE_DEN_R &= ~0x0C;     /* Disable digital I/O on PE2, PE3 */
    GPIO_PORTE_AMSEL_R |= 0x0C;    /* Enable analog function on PE2, PE3 */

    /* 3. Disable SS2 and SS3 before config */
    ADC0_ACTSS_R &= ~((1 << 2) | (1 << 3));
    
    /* 4. Software trigger for both (default 0x0 in EMUX) */
    ADC0_EMUX_R &= ~0xFF00;

    /* 5. Config SS3 for External Temperature Sensor (AIN1) */
    ADC0_SSMUX3_R = 1;               /* Channel 1 (AIN1/PE2) */
    ADC0_SSCTL3_R = (1<<1) | (1<<2); /* IE0, END0 (No TS0 since it's external) */

    /* 6. Config SS2 for LDR/Light Sensor (AIN0) */
    ADC0_SSMUX2_R = 0;               /* Channel 0 (AIN0/PE3) */
    ADC0_SSCTL2_R = (1<<1) | (1<<2); /* IE0, END0 */

    /* 7. Re-enable SS2 and SS3 */
    ADC0_ACTSS_R |= (1 << 2) | (1 << 3);
}

/* ================= READ LIGHT ================= */
uint32_t ADC_ReadLightSensor(void)
{
    uint32_t result;

    ADC0_PSSI_R = (1 << 2);                   /* Initiate SS2 */

    if (!ADC_WaitForConversion(1 << 2))       /* Bounded wait for conversion */
    {
        return ADC_RAW_INVALID;
    }

    result = ADC0_SSFIFO2_R & 0xFFF;          /* Read result */
    ADC0_ISC_R = (1 << 2);                    /* Clear flag */
    return result;
}

/* ================= READ TEMP ================= */
uint32_t ADC_ReadTemperatureRaw(void)
{
    uint32_t raw_adc;

    ADC0_PSSI_R = (1 << 3);                   /* Initiate SS3 */

    if (!ADC_WaitForConversion(1 << 3))       /* Bounded wait for conversion */
    {
        return ADC_RAW_INVALID;
    }

    raw_adc = ADC0_SSFIFO3_R & 0xFFF;         /* Read result */
    ADC0_ISC_R = (1 << 3);                    /* Clear flag */
    return raw_adc;
}

int32_t ADC_ReadTemperatureTenths(void)
{
    return ADC_RawToTenths(ADC_ReadTemperatureRaw());
}

float ADC_ReadTemperatureSensor(void)
{
    return (float)ADC_ReadTemperatureTenths() / 10.0f;
}
