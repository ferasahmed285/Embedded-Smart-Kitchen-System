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

/* GPIO E for AIN0 (PE3) and PE1 (Disconnect Simulator) */
#define GPIO_PORTE_DATA_R  (*((volatile unsigned long *)0x400243FC))
#define GPIO_PORTE_DIR_R   (*((volatile unsigned long *)0x40024400))
#define GPIO_PORTE_AFSEL_R (*((volatile unsigned long *)0x40024420))
#define GPIO_PORTE_PUR_R   (*((volatile unsigned long *)0x40024510))
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
 * finishes in a few microseconds, so reaching this count means the peripheral
 * is faulty. Bounding the wait stops a dead ADC from hanging the calling task
 * forever, which an unbounded 'while' would do.
 */
#define ADC_WAIT_LIMIT  100000u

/* ================= TEMP CONVERT ================= */

/*
 * Tiva C internal temperature sensor, from the TM4C123 datasheet:
 *   Temp(C)     = 147.5 - (75 * 3.3 * ADC) / 4096
 *   Temp(0.1 C) = 1475  - (2475 * ADC) / 4096
 *
 * Note the negative slope: a HIGHER ADC code means a LOWER temperature. The
 * integer form is used by the oven control task so its safety comparisons do
 * not depend on floating point rounding. At the maximum code of 4095 the
 * intermediate product is about 10.1 million, well inside int32.
 */
static int32_t ConvertTempTenths(uint32_t adc)
{
    return 1475 - (int32_t)((2475u * adc) / 4096u);
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

    /* 2. Configure PE3 (AIN0) for analog input (Light Sensor) */
    GPIO_PORTE_DIR_R &= ~0x08;     
    GPIO_PORTE_AFSEL_R |= 0x08;    
    GPIO_PORTE_DEN_R &= ~0x08;     
    GPIO_PORTE_AMSEL_R |= 0x08;    

    /* 2b. Configure PE1 as a digital input with pull-up to simulate sensor disconnect */
    GPIO_PORTE_DIR_R &= ~(1 << 1);    /* PE1 as input */
    GPIO_PORTE_AFSEL_R &= ~(1 << 1);  /* Disable alt function on PE1 */
    GPIO_PORTE_AMSEL_R &= ~(1 << 1);  /* Disable analog on PE1 */
    GPIO_PORTE_PUR_R |= (1 << 1);     /* Enable pull-up resistor on PE1 */
    GPIO_PORTE_DEN_R |= (1 << 1);     /* Enable digital I/O on PE1 */

    /* 2c. Configure PE5 as a digital input with pull-up to mock the LDR */
    GPIO_PORTE_DIR_R &= ~(1 << 5);    /* PE5 as input */
    GPIO_PORTE_AFSEL_R &= ~(1 << 5);  /* Disable alt function on PE5 */
    GPIO_PORTE_AMSEL_R &= ~(1 << 5);  /* Disable analog on PE5 */
    GPIO_PORTE_PUR_R |= (1 << 5);     /* Enable pull-up resistor on PE5 */
    GPIO_PORTE_DEN_R |= (1 << 5);     /* Enable digital I/O on PE5 */

    /* 3. Disable SS2 and SS3 before config */
    ADC0_ACTSS_R &= ~((1 << 2) | (1 << 3));
    
    /* 4. Software trigger for both (default 0x0 in EMUX) */
    ADC0_EMUX_R &= ~0xFF00;

    /* 5. Config SS3 for Internal Temperature Sensor */
    ADC0_SSMUX3_R = 0;
    ADC0_SSCTL3_R = (1<<1) | (1<<2) | (1<<3); /* IE0, END0, TS0 */

    /* 6. Config SS2 for LDR/Light Sensor (AIN0) */
    ADC0_SSMUX2_R = 0;               /* Channel 0 (AIN0/PE3) */
    ADC0_SSCTL2_R = (1<<1) | (1<<2); /* IE0, END0 */

    /* 7. Re-enable SS2 and SS3 */
    ADC0_ACTSS_R |= (1 << 2) | (1 << 3);
}

/* ================= READ LIGHT ================= */

/*
 * TO USE THE REAL LDR SENSOR: 
 * Delete the "MOCK SENSOR" function below, and uncomment this real one.
 * You will need to build a voltage divider with the LDR and a 10k resistor on PE3.
 */
#if 0
uint32_t ADC_ReadLightSensor(void)
{
    uint32_t result;

    ADC0_PSSI_R = (1 << 2);                   /* Initiate SS2 */

    if (!ADC_WaitForConversion(1 << 2))       /* Bounded wait for conversion */
    {
        /*
         * Report a rail value so the lighting task's existing disconnect
         * check treats a dead ADC as a sensor fault rather than as darkness.
         */
        return 0u;
    }

    result = ADC0_SSFIFO2_R & 0xFFF;          /* Read result */
    ADC0_ISC_R = (1 << 2);                    /* Clear flag */
    return result;
}
#endif

/*
 * MOCK SENSOR (ACTIVE): 
 * Since there is no 10k resistor available, we use a digital jumper wire on PE5.
 * Connected to GND (LOW) -> Simulates a dark room (returns 3000, avoids fault)
 * Unplugged (HIGH)       -> Simulates a bright room (returns 500, avoids fault)
 */
uint32_t ADC_ReadLightSensor(void)
{
    if ((GPIO_PORTE_DATA_R & (1 << 5)) == 0) {
        return 3000; /* Dark */
    } else {
        return 500;  /* Bright */
    }
}

/* ================= READ TEMP ================= */
int32_t ADC_ReadTemperatureTenths(void)
{
    uint32_t raw_adc;

    /* Simulate a sensor disconnect if the jumper wire is pulled out of PE1. */
    if (GPIO_PORTE_DATA_R & (1 << 1))
    {
        /* Wire pulled out: PE1 is HIGH because of the pull-up. */
        return ADC_TEMP_TENTHS_INVALID;
    }

    ADC0_PSSI_R = (1 << 3);                   /* Initiate SS3 */

    if (!ADC_WaitForConversion(1 << 3))       /* Bounded wait for conversion */
    {
        return ADC_TEMP_TENTHS_INVALID;
    }

    raw_adc = ADC0_SSFIFO3_R & 0xFFF;         /* Read result */
    ADC0_ISC_R = (1 << 3);                    /* Clear flag */

    return ConvertTempTenths(raw_adc);
}

float ADC_ReadTemperatureSensor(void)
{
    int32_t tenths = ADC_ReadTemperatureTenths();

    if (tenths == ADC_TEMP_TENTHS_INVALID)
    {
        return ADC_TEMP_FAULT_C;
    }

    return (float)tenths / 10.0f;
}
