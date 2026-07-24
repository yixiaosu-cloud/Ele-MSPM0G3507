#include "uart1.h"
#include "ti_msp_dl_config.h"
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/dl_dma.h>

static volatile bool g_uart1_dma_done = true;
static volatile bool g_uart1_tx_complete = true;
static void (*g_uart1_rx_cb)(uint8_t) = NULL;

void UART1_putchar(char c)
{
	DL_UART_Main_transmitDataBlocking(UART_1_INST, c);
}

void UART1_puts(const char *s)
{
	while (*s) {
		UART1_putchar(*s++);
	}
}

void UART1_println(const char *s)
{
	UART1_puts(s);
	UART1_puts("\r\n");
}

void UART1_printNum(uint32_t n)
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
	UART1_puts(p);
}

void UART1_printFloat(float f, int32_t decimals)
{
	int32_t int_part;

	if (f < 0.0f) {
		UART1_putchar('-');
		f = -f;
	}
	int_part = (int32_t)f;
	UART1_printNum((uint32_t)int_part);
	UART1_putchar('.');
	f -= (float)int_part;
	while (decimals-- > 0) {
		f *= 10.0f;
		UART1_putchar('0' + (uint8_t)f);
		f -= (float)(uint8_t)f;
	}
}

bool UART1_DMA_send(const uint8_t *buf, uint16_t len)
{
	if (g_uart1_dma_done == false || g_uart1_tx_complete == false) {
		return false;
	}
	g_uart1_dma_done   = false;
	g_uart1_tx_complete = false;
	DL_DMA_setSrcAddr(DMA, DMA_CH1_CHAN_ID, (uint32_t)buf);
	DL_DMA_setDestAddr(DMA, DMA_CH1_CHAN_ID,
		(uint32_t)(&UART_1_INST->TXDATA));
	DL_DMA_setTransferSize(DMA, DMA_CH1_CHAN_ID, len);
	DL_DMA_enableChannel(DMA, DMA_CH1_CHAN_ID);
	return true;
}

bool UART1_DMA_isBusy(void)
{
	return (g_uart1_dma_done == false || g_uart1_tx_complete == false);
}

void UART1_DMA_waitDone(void)
{
	while (UART1_DMA_isBusy()) {
		__WFE();
	}
}

void UART1_RX_setCallback(void (*cb)(uint8_t))
{
	g_uart1_rx_cb = cb;
}

void UART_1_INST_IRQHandler(void)
{
	switch (DL_UART_Main_getPendingInterrupt(UART_1_INST)) {
	case DL_UART_MAIN_IIDX_RX:
		if (g_uart1_rx_cb) {
			g_uart1_rx_cb((uint8_t)DL_UART_Main_receiveData(
				UART_1_INST));
		}
		break;
	case DL_UART_MAIN_IIDX_DMA_DONE_TX:
		g_uart1_dma_done = true;
		break;
	case DL_UART_MAIN_IIDX_EOT_DONE:
		g_uart1_tx_complete = true;
		break;
	default:
		break;
	}
}
