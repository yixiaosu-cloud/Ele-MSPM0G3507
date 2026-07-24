/**
 * @file    tjc_usart_hmi.c
 * @brief   淘晶驰 TJC 串口屏 MSPM0 DriverLib 驱动
 * @note    UART1: PB6=TX, PB7=RX, 115200-8N1
 *          协议: 指令字符串 + 0xFF 0xFF 0xFF (3字节帧尾)
 *
 * [架构] RX: 逐字节中断 -> 环形缓冲区 -> 主循环 tjc_parse_poll 拆帧
 *        TX: 命令累积到缓冲区 -> tjc_end() -> DMA 一次性发出
 */

#include "tjc_usart_hmi.h"
#include "uart1.h"
#include "uart.h"
#include "utils.h"
#include "ti_msp_dl_config.h"
#include <ti/driverlib/driverlib.h>
#include <string.h>

/* ========== TX 累积缓冲区 ========== */
#define TJC_TX_BUF_SIZE  512

static uint8_t tjc_tx_buf[TJC_TX_BUF_SIZE];
static uint16_t tjc_tx_len;

/* ========== 底层 TX: 累积 -> DMA 发出 ========== */

static void tjc_tx_reset(void)
{
	UART1_DMA_waitDone();
	tjc_tx_len = 0;
}

void tjc_uart_send_byte(uint8_t data)
{
	if (tjc_tx_len == 0) tjc_tx_reset();
	if (tjc_tx_len < TJC_TX_BUF_SIZE)
		tjc_tx_buf[tjc_tx_len++] = data;
}

void tjc_uart_send_bytes(const uint8_t *data, uint16_t len)
{
	if (tjc_tx_len == 0) tjc_tx_reset();
	for (uint16_t i = 0; i < len && tjc_tx_len < TJC_TX_BUF_SIZE; i++)
		tjc_tx_buf[tjc_tx_len++] = data[i];
}

void tjc_flush(void)
{
	if (tjc_tx_len == 0) return;
	UART1_DMA_waitDone();
	UART1_DMA_send(tjc_tx_buf, tjc_tx_len);
	tjc_tx_len = 0;
}

/* ========== 帧头/帧尾 ========== */

void tjc_begin(void) {}

void tjc_end(void)
{
	tjc_uart_send_byte(0xFF);
	tjc_uart_send_byte(0xFF);
	tjc_uart_send_byte(0xFF);
	tjc_flush();
}

/* ========== 简单整数 -> 字符串 (避免依赖 stdio) ========== */

static int itoa_buf(char *buf, int val)
{
	char tmp[12];
	char *p = tmp + sizeof(tmp) - 1;
	int neg = 0;
	*p = '\0';
	if (val < 0) { neg = 1; val = -val; }
	if (val == 0) { *--p = '0'; }
	else { while (val) { *--p = '0' + (val % 10); val /= 10; } }
	if (neg) *--p = '-';
	int len = 0;
	while (*p) buf[len++] = *p++;
	return len;
}

/* ========== 控件操作 API ========== */

void tjc_set_text(const char *obj, const char *txt)
{
	uint16_t olen = (uint16_t)strlen(obj);
	uint16_t tlen = (uint16_t)strlen(txt);
	tjc_uart_send_bytes((const uint8_t *)obj, olen);
	tjc_uart_send_byte('.');
	tjc_uart_send_bytes((const uint8_t *)"txt=", 4);
	tjc_uart_send_byte('"');
	tjc_uart_send_bytes((const uint8_t *)txt, tlen);
	tjc_uart_send_byte('"');
	tjc_end();
}

void tjc_set_val(const char *obj, int val)
{
	char buf[12];
	int len = itoa_buf(buf, val);
	tjc_uart_send_bytes((const uint8_t *)obj, (uint16_t)strlen(obj));
	tjc_uart_send_byte('.');
	tjc_uart_send_bytes((const uint8_t *)"val=", 4);
	tjc_uart_send_bytes((uint8_t *)buf, (uint16_t)len);
	tjc_end();
}

void tjc_set_page(const char *page)
{
	tjc_uart_send_bytes((const uint8_t *)"page ", 5);
	tjc_uart_send_bytes((const uint8_t *)page, (uint16_t)strlen(page));
	tjc_end();
}

void tjc_set_global_var(const char *name, int val)
{
	char buf[12];
	int len = itoa_buf(buf, val);
	tjc_uart_send_bytes((const uint8_t *)name, (uint16_t)strlen(name));
	tjc_uart_send_byte('=');
	tjc_uart_send_bytes((uint8_t *)buf, (uint16_t)len);
	tjc_end();
}

void tjc_send_raw(const char *fmt, ...)
{
	(void)fmt;
	tjc_uart_send_bytes((const uint8_t *)fmt, (uint16_t)strlen(fmt));
	tjc_end();
}

/* ========== 环形缓冲区 (RX -> 帧解析) ========== */

#define RING_BUF_SIZE  500

typedef struct {
	uint8_t  data[RING_BUF_SIZE];
	volatile uint16_t head;
	volatile uint16_t tail;
	volatile uint16_t len;
} RingBuf_t;

