#include "utils.h"
#include "ti_msp_dl_config.h"
#include <ti/driverlib/driverlib.h>
#include "uart.h"
#include "dsp/Include/arm_math.h"

void Delay_ms(uint32_t ms)
{
	for (; ms > 0; ms--) {
		delay_cycles(32000);
	}
}

/* 按键状态机：0=空闲 1=消抖确认 2=等待释放 */
uint8_t Key_Read(void)
{
	static uint8_t state = 0;
	uint8_t cur = DL_GPIO_readPins(KEY_PORT, KEY_S2_PIN) > 0;

	switch (state) {
	case 0:
		if (!cur) state = 1;
		break;
	case 1:
		Delay_ms(20);
		if (DL_GPIO_readPins(KEY_PORT, KEY_S2_PIN)) {
			state = 0;
		} else {
			state = 2;
		}
		break;
	case 2:
		if (cur) {
			Delay_ms(20);
			if (DL_GPIO_readPins(KEY_PORT, KEY_S2_PIN)) {
				state = 0;
				return 1;
			}
		}
		break;
	}
	return 0;
}

#define DSP_N 64
static float32_t g_dsp_buf[DSP_N];

void DSP_Demo(void)
{
	float32_t mean, rms, min, max;
	uint32_t  minIdx, maxIdx;
	int       i;

	UART_println("--- DSP Demo ---");

	/* 生成信号: 1.5*sin(2pi*i/N) + 2.0 */
	for (i = 0; i < DSP_N; i++) {
		g_dsp_buf[i] = 1.5f * arm_sin_f32(
			2.0f * 3.14159265f * (float)i / (float)DSP_N) + 2.0f;
	}

	UART_puts("Signal[0..7]: ");
	for (i = 0; i < 8; i++) {
		UART_printFloat(g_dsp_buf[i], 3);
		UART_putchar(' ');
	}
	UART_puts("\r\n");

	/* 统计 */
	arm_mean_f32(g_dsp_buf, DSP_N, &mean);
	arm_rms_f32 (g_dsp_buf, DSP_N, &rms);
	arm_min_f32 (g_dsp_buf, DSP_N, &min,  &minIdx);
	arm_max_f32 (g_dsp_buf, DSP_N, &max,  &maxIdx);

	UART_puts("mean=");  UART_printFloat(mean, 4); UART_puts("\r\n");
	UART_puts("rms=");   UART_printFloat(rms,  4); UART_puts("\r\n");
	UART_puts("min=");   UART_printFloat(min,  4);
	UART_puts(" @idx="); UART_printNum(minIdx);    UART_puts("\r\n");
	UART_puts("max=");   UART_printFloat(max,  4);
	UART_puts(" @idx="); UART_printNum(maxIdx);    UART_puts("\r\n");

	/* 三角函数演示 */
	UART_println("Trig sin/cos:");
	{
		float    ang[] = {0.5236f, 0.7854f, 1.0472f, 1.5708f};
		const char *lab[] = {"30", "45", "60", "90"};
		for (i = 0; i < 4; i++) {
			UART_puts(lab[i]); UART_puts("deg: sin=");
			UART_printFloat(arm_sin_f32(ang[i]), 4);
			UART_puts(" cos=");
			UART_printFloat(arm_cos_f32(ang[i]), 4);
			UART_puts("\r\n");
		}
	}

	/* FIR 低通滤波演示: 对台阶信号滤波 */
	{
		float32_t coeff[5] = {0.2f, 0.2f, 0.2f, 0.2f, 0.2f};
		float32_t state[5 + DSP_N];
		float32_t in[DSP_N], out[DSP_N];
		arm_fir_instance_f32 fir;

		for (i = 0; i < DSP_N; i++) {
			in[i] = (i < DSP_N / 2) ? 0.0f : 3.0f;
		}
		arm_fir_init_f32(&fir, 5, coeff, state, DSP_N);
		arm_fir_f32(&fir, in, out, DSP_N);

		UART_println("FIR: step@0->3, 5-tap avg, out[30..34]:");
		for (i = 30; i < 35; i++) {
			UART_printFloat(out[i], 4);
			UART_putchar(' ');
		}
		UART_puts("\r\n");
	}

	UART_println("--- DSP Demo End ---");
}
