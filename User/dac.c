#include "dac.h"
#include "ti_msp_dl_config.h"
#include <ti/driverlib/driverlib.h>
#include "uart.h"
#include "utils.h"
#include "dsp/Include/arm_math.h"

#define DAC_VREF_MV   2500
#define TABLE_BITS    8
#define TABLE_SIZE    (1U << TABLE_BITS)
#define TABLE_MASK    (TABLE_SIZE - 1)
#define PHASE_SHIFT   (32 - TABLE_BITS)

static uint16_t g_wave_table[TABLE_SIZE];
static uint32_t g_phase_acc;
static uint32_t g_phase_inc;

void DAC_SetVoltage(uint32_t mV)
{
	uint16_t code = (uint16_t)((mV * 4095UL) / DAC_VREF_MV);
	DL_DAC12_output12(DAC0, code);
}

void DAC_SetCode(uint16_t code)
{
	DL_DAC12_output12(DAC0, code);
}

void DAC_Disable(void)
{
	DL_DAC12_disable(DAC0);
}

/*
 * DDS 频率设定: phase_inc = freq_hz * 2^32 / 1e6 (采样率 1MHz)
 * 分辨率 = 1e6 / 2^32 ≈ 0.00023 Hz
 */
void DAC_SetFrequency(float freq_hz)
{
	g_phase_inc = (uint32_t)(freq_hz * 4294.967296f);
	g_phase_acc = 0;
}

void DAC_WaveConfig(float offset_mV, float amplitude_mV)
{
	int i;
	for (i = 0; i < (int)TABLE_SIZE; i++) {
		float s = arm_sin_f32(
			2.0f * 3.14159265f * (float)i / (float)TABLE_SIZE);
		g_wave_table[i] = (uint16_t)((offset_mV + s * amplitude_mV)
			* 4095.0f / (float)DAC_VREF_MV);
	}
}

void DAC_Demo(void)
{
	int i;

	UART_println("--- DAC Demo ---");
	UART_puts("VREF=2500mV, PA15=DAC_OUT\r\n");
	UART_puts("DDS mode, 1Msps, any freq (0.0002Hz res)\r\n");

	DAC_WaveConfig(1250.0f, 1000.0f);
	DAC_SetFrequency(1000.0f);

	UART_puts("table[0..7]: ");
	for (i = 0; i < 8; i++) {
		UART_printNum(g_wave_table[i]);
		UART_putchar(' ');
	}
	UART_puts("\r\n");
	UART_puts("freq=1000Hz, inc=");
	UART_printNum(g_phase_inc);
	UART_puts("\r\n");

	for (i = 0; i < 4; i++) {
		DAC_SetCode(g_wave_table[i]);
	}

	NVIC_EnableIRQ(DAC12_INT_IRQN);
	DL_DAC12_enable(DAC0);
	DL_TimerG_startCounter(TIMER_1_INST);

	UART_println("DAC running...");
	UART_println("--- DAC Demo End ---");
}

void DAC12_IRQHandler(void)
{
	switch (DL_DAC12_getPendingInterrupt(DAC0)) {
	case DL_DAC12_IIDX_FIFO_1_2_EMPTY:
		g_phase_acc += g_phase_inc;
		DAC_SetCode(g_wave_table[
			(g_phase_acc >> PHASE_SHIFT) & TABLE_MASK]);
		g_phase_acc += g_phase_inc;
		DAC_SetCode(g_wave_table[
			(g_phase_acc >> PHASE_SHIFT) & TABLE_MASK]);
		break;
	default:
		break;
	}
}
