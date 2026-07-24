#include "adc.h"
#include "ti_msp_dl_config.h"
#include <ti/driverlib/driverlib.h>
#include "uart.h"
#include "utils.h"

#define ADC_VREF_MV 2500

static volatile uint16_t g_adc_raw;
static volatile uint8_t  g_adc_done;

uint16_t ADC_ReadRaw(void)
{
	DL_ADC12_startConversion(ADC12_0_INST);

	g_adc_done = 0;
	while (!g_adc_done) {
	}
	DL_ADC12_enableConversions(ADC12_0_INST);
	return g_adc_raw;
}

uint16_t ADC_Read_mV(void)
{
	uint16_t raw = ADC_ReadRaw();
	return (uint16_t)(((uint32_t)raw * ADC_VREF_MV) / 4095);
}

void ADC_Demo(void)
{
	int i;

	UART_println("--- ADC Demo ---");
	UART_puts("ADC0 CH2=PA25, VREF=2.5V\r\n");
	UART_puts("Sampling 10x...\r\n");

	for (i = 0; i < 10; i++) {
		uint16_t raw = ADC_ReadRaw();
		uint16_t mV  = (uint16_t)(((uint32_t)raw * ADC_VREF_MV) / 4095);
		UART_puts("  "); UART_printNum((uint32_t)i);
		UART_puts(": raw="); UART_printNum(raw);
		UART_puts(" -> ");   UART_printNum(mV);
		UART_puts("mV\r\n");
		Delay_ms(100);
	}

	UART_println("--- ADC Demo End ---");
}

void ADC12_0_INST_IRQHandler(void)
{
	switch (DL_ADC12_getPendingInterrupt(ADC12_0_INST)) {
	case DL_ADC12_IIDX_MEM0_RESULT_LOADED:
		g_adc_raw = DL_ADC12_getMemResult(ADC12_0_INST, DL_ADC12_MEM_IDX_0);
		g_adc_done = 1;
		break;
	default:
		break;
	}
}
