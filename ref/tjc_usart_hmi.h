#ifndef __TJC_USART_HMI_H__
#define __TJC_USART_HMI_H__

#include "main.h"
#include <stdio.h>
#include <string.h>

/* TJC 串口屏专用串口: USART1 (PA9=TX, PA10=RX), 115200-8N1
 * 协议: 指令字符串 + 0xFF 0xFF 0xFF (3 字节帧尾) */
extern UART_HandleTypeDef huart1;

void tjc_begin(void);
void tjc_end(void);

void tjc_set_text(const char *obj, const char *txt);
void tjc_set_val(const char *obj, int val);
void tjc_set_page(const char *page);
void tjc_set_global_var(const char *name, int val);
void tjc_send_raw(const char *fmt, ...);

void tjc_uart_send_byte(uint8_t data);
void tjc_uart_send_bytes(const uint8_t *data, uint16_t len);
void tjc_flush(void);           /* 将累积缓冲区经 DMA 发出 */
void tjc_uart_rx_start(void);

void tjc_ringbuf_init(void);
void tjc_ringbuf_write(uint8_t data);
uint8_t tjc_ringbuf_read(uint16_t pos);
uint16_t tjc_ringbuf_len(void);
void tjc_ringbuf_pop(uint16_t size);
void tjc_dma_buf_init(void);    /* 清零 DMA 收发缓冲区 (SRAM 上电值随机) */

/* ========== 帧解析 (屏 -> STM32) — 串口读取事件核心 ==========
 *
 * [数据流] DMA rx_buf --(IDLE中断)--> 环形缓冲区 --(主循环轮询)--> 帧解析 --> 事件回调
 *   1. DMA 收满或空闲 -> HAL_UARTEx_RxEventCallback -> tjc_ringbuf_write 逐字灌入环形缓冲
 *   2. 主循环调用 tjc_parse_poll -> 按帧头字节拆帧 -> 匹配帧尾 FF FF FF
 *   3. 定长帧按类型校验帧尾, 变长帧扫描帧尾; 失步时跳到下一个已知帧头自愈
 *   4. 解析成功 -> 派发到对应的 __weak 回调函数
 *
 * [支持的帧类型] 按首字节 (lead byte) 分派:
 *   0x65 — 标准触摸帧: 65 page id event FF FF FF  (控件勾选"发送控件ID", 免脚本)
 *   0x71 — 数值返回帧: 71 b0 b1 b2 b3 FF FF FF    (get 指令的响应, 数据段可能含 FF)
 *   0x66 — 当前页面帧: 66 page FF FF FF            (切页时屏幕自动上报)
 *   0x55 — 自定义数据帧: 55 cmd data... FF FF FF   (屏幕 printh/prints 拼帧, 推荐方式)
 *
 * [如何添加新事件类型]
 *   方式A (推荐): 不改解析器, 屏幕用 0x55 帧 + 新 cmd 号, 在 tjc_on_frame 里加分支
 *     屏幕脚本:  printh 55; printh CMD; prints 数据; printh FF FF FF
 *     单片机:    if (cmd == 新CMD) { ... 处理 data ... }
 *   方式B:       新增帧头字节, 需改 tjc_fixed_frame_len() 注册 + 解析器加派发 + 添新回调
 *     1. tjc_fixed_frame_len() 注册帧长 (定长) 或返回 0 (变长)
 *     2. tjc_parse_poll() 中加派发分支
 *     3. 声明并实现新的 __weak 回调
 */
void tjc_parse_poll(void);

/* 主动读取控件数值: 发送 "get obj.attr" 指令, 屏幕回 0x71 数值帧。
 * dst 为可选的结果落地指针(收到后自动写入); 支持连续多次调用(先进先出配对返回帧)。
 * 例: tjc_get_number("n_freq.val", &g_freq); */
void tjc_get_number(const char *obj_attr, int32_t *dst);

/* ========== 串口读取事件回调 (屏 -> MCU) ==========
 *
 * 三个回调均为 __weak 弱定义, 默认只打印调试信息到 USART2.
 * 在 app_screen.c (或任意 .c 文件) 中重新定义同名函数即可自动覆盖.
 * 解析器 tjc_parse_poll() 在主循环中轮询, 匹配到对应帧后自动调这些回调.
 *
 * [扩展新事件示例] 在 tjc_on_frame 内加 cmd 分支:
 *   void tjc_on_frame(uint8_t cmd, const uint8_t *data, uint16_t len) {
 *       if (cmd == 0x01) { ... 已有: mode_change ... }
 *       else if (cmd == 0x02) { ... 新增事件处理 ... }
 *   }
 */
void tjc_on_touch(uint8_t page, uint8_t id, uint8_t event);
void tjc_on_frame(uint8_t cmd, const uint8_t *data, uint16_t len);
void tjc_on_number(int32_t value, int32_t *dst);   /* 收到 0x71 数值帧时回调 */

void tjc_wave_add(const char *obj, uint8_t ch, uint8_t val);
void tjc_wave_addt(const char *obj, uint8_t ch, const uint8_t *data, uint16_t qyt);
void tjc_wave_cle(const char *obj, uint8_t ch);
void tjc_wave_send_dac(const char *obj, uint8_t ch, const uint16_t *dac_data, uint16_t count);

#endif