static RingBuf_t g_ring;

void tjc_ringbuf_init(void)
{
	g_ring.head = 0;
	g_ring.tail = 0;
	g_ring.len  = 0;
}

void tjc_ringbuf_write(uint8_t data)
{
	if (g_ring.len >= RING_BUF_SIZE) return;
	g_ring.data[g_ring.tail] = data;
	g_ring.tail = (g_ring.tail + 1) % RING_BUF_SIZE;
	g_ring.len++;
}

uint8_t tjc_ringbuf_read(uint16_t pos)
{
	return g_ring.data[(g_ring.head + pos) % RING_BUF_SIZE];
}

uint16_t tjc_ringbuf_len(void)
{
	return g_ring.len;
}

void tjc_ringbuf_pop(uint16_t size)
{
	if (size >= g_ring.len) {
		tjc_ringbuf_init();
		return;
	}
	g_ring.head = (g_ring.head + size) % RING_BUF_SIZE;
	__disable_irq();
	g_ring.len -= size;
	__enable_irq();
}

/* ========== 波形控件 API ========== */

void tjc_wave_add(const char *obj, uint8_t ch, uint8_t val)
{
	char buf[48];
	uint16_t objlen = (uint16_t)strlen(obj);
	uint8_t *p = (uint8_t *)buf;
	memcpy(p, "add ", 4); p += 4;
	memcpy(p, obj, objlen); p += objlen;
	*p++ = '.'; *p++ = 'i'; *p++ = 'd'; *p++ = ',';
	*p++ = '0' + ch;
	*p++ = ',';
	{
		char tmp[4]; int tl = itoa_buf(tmp, val);
		memcpy(p, tmp, (uint16_t)tl); p += tl;
	}
	tjc_uart_send_bytes((uint8_t *)buf, (uint16_t)(p - (uint8_t *)buf));
	tjc_end();
}

void tjc_wave_addt(const char *obj, uint8_t ch,
	const uint8_t *data, uint16_t qyt)
{
	char buf[48];
	uint16_t objlen = (uint16_t)strlen(obj);
	uint8_t *p = (uint8_t *)buf;
	memcpy(p, "addt ", 5); p += 5;
	memcpy(p, obj, objlen); p += objlen;
	*p++ = '.'; *p++ = 'i'; *p++ = 'd'; *p++ = ',';
	*p++ = '0' + ch;
	*p++ = ',';
	{
		char tmp[6]; int tl = itoa_buf(tmp, (int)qyt);
		memcpy(p, tmp, (uint16_t)tl); p += tl;
	}
	tjc_uart_send_bytes((uint8_t *)buf, (uint16_t)(p - (uint8_t *)buf));
	tjc_end();

	Delay_ms(10);

	tjc_uart_send_bytes(data, qyt);
	tjc_flush();
}

void tjc_wave_cle(const char *obj, uint8_t ch)
{
	char buf[48];
	uint16_t objlen = (uint16_t)strlen(obj);
	uint8_t *p = (uint8_t *)buf;
	memcpy(p, "cle ", 4); p += 4;
	memcpy(p, obj, objlen); p += objlen;
	*p++ = '.'; *p++ = 'i'; *p++ = 'd'; *p++ = ',';
	*p++ = '0' + ch;
	tjc_uart_send_bytes((uint8_t *)buf, (uint16_t)(p - (uint8_t *)buf));
	tjc_end();
}

void tjc_wave_send_dac(const char *obj, uint8_t ch,
	const uint16_t *dac_data, uint16_t count)
{
	static uint8_t buf[256];
	uint16_t n = count > 256 ? 256 : count;
	for (uint16_t i = 0; i < n; i++)
		buf[i] = (uint8_t)(dac_data[i] >> 4);
	tjc_wave_addt(obj, ch, buf, n);
}

/* ========== 主动读取数值 (get -> 0x71 返回帧) ========== */

#define TJC_GET_QUEUE_LEN  8
static int32_t *tjc_get_dst[TJC_GET_QUEUE_LEN];
static uint8_t  tjc_get_head;
static uint8_t  tjc_get_tail;

void tjc_get_number(const char *obj_attr, int32_t *dst)
{
	tjc_get_dst[tjc_get_tail] = dst;
	tjc_get_tail = (tjc_get_tail + 1) % TJC_GET_QUEUE_LEN;
	if (tjc_get_tail == tjc_get_head)
		tjc_get_head = (tjc_get_head + 1) % TJC_GET_QUEUE_LEN;

	tjc_uart_send_bytes((const uint8_t *)"get ", 4);
	tjc_uart_send_bytes((const uint8_t *)obj_attr,
		(uint16_t)strlen(obj_attr));
	tjc_end();
}

/* ========== 帧解析 ========== */

#define TJC_FRAME_MAX  64

static uint8_t tjc_fixed_frame_len(uint8_t lead)
{
	switch (lead) {
	case 0x65: return 7;
	case 0x71: return 8;
	case 0x66: return 5;
	default:   return 0;
	}
}

