#include "uart.h"
#include "ti_msp_dl_config.h"
#include <ti/driverlib/driverlib.h>

void UART_putchar(char c)
{
	DL_UART_Main_transmitDataBlocking(UART_0_INST, c);
}

void UART_puts(const char *s)
{
	while (*s) {
		UART_putchar(*s++);
	}
}

void UART_println(const char *s)
{
	UART_puts(s);
	UART_puts("\r\n");
}

void UART_printNum(uint32_t n)
{
	char buf[12];
	char *p = buf + sizeof(buf) - 1;
	*p = '\0';
	if (n == 0) {
		*--p = '0';
	} else {
		while (n) {
			*--p = '0' + (n % 10);
			n /= 10;
		}
	}
	UART_puts(p);
}

void UART_printFloat(float f, int32_t decimals)
{
	int32_t int_part;

	if (f < 0.0f) {
		UART_putchar('-');
		f = -f;
	}
	int_part = (int32_t)f;
	UART_printNum((uint32_t)int_part);
	UART_putchar('.');
	f -= (float)int_part;
	while (decimals-- > 0) {
		f *= 10.0f;
		UART_putchar('0' + (uint8_t)f);
		f -= (float)(uint8_t)f;
	}
}

void UART_0_INST_IRQHandler(void)
{
	switch (DL_UART_Main_getPendingInterrupt(UART_0_INST)) {
	case DL_UART_MAIN_IIDX_RX:
		DL_UART_Main_transmitData(UART_0_INST,
			DL_UART_Main_receiveData(UART_0_INST));
		break;
	default:
		break;
	}
}
