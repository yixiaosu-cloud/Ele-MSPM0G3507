#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stdbool.h>

void UART_putchar(char c);
void UART_puts(const char *s);
void UART_println(const char *s);
void UART_printNum(uint32_t n);
void UART_printFloat(float f, int32_t decimals);

bool UART_DMA_send(const uint8_t *buf, uint16_t len);
bool UART_DMA_isBusy(void);
void UART_DMA_waitDone(void);

#endif
