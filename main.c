#include <ti/driverlib/m0p/dl_interrupt.h>
#include "ti_msp_dl_config.h"
#include <ti/driverlib/driverlib.h>
#include "dsp/Include/arm_math.h"

#include "utils.h"
#include "uart.h"
#include "uart1.h"
#include "dac.h"
#include "adc.h"

static volatile int32_t g_tick;

void TIMER_0_INST_IRQHandler(void)
{
	switch (DL_TimerG_getPendingInterrupt(TIMER_0_INST)) {
	case DL_TIMERG_IIDX_ZERO:
		g_tick++;
		DL_GPIO_togglePins(GPIOB, LED_GREEN_PIN);
		break;
	default:
		break;
	}
}

int main(void)
{
	SYSCFG_DL_init();

	NVIC_EnableIRQ(TIMER_0_INST_INT_IRQN);
	NVIC_EnableIRQ(UART_0_INST_INT_IRQN);
	NVIC_EnableIRQ(UART_1_INST_INT_IRQN);
	NVIC_EnableIRQ(ADC12_0_INST_INT_IRQN);

	DL_GPIO_clearPins(GPIOA, LED_LED1_PIN);

	DL_TimerG_startCounter(TIMER_0_INST);

	DL_SYSCTL_disableSleepOnExit();

	UART_println("=== MSPM0G3507 UART Debug ===");
	UART_println("PA10=TX, PA11=RX, 115200-8N1");

	UART1_println("=== MSPM0G3507 UART1 Test ===");
	UART1_println("PB6=TX, PB7=RX, 115200-8N1");
	UART1_puts("CPUCLK=");
	UART1_printNum(CPUCLK_FREQ);
	UART1_puts(" Hz\r\n");

	while (1) {
		if (Key_Read()) {
			DL_GPIO_togglePins(GPIOA, LED_LED1_PIN);
			UART_puts("[BTN] LED1 toggled, tick=");
			UART_printNum((uint32_t)g_tick);
			UART_puts("\r\n");
			UART1_puts("[BTN] tick=");
			UART1_printNum((uint32_t)g_tick);
			UART1_puts("\r\n");
		}
		if ((g_tick % 5) == 0) {
			static uint32_t last_tick;
			if (g_tick != last_tick) {
				last_tick = (uint32_t)g_tick;
				UART1_puts("tick=");
				UART1_printNum((uint32_t)g_tick);
				UART1_puts("\r\n");
			}
		}
	}
}
