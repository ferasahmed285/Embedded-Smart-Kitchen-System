/**
 * @file switch.c
 * @brief GPIO and Interrupt configuration for system buttons and mode jumper wire.
 * @note Role 4 hardware integration. Uses Port F (PF4, PF0) and Port B (PB0).
 */
#include "switch.h"
#include "tasks.h" 
#include "inc/tm4c123gh6pm.h"

/* Pin Bitmasks */
#define PIN_SW2    (1 << 0) /* PF0 */
#define PIN_SW1    (1 << 4) /* PF4 */
#define SWITCH_PINS (PIN_SW1 | PIN_SW2)
#define PIN_MODE   (1 << 0) /* PB0 */

void Switch_Init(void)
{
    SYSCTL_RCGCGPIO_R |= 0x20 | 0x02; /* Enable Port F and Port B */
    for(volatile int i=0; i<1000; i++);

    /* 1. Configure Port F (Onboard SW1 and SW2) */
    GPIO_PORTF_LOCK_R = 0x4C4F434B;
    GPIO_PORTF_CR_R |= SWITCH_PINS;
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

    /* 2. Configure Port B (Mode Jumper Wire on PB0) */
    GPIO_PORTB_AMSEL_R &= ~PIN_MODE;
    GPIO_PORTB_PCTL_R &= ~0x0000000F;
    GPIO_PORTB_DIR_R &= ~PIN_MODE;
    GPIO_PORTB_AFSEL_R &= ~PIN_MODE;
    GPIO_PORTB_PUR_R |= PIN_MODE;
    GPIO_PORTB_DEN_R |= PIN_MODE;

    GPIO_PORTB_IS_R &= ~PIN_MODE;
    GPIO_PORTB_IBE_R |= PIN_MODE; /* Interrupt on BOTH edges (insert/remove) */
    GPIO_PORTB_ICR_R = PIN_MODE;
    GPIO_PORTB_IM_R |= PIN_MODE;

    /* Priority 5 for IRQ 30 (Port F) and IRQ 1 (Port B) */
    NVIC_PRI7_R = (NVIC_PRI7_R & 0xFF0FFFFF) | (5 << 21);
    NVIC_PRI0_R = (NVIC_PRI0_R & 0xFFFF0FFF) | (5 << 13);
    NVIC_EN0_R |= (1 << 30) | (1 << 1); 
}

void GPIOF_Handler(void)
{
    /* Debounce */
    for(volatile int i=0; i<50000; i++); 

    if (GPIO_PORTF_RIS_R & PIN_SW1) {
        Tasks_PostButtonEventFromISR(EVENT_TOGGLE_LIGHT);
        GPIO_PORTF_ICR_R = PIN_SW1;
    }
    if (GPIO_PORTF_RIS_R & PIN_SW2) {
        Tasks_PostButtonEventFromISR(EVENT_TOGGLE_OVEN);
        GPIO_PORTF_ICR_R = PIN_SW2;
    }
}

void GPIOB_Handler(void)
{
    /* Debounce insertion/removal of jumper wire */
    for(volatile int i=0; i<50000; i++); 
    
    if (GPIO_PORTB_RIS_R & PIN_MODE) {
        Tasks_PostButtonEventFromISR(EVENT_UPDATE_MODE);
        GPIO_PORTB_ICR_R = PIN_MODE;
    }
}
