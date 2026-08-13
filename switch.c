/**
 * @file switch.c
 * @brief GPIO and Interrupt configuration for the manual override switches.
 * @note Role 4 hardware integration. Uses Port F (PF4/SW1, PF0/SW2).
 *
 * Registers are declared locally, in the same bare metal style as adc.c,
 * led.c and uart.c. The file previously included <inc/tm4c123gh6pm.h>, which
 * made the whole project unbuildable unless TivaWare happened to be installed
 * at the exact path hard coded in the uVision include list.
 */
#include "switch.h"
#include "tasks.h"

/* ================= REGISTERS ================= */
#define SYSCTL_RCGCGPIO_R  (*((volatile unsigned long *)0x400FE608))

#define GPIO_PORTF_DATA_R  (*((volatile unsigned long *)0x400253FC))
#define GPIO_PORTF_DIR_R   (*((volatile unsigned long *)0x40025400))
#define GPIO_PORTF_IS_R    (*((volatile unsigned long *)0x40025404))
#define GPIO_PORTF_IBE_R   (*((volatile unsigned long *)0x40025408))
#define GPIO_PORTF_IEV_R   (*((volatile unsigned long *)0x4002540C))
#define GPIO_PORTF_IM_R    (*((volatile unsigned long *)0x40025410))
#define GPIO_PORTF_ICR_R   (*((volatile unsigned long *)0x4002541C))
#define GPIO_PORTF_AFSEL_R (*((volatile unsigned long *)0x40025420))
#define GPIO_PORTF_PUR_R   (*((volatile unsigned long *)0x40025510))
#define GPIO_PORTF_DEN_R   (*((volatile unsigned long *)0x4002551C))
#define GPIO_PORTF_LOCK_R  (*((volatile unsigned long *)0x40025520))
#define GPIO_PORTF_CR_R    (*((volatile unsigned long *)0x40025524))
#define GPIO_PORTF_AMSEL_R (*((volatile unsigned long *)0x40025528))
#define GPIO_PORTF_PCTL_R  (*((volatile unsigned long *)0x4002552C))

#define NVIC_EN0_R         (*((volatile unsigned long *)0xE000E100))
#define NVIC_PRI7_R        (*((volatile unsigned long *)0xE000E41C))

#define GPIO_PORTF_LOCK_KEY 0x4C4F434B

void Switch_Init(void)
{
    volatile int delay;

    SYSCTL_RCGCGPIO_R |= 0x20; /* Enable Port F clock */
    for (delay = 0; delay < 1000; delay++)
    {
    }

    /* PF0 is locked as the NMI pin out of reset and must be unlocked. */
    GPIO_PORTF_LOCK_R = GPIO_PORTF_LOCK_KEY;
    GPIO_PORTF_CR_R |= SWITCH_ALL_PINS;

    GPIO_PORTF_AMSEL_R &= ~SWITCH_ALL_PINS;
    GPIO_PORTF_PCTL_R  &= ~0x000F000F;
    GPIO_PORTF_DIR_R   &= ~SWITCH_ALL_PINS;   /* Inputs                     */
    GPIO_PORTF_AFSEL_R &= ~SWITCH_ALL_PINS;
    GPIO_PORTF_PUR_R   |=  SWITCH_ALL_PINS;   /* Internal pull-ups          */
    GPIO_PORTF_DEN_R   |=  SWITCH_ALL_PINS;

    GPIO_PORTF_IS_R  &= ~SWITCH_ALL_PINS;     /* Edge sensitive             */
    GPIO_PORTF_IBE_R &= ~SWITCH_ALL_PINS;     /* Single edge                */
    GPIO_PORTF_IEV_R &= ~SWITCH_ALL_PINS;     /* Falling edge (press)       */
    GPIO_PORTF_ICR_R  =  SWITCH_ALL_PINS;     /* Clear any stale flags      */
    GPIO_PORTF_IM_R  |=  SWITCH_ALL_PINS;     /* Unmask                     */

    /*
     * Port F is IRQ 30, whose priority field lives in NVIC_PRI7 bits 23:21.
     * Priority 5 is numerically equal to configMAX_SYSCALL_INTERRUPT_PRIORITY
     * (5 << 5), which is the highest priority from which a FreeRTOS
     * "FromISR" API call may legally be made.
     */
    NVIC_PRI7_R = (NVIC_PRI7_R & 0xFF0FFFFF) | (5 << 21);
    NVIC_EN0_R |= (1 << 30);
}

uint32_t Switch_Read(void)
{
    return GPIO_PORTF_DATA_R & SWITCH_ALL_PINS;
}

bool Switch_IsPressed(uint32_t pinMask, uint32_t snapshot)
{
    /* Active low: the pull-up holds the line high until the button shorts it. */
    return ((snapshot & pinMask) == 0u);
}

void Switch_EnableInterrupts(void)
{
    GPIO_PORTF_ICR_R = SWITCH_ALL_PINS;  /* Discard edges seen while masked */
    GPIO_PORTF_IM_R |= SWITCH_ALL_PINS;
}

/**
 * @brief Port F interrupt service routine.
 *
 * The handler does the minimum possible work: it masks further switch
 * interrupts, clears the flag, and hands a snapshot of the pins to the
 * override task. All debouncing, classification and logging happen in that
 * task at priority 3.
 *
 * The previous implementation spun in a 150000 iteration delay loop inside the
 * ISR to wait for a second button press. At 16 MHz that stalled every task and
 * the kernel tick for several milliseconds per press, which is exactly what an
 * RTOS design is supposed to avoid.
 */
void GPIOF_Handler(void)
{
    uint32_t snapshot = GPIO_PORTF_DATA_R & SWITCH_ALL_PINS;

    GPIO_PORTF_IM_R &= ~SWITCH_ALL_PINS;   /* Masked until the task re-arms */
    GPIO_PORTF_ICR_R = SWITCH_ALL_PINS;

    Tasks_PostSwitchEventFromISR(snapshot);
}
