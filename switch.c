/**
 * @file switch.c
 * @brief GPIO and Interrupt configuration for manual override switches.
 * @note Role 4 hardware integration. Uses PF4 and PF0.
 */
#include "switch.h"
#include "tasks.h" 



/* ================= STATE ================= */
static OverrideMode_t lightMode = OVERRIDE_AUTO;
static OverrideMode_t ovenMode = OVERRIDE_AUTO;

/* ================= INIT SWITCH ================= */
void Switch_Init(void)
{
    SYSCTL_RCGCGPIO_R |= 0x20; /* Enable Port F clock */
    for(volatile int i=0; i<1000; i++);

    GPIO_PORTF_LOCK_R = 0x4C4F434B;   /* Unlock Port F Commit register (for PF0) */
    GPIO_PORTF_CR_R |= 0x11;          /* Allow changes to PF4 and PF0 */

    GPIO_PORTF_AMSEL_R &= ~0x11;      /* Disable analog on PF4, PF0 */
    GPIO_PORTF_PCTL_R &= ~0x000F000F; /* GPIO clear bit PCTL for PF4, PF0 */
    GPIO_PORTF_DIR_R &= ~0x11;        /* PF4, PF0 are inputs */
    GPIO_PORTF_AFSEL_R &= ~0x11;      /* Disable alt funct on PF4, PF0 */
    GPIO_PORTF_PUR_R |= 0x11;         /* Enable pull-up on PF4, PF0 */
    GPIO_PORTF_DEN_R |= 0x11;         /* Enable digital I/O on PF4, PF0 */

    /* Configure Interrupts */
    GPIO_PORTF_IS_R &= ~0x11;         /* PF4, PF0 is edge-sensitive */
    GPIO_PORTF_IBE_R &= ~0x11;        /* PF4, PF0 is not both edges */
    GPIO_PORTF_IEV_R &= ~0x11;        /* PF4, PF0 falling edge event (button press) */
    GPIO_PORTF_ICR_R = 0x11;          /* Clear flag4 and flag0 */
    GPIO_PORTF_IM_R |= 0x11;          /* Arm interrupt on PF4 and PF0 */

    /* NVIC Config for Port F (IRQ 30) */
    /* Priority 5 to be safe with FreeRTOS syscalls */
    NVIC_PRI7_R = (NVIC_PRI7_R & 0xFF00FFFF) | (5 << 21);
    
    /* Enable interrupt 30 in NVIC */
    NVIC_EN0_R |= (1 << 30);
}

/* ================= GETTERS ================= */
bool Switch_IsKitchenOverrideActive(void)
{
    return (lightMode != OVERRIDE_AUTO);
}

bool Switch_IsOvenOverrideActive(void)
{
    return (ovenMode != OVERRIDE_AUTO);
}

/* ================= ISR ================= */
void GPIOF_Handler(void)
{
    /* Simple software debounce delay */
    for(volatile int i=0; i<50000; i++);

    /* Check PF4 - Kitchen Light Switch */
    if (GPIO_PORTF_RIS_R & 0x10)
    {
        GPIO_PORTF_ICR_R = 0x10; /* Acknowledge flag4 */

        /* Cycle state: AUTO -> ON -> OFF -> AUTO */
        if (lightMode == OVERRIDE_AUTO) lightMode = OVERRIDE_FORCE_ON;
        else if (lightMode == OVERRIDE_FORCE_ON) lightMode = OVERRIDE_FORCE_OFF;
        else lightMode = OVERRIDE_AUTO;

        /* Send to RTOS */
        Tasks_PostOverrideFromISR(OVERRIDE_LIGHT, lightMode);
    }

    /* Check PF0 - Oven Switch */
    if (GPIO_PORTF_RIS_R & 0x01)
    {
        GPIO_PORTF_ICR_R = 0x01; /* Acknowledge flag0 */

        /* Cycle state: AUTO -> ON -> OFF -> AUTO */
        if (ovenMode == OVERRIDE_AUTO) ovenMode = OVERRIDE_FORCE_ON;
        else if (ovenMode == OVERRIDE_FORCE_ON) ovenMode = OVERRIDE_FORCE_OFF;
        else ovenMode = OVERRIDE_AUTO;

        /* Send to RTOS */
        Tasks_PostOverrideFromISR(OVERRIDE_OVEN, ovenMode);
    }
}
