#ifndef UART1_H
#define UART1_H

#include <stdint.h>
#include <stdbool.h>

void UART1_putchar(char c);
void UART1_puts(const char *s);
void UART1_println(const char *s);
void UART1_printNum(uint32_t n);
void UART1_printFloat(float f, int32_t decimals);

bool UART1_DMA_send(const uint8_t *buf, uint16_t len);
bool UART1_DMA_isBusy(void);
void UART1_DMA_waitDone(void);

#endif
