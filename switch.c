/**
 * @file switch.c
 * @brief GPIO and Interrupt configuration for system buttons.
 * @note Role 4 hardware integration. Uses Port F (PF4, PF0) onboard switches.
 */
#include "switch.h"
#include "tasks.h" 
#include "inc/tm4c123gh6pm.h"

/* Pin Bitmasks */
#define PIN_SW2    (1 << 0) /* PF0 (SW2) */
#define PIN_SW1    (1 << 4) /* PF4 (SW1) */
#define SWITCH_PINS (PIN_SW1 | PIN_SW2)

void Switch_Init(void)
{
    SYSCTL_RCGCGPIO_R |= 0x20; /* Enable Port F clock */
    for(volatile int i=0; i<1000; i++);

    /* Unlock PF0 for SW2 */
    GPIO_PORTF_LOCK_R = 0x4C4F434B;
    GPIO_PORTF_CR_R |= PIN_SW2;

    GPIO_PORTF_AMSEL_R &= ~SWITCH_PINS;
    GPIO_PORTF_PCTL_R &= ~0x000F000F;
    GPIO_PORTF_DIR_R &= ~SWITCH_PINS;
    GPIO_PORTF_AFSEL_R &= ~SWITCH_PINS;
    GPIO_PORTF_PUR_R |= SWITCH_PINS;
    GPIO_PORTF_DEN_R |= SWITCH_PINS;

    GPIO_PORTF_IS_R &= ~SWITCH_PINS;
    GPIO_PORTF_IBE_R &= ~SWITCH_PINS;
    GPIO_PORTF_IEV_R &= ~SWITCH_PINS; /* falling edge */
    GPIO_PORTF_ICR_R = SWITCH_PINS;
    GPIO_PORTF_IM_R |= SWITCH_PINS;

    /* Priority 5 for IRQ 30 (Port F). NVIC_PRI7_R bits 23:21 */
    NVIC_PRI7_R = (NVIC_PRI7_R & 0xFF0FFFFF) | (5 << 21);
    NVIC_EN0_R |= (1 << 30); /* Enable IRQ 30 */
}

void GPIOF_Handler(void)
{
    /* Wait briefly to allow human to press both buttons simultaneously */
    for(volatile int i=0; i<150000; i++); 

    /* Read actual physical state of the pins (active low) */
    uint32_t data = GPIO_PORTF_DATA_R;
    bool sw1_pressed = ((data & PIN_SW1) == 0);
    bool sw2_pressed = ((data & PIN_SW2) == 0);

    /* Simultaneous press logic */
    if (sw1_pressed && sw2_pressed) {
        Tasks_PostButtonEventFromISR(EVENT_TOGGLE_MODE);
    }
    else if (sw1_pressed) {
        Tasks_PostButtonEventFromISR(EVENT_TOGGLE_LIGHT);
    }
    else if (sw2_pressed) {
        Tasks_PostButtonEventFromISR(EVENT_TOGGLE_OVEN);
    }

    /* Clear all switch interrupts to prevent cascading triggers */
    GPIO_PORTF_ICR_R = SWITCH_PINS; 
}
