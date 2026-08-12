#ifndef LED_H
#define LED_H
#include <stdint.h>
#include <stdbool.h>

void LED_Init(void);
void LED_SetKitchenLight(bool state);
void LED_SetOvenHeatingElement(bool state);

#endif // LED_H
