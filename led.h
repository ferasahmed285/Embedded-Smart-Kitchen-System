#ifndef LED_H
#define LED_H
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initializes the GPIO pins used for the Oven and Kitchen Light indicators.
 */
void LED_Init(void);

/**
 * @brief Turns the Kitchen Light indicator ON or OFF.
 */
void LED_SetKitchenLight(bool on);

/**
 * @brief Turns the Oven Heating Element indicator ON or OFF.
 */
void LED_SetOvenElement(bool on);

#endif // LED_H
