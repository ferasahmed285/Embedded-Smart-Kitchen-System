#ifndef UART_H
#define UART_H

/* Initializes UART0 for 9600 baud rate on PA0 and PA1 */
void UART_Init(void);

/* Transmits a single character over UART0 */
void UART_WriteChar(char c);

/* Transmits a null-terminated string over UART0 */
void UART_SendString(const char* text);

#endif // UART_H