static void tjc_handle_number(const uint8_t *f)
{
	int32_t v = (int32_t)(
		(uint32_t)f[1] | ((uint32_t)f[2] << 8) |
		((uint32_t)f[3] << 16) | ((uint32_t)f[4] << 24));
	int32_t *dst = (int32_t *)0;
	if (tjc_get_head != tjc_get_tail) {
		dst = tjc_get_dst[tjc_get_head];
		tjc_get_head = (tjc_get_head + 1) % TJC_GET_QUEUE_LEN;
	}
	if (dst) *dst = v;
	tjc_on_number(v, dst);
}

void tjc_parse_poll(void)
{
	uint16_t avail = tjc_ringbuf_len();

	while (avail >= 4) {
		uint8_t lead = tjc_ringbuf_read(0);
		uint8_t fixed = tjc_fixed_frame_len(lead);

		if (fixed) {
			if (avail < fixed) return;
			if (tjc_ringbuf_read(fixed - 3) != 0xFF ||
				tjc_ringbuf_read(fixed - 2) != 0xFF ||
				tjc_ringbuf_read(fixed - 1) != 0xFF) {
				tjc_ringbuf_pop(1);
				avail = tjc_ringbuf_len();
				uint16_t skip = 0;
				while (skip < avail &&
					tjc_fixed_frame_len(tjc_ringbuf_read(skip)) == 0
					&& tjc_ringbuf_read(skip) != 0x55)
					skip++;
				if (skip > 0) { tjc_ringbuf_pop(skip); avail -= skip; }
				continue;
			}
			uint8_t frame[8];
			for (uint8_t i = 0; i < fixed; i++)
				frame[i] = tjc_ringbuf_read(i);
			if (lead == 0x65)
				tjc_on_touch(frame[1], frame[2], frame[3]);
			else if (lead == 0x71)
				tjc_handle_number(frame);
			tjc_ringbuf_pop(fixed);
		} else {
			int frame_end = -1;
			for (uint16_t i = 0; i + 2 < avail; i++) {
				if (tjc_ringbuf_read(i) == 0xFF &&
					tjc_ringbuf_read(i + 1) == 0xFF &&
					tjc_ringbuf_read(i + 2) == 0xFF) {
					frame_end = (int)i; break;
				}
			}
			if (frame_end < 0) {
				if (tjc_ringbuf_read(0) == 0x55) return;
				uint16_t skip = 0;
				while (skip < avail &&
					tjc_fixed_frame_len(tjc_ringbuf_read(skip)) == 0
					&& tjc_ringbuf_read(skip) != 0x55)
					skip++;
				if (skip == 0) skip = 1;
				tjc_ringbuf_pop(skip);
				return;
			}
			uint8_t frame[TJC_FRAME_MAX];
			uint16_t flen = (frame_end < TJC_FRAME_MAX) ?
				(uint16_t)frame_end : TJC_FRAME_MAX;
			for (uint16_t i = 0; i < flen; i++)
				frame[i] = tjc_ringbuf_read(i);
			if (flen > 0 && frame[0] == 0x55)
				tjc_on_frame(frame[1],
					(flen > 2) ? &frame[2] : (const uint8_t *)0,
					(flen > 2) ? (flen - 2) : 0);
			tjc_ringbuf_pop((uint16_t)(frame_end + 3));
		}
		avail = tjc_ringbuf_len();
	}
}

/* ========== weak 回调 (默认打印到 UART0) ========== */

__attribute__((weak)) void tjc_on_touch(uint8_t page, uint8_t id,
	uint8_t event)
{
	UART_puts("[TJC][TOUCH] page="); UART_printNum(page);
	UART_puts(" id="); UART_printNum(id);
	UART_puts(event ? " press\r\n" : " release\r\n");
}

__attribute__((weak)) void tjc_on_frame(uint8_t cmd,
	const uint8_t *data, uint16_t len)
{
	UART_puts("[TJC][FRAME] cmd=0x");
	{
		char hex = (char)((cmd >> 4) < 10 ? '0' + (cmd >> 4) :
			'A' + (cmd >> 4) - 10);
		UART_putchar(hex);
		hex = (char)((cmd & 0x0F) < 10 ? '0' + (cmd & 0x0F) :
			'A' + (cmd & 0x0F) - 10);
		UART_putchar(hex);
	}
	UART_puts(" len="); UART_printNum(len);
	UART_puts(" data=");
	for (uint16_t i = 0; i < len; i++) {
		uint8_t b = data[i];
		char hex = (char)((b >> 4) < 10 ? '0' + (b >> 4) :
			'A' + (b >> 4) - 10);
		UART_putchar(hex);
		hex = (char)((b & 0x0F) < 10 ? '0' + (b & 0x0F) :
			'A' + (b & 0x0F) - 10);
		UART_putchar(hex);
		UART_putchar(' ');
	}
	UART_puts("\r\n");
}

__attribute__((weak)) void tjc_on_number(int32_t value, int32_t *dst)
{
	(void)dst;
	UART_puts("[TJC][NUM] value=");
	UART_printNum((uint32_t)value);
	UART_puts("\r\n");
}
