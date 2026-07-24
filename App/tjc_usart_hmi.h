#ifndef __TJC_USART_HMI_H__
#define __TJC_USART_HMI_H__

#include <stdint.h>

/* TJC 串口屏: UART1 (PB6=TX, PB7=RX), 115200-8N1
 * 协议: 指令字符串 + 0xFF 0xFF 0xFF (3 字节帧尾) */

void tjc_begin(void);
void tjc_end(void);

void tjc_set_text(const char *obj, const char *txt);
void tjc_set_val(const char *obj, int val);
void tjc_set_page(const char *page);
void tjc_set_global_var(const char *name, int val);
void tjc_send_raw(const char *fmt, ...);

void tjc_uart_send_byte(uint8_t data);
void tjc_uart_send_bytes(const uint8_t *data, uint16_t len);
void tjc_flush(void);

void tjc_ringbuf_init(void);
void tjc_ringbuf_write(uint8_t data);
uint8_t tjc_ringbuf_read(uint16_t pos);
uint16_t tjc_ringbuf_len(void);
void tjc_ringbuf_pop(uint16_t size);

/* ========== 帧解析 (屏 -> MCU) ==========
 *
 * 屏 -> MCU 帧格式:
 *   0x65: 65 page id event FF FF FF  (触摸帧)
 *   0x71: 71 b0 b1 b2 b3 FF FF FF  (数值返回, 小端 int32)
 *   0x66: 66 page FF FF FF          (当前页面号)
 *   0x55: 55 cmd data... FF FF FF   (自定义数据帧)
 */
void tjc_parse_poll(void);

void tjc_get_number(const char *obj_attr, int32_t *dst);

void tjc_on_touch(uint8_t page, uint8_t id, uint8_t event);
void tjc_on_frame(uint8_t cmd, const uint8_t *data, uint16_t len);
void tjc_on_number(int32_t value, int32_t *dst);

void tjc_wave_add(const char *obj, uint8_t ch, uint8_t val);
void tjc_wave_addt(const char *obj, uint8_t ch,
	const uint8_t *data, uint16_t qyt);
void tjc_wave_cle(const char *obj, uint8_t ch);
void tjc_wave_send_dac(const char *obj, uint8_t ch,
	const uint16_t *dac_data, uint16_t count);

#endif
