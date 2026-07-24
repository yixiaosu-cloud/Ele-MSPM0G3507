#include "app_screen.h"
#include "tjc_usart_hmi.h"
#include "uart.h"
#include "utils.h"
#include "dsp/Include/arm_math.h"
#include <string.h>

static volatile int32_t g_n_freq;
static volatile int32_t g_n_vpp;
static volatile int32_t g_sys0;

#define SCREEN_W  460
#define SCREEN_H  170
#define WAVE_POINTS 256
static uint8_t wave_buf[WAVE_POINTS];

static void draw_waveform(void)
{
	uint8_t  mode = (uint8_t)g_sys0;
	uint16_t freq = (uint16_t)g_n_freq;
	int32_t  vpp  = g_n_vpp;
	uint8_t  center = SCREEN_H / 2;
	uint16_t disp_cycles = freq;
	float32_t pi2 = 2.0f * 3.14159265f;

	if (disp_cycles > 20) disp_cycles = 20;

	uint8_t half_amp = 0;
	if (vpp > 0) {
		half_amp = (uint8_t)(((uint32_t)vpp * (SCREEN_H / 2)) / 3300);
		if (half_amp == 0) half_amp = 1;
	}

	if (freq == 0 || half_amp == 0) {
		memset(wave_buf, center, WAVE_POINTS);
	} else {
		for (uint16_t i = 0; i < WAVE_POINTS; i++) {
			float32_t phase = (float32_t)i * (float32_t)disp_cycles
				/ (float32_t)WAVE_POINTS;
			float32_t norm;

			switch (mode) {
			case 0:
				norm = (arm_sin_f32(pi2 * phase) + 1.0f) / 2.0f;
				break;
			case 1: {
				float32_t t = phase -
					(float32_t)(int32_t)phase;
				t = (t < 0.5f) ? 2.0f * t : 2.0f * (1.0f - t);
				norm = 1.0f - t;
				break;
			}
			case 2:
				norm = phase - (float32_t)(int32_t)phase;
				break;
			case 3:
			default: {
				float32_t frac = phase -
					(float32_t)(int32_t)phase;
				norm = (frac < 0.5f) ? 1.0f : 0.0f;
				break;
			}
			}

			int32_t val = (int32_t)((float32_t)center +
				(norm - 0.5f) * 2.0f * (float32_t)half_amp);
			if (val < 0)         val = 0;
			if (val > SCREEN_H)  val = SCREEN_H;
			wave_buf[i] = (uint8_t)val;
		}
	}

	tjc_wave_cle("s0", 0);
	Delay_ms(5);
	tjc_wave_addt("s0", 0, wave_buf, WAVE_POINTS);

	if (freq == 0 || half_amp == 0) {
		tjc_set_text("t_title", "No Signal");
	} else {
		tjc_set_text("t_title", "MSPM0 Ready");
	}

	UART_puts("[SCREEN] waveform sent: mode=");
	UART_printNum(mode);
	UART_puts(" freq=");
	UART_printNum(freq);
	UART_puts(" vpp=");
	UART_printNum((uint32_t)vpp);
	UART_puts(" amp=");
	UART_printNum(half_amp);
	UART_puts(" cycles=");
	UART_printNum(disp_cycles);
	UART_puts("\r\n");
}

void tjc_on_touch(uint8_t page, uint8_t id, uint8_t event)
{
	UART_puts("[TJC][TOUCH] page="); UART_printNum(page);
	UART_puts(" id="); UART_printNum(id);
	UART_puts(event ? " press\r\n" : " release\r\n");

	if (page == 2 && id == 4 && event == 0) {
		UART_puts("[APP] OK pressed -> reading n_freq, n_vpp\r\n");
		tjc_get_number("n_freq.val", (int32_t *)&g_n_freq);
		tjc_get_number("n_vpp.val",  (int32_t *)&g_n_vpp);
	}
}

void tjc_on_frame(uint8_t cmd, const uint8_t *data, uint16_t len)
{
	if (cmd == 0x01 && len == 5) {
		uint16_t freq = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
		uint16_t vpp  = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
		uint8_t  mode = data[4];
		g_n_freq = freq;
		g_n_vpp  = vpp;
		g_sys0   = mode;
		UART_puts("[APP] mode_change: freq=");
		UART_printNum(freq);
		UART_puts(" vpp="); UART_printNum(vpp);
		UART_puts(" sys0="); UART_printNum(mode);
		UART_puts("\r\n");
		draw_waveform();
	} else {
		UART_puts("[TJC][FRAME] cmd=0x");
		{
			char hex = (char)((cmd >> 4) < 10 ?
				'0' + (cmd >> 4) : 'A' + (cmd >> 4) - 10);
			UART_putchar(hex);
			hex = (char)((cmd & 0x0F) < 10 ?
				'0' + (cmd & 0x0F) : 'A' + (cmd & 0x0F) - 10);
			UART_putchar(hex);
		}
		UART_puts(" len="); UART_printNum(len);
		UART_puts(" data=");
		for (uint16_t i = 0; i < len; i++) {
			uint8_t b = data[i];
			char hex = (char)((b >> 4) < 10 ?
				'0' + (b >> 4) : 'A' + (b >> 4) - 10);
			UART_putchar(hex);
			hex = (char)((b & 0x0F) < 10 ?
				'0' + (b & 0x0F) : 'A' + (b & 0x0F) - 10);
			UART_putchar(hex);
			UART_putchar(' ');
		}
		UART_puts("\r\n");
	}
}

void tjc_on_number(int32_t value, int32_t *dst)
{
	if (dst == (int32_t *)&g_n_freq) {
		UART_puts("[APP] n_freq = ");
		UART_printNum((uint32_t)value);
		UART_puts("\r\n");
	} else if (dst == (int32_t *)&g_n_vpp) {
		UART_puts("[APP] n_vpp  = ");
		UART_printNum((uint32_t)value);
		UART_puts("\r\n");
	} else if (dst == (int32_t *)&g_sys0) {
		UART_puts("[APP] sys0   = ");
		UART_printNum((uint32_t)value);
		UART_puts(" -> drawing waveform\r\n");
		draw_waveform();
	} else {
		UART_puts("[TJC][NUM] value=");
		UART_printNum((uint32_t)value);
		UART_puts("\r\n");
	}
}

void app_screen_init(void)
{
	tjc_ringbuf_init();
	UART1_RX_setCallback(tjc_ringbuf_write);
	tjc_send_raw("bkcmd=0");
	Delay_ms(100);

	UART_println("System boot, TJC screen on UART1");
	tjc_set_page("main");
	tjc_set_text("t_title", "MSPM0 Ready");
}

void app_screen_poll(void)
{
	tjc_parse_poll();
}

void app_screen_auto_scale(void)
{
	UART_println("[APP] AUTO: auto-scale waveform");
	g_n_freq = 5;
	g_n_vpp  = 2800;
	tjc_set_val("n_freq", g_n_freq);
	tjc_set_val("n_vpp",  g_n_vpp);
	draw_waveform();
}

void app_screen_draw(void)
{
	draw_waveform();
}
