#include <ti/driverlib/m0p/dl_interrupt.h>
#include "ti_msp_dl_config.h"
#include "ti/driverlib/driverlib.h"
#include "dsp/Include/arm_math.h"

void Delay_ms(uint32_t ms)
{
	for (; ms > 0; ms--) {
		delay_cycles(32000);
	}
}

uint8_t Key_Read(void)
{
	if (DL_GPIO_readPins(KEY_PORT, KEY_LB2_PIN)) {
		return 0;
	}
	Delay_ms(20);
	if (DL_GPIO_readPins(KEY_PORT, KEY_LB2_PIN)) {
		return 0;
	}
	while (!DL_GPIO_readPins(KEY_PORT, KEY_LB2_PIN)) {
	}
	return 1;
}

void TIMER_0_INST_IRQHandler(void)
{
	switch (DL_TimerG_getPendingInterrupt(TIMER_0_INST)) {
	case DL_TIMERG_IIDX_ZERO:
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

	DL_GPIO_clearPins(GPIOA, LED_LED1_PIN);
	DL_GPIO_setPins(GPIOB, LED_BLUE_PIN);

	DL_TimerG_startCounter(TIMER_0_INST);

	while (1) {
		if (Key_Read()) {
			DL_GPIO_togglePins(GPIOA, LED_LED1_PIN);
		}
	}
}
