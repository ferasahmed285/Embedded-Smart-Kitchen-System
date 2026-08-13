/**
 * @file switch.h
 * @brief Port F GPIO Driver for Onboard Switches (SW1 and SW2)
 * @note Role 4 hardware integration.
 */
#ifndef SWITCH_H
#define SWITCH_H
#include <stdint.h>
#include <stdbool.h>

/* Pin masks within the Port F data register. Both switches are active low. */
#define SWITCH_SW1_PIN   (1u << 4)   /* PF4 - Kitchen light override  */
#define SWITCH_SW2_PIN   (1u << 0)   /* PF0 - Oven override           */
#define SWITCH_ALL_PINS  (SWITCH_SW1_PIN | SWITCH_SW2_PIN)

/* Configures PF0/PF4 as pulled-up inputs with falling edge interrupts. */
void Switch_Init(void);

/* Returns the raw Port F switch bits. A 0 bit means that switch is pressed. */
uint32_t Switch_Read(void);

/* True if the switch identified by the given pin mask is currently pressed. */
bool Switch_IsPressed(uint32_t pinMask, uint32_t snapshot);

/*
 * Re-arms the switch interrupts after the override task has finished
 * processing an event. The ISR masks them on entry so that contact bounce
 * cannot flood the RTOS objects with hundreds of spurious events.
 */
void Switch_EnableInterrupts(void);

#endif // SWITCH_H
