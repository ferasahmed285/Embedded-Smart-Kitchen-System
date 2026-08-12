#ifndef SWITCH_H
#define SWITCH_H
#include <stdint.h>
#include <stdbool.h>

/* 
 * Initializes Port F (PF4 and PF0) as interrupt-driven inputs for manual overrides. 
 * Configures Priority 5 to allow safe interaction with FreeRTOS ISR APIs.
 */
void Switch_Init(void);

/* Helper functions to check if an override is active */
bool Switch_IsKitchenOverrideActive(void);
bool Switch_IsOvenOverrideActive(void);

#endif // SWITCH_H
