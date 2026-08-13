/**
 * @file led.c
 * @brief GPIO driver for Kitchen Light and Oven Heating Element LEDs.
 * @note Role 4 hardware integration. Uses onboard Port F LEDs (PF1, PF3).
 */
#include "led.h"

/* ================= REGISTERS ================= */
#define SYSCTL_RCGCGPIO_R  (*((volatile unsigned long *)0x400FE608))
#define GPIO_PORTF_DATA_R  (*((volatile unsigned long *)0x400253FC))
#define GPIO_PORTF_DIR_R   (*((volatile unsigned long *)0x40025400))
#define GPIO_PORTF_AFSEL_R (*((volatile unsigned long *)0x40025420))
#define GPIO_PORTF_DEN_R   (*((volatile unsigned long *)0x4002551C))
#define GPIO_PORTF_AMSEL_R (*((volatile unsigned long *)0x40025528))
#define GPIO_PORTF_PCTL_R  (*((volatile unsigned long *)0x4002552C))

/* Pin Bitmasks */
#define OVEN_LED_PIN       (1 << 1)  /* PF1 (Red LED) */
#define LIGHT_LED_PIN      (1 << 3)  /* PF3 (Green LED) */

void LED_Init(void)
{
    SYSCTL_RCGCGPIO_R |= 0x20; /* Enable Port F clock */
    for(volatile int i=0; i<1000; i++);

    /* Note: Port F is also used by switch.c. Use |= and &= to not disturb PF0/PF4 */
    GPIO_PORTF_AMSEL_R &= ~(OVEN_LED_PIN | LIGHT_LED_PIN);
    GPIO_PORTF_PCTL_R  &= ~0x0000F0F0; /* Clear PCTL for PF1, PF3 */
    GPIO_PORTF_DIR_R   |=  (OVEN_LED_PIN | LIGHT_LED_PIN); /* Set as output */
    GPIO_PORTF_AFSEL_R &= ~(OVEN_LED_PIN | LIGHT_LED_PIN);
    GPIO_PORTF_DEN_R   |=  (OVEN_LED_PIN | LIGHT_LED_PIN);
    
    /* Ensure LEDs are initially OFF */
    GPIO_PORTF_DATA_R &= ~(OVEN_LED_PIN | LIGHT_LED_PIN);
}

void LED_SetKitchenLight(bool on)
{
    if (on)
    {
        GPIO_PORTF_DATA_R |= LIGHT_LED_PIN;
    }
    else
    {
        GPIO_PORTF_DATA_R &= ~LIGHT_LED_PIN;
    }
}

void LED_SetOvenElement(bool on)
{
    if (on)
    {
        GPIO_PORTF_DATA_R |= OVEN_LED_PIN;
    }
    else
    {
        GPIO_PORTF_DATA_R &= ~OVEN_LED_PIN;
    }
}
