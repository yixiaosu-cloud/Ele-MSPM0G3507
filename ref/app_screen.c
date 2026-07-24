#include "app_screen.h"
#include "tjc_usart_hmi.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ========== 串口读取事件处理（覆写 tjc_usart_hmi.c 的 __weak 回调）==========
 *
 * tjc_usart_hmi.c 中 tjc_on_touch / tjc_on_frame / tjc_on_number 声明为 __weak。
 * 在本文件中重新定义同名函数即可自动覆盖弱定义, 无需修改 tjc_usart_hmi.c。
 * 主循环 app_screen_poll() -> tjc_parse_poll() 会自动调这些回调。
 *
 * [添加新事件]
 *   绝大多数场景只需在 tjc_on_frame 内加 cmd 分支:
 *     屏幕 printh 55; printh CMD; prints 数据...; printh FF FF FF
 *     此处 if (cmd == 新CMD) { ... 解析 data[] ... }
 *   需要全新帧头字节时才改 tjc_usart_hmi.c 的解析器 (见 tjc_fixed_frame_len + parse_poll)。
 *
 * [协议速查] 屏 -> MCU 帧格式:
 *   0x65: 65 page id event FF FF FF  (触摸)
 *   0x55: 55 cmd data... FF FF FF   (自定义, 推荐)
 *   0x71: 71 b0 b1 b2 b3 FF FF FF  (get 返回)
 */

/* 屏幕控件当前值 (屏端通过 0x55 帧发送, 或通过 get 读取) */
static volatile int32_t g_n_freq = 0;
static volatile int32_t g_n_vpp  = 0;
static volatile int32_t g_sys0   = 0;

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
            float phase = (float)i * (float)disp_cycles / (float)WAVE_POINTS;
            float norm;

            switch (mode) {
                case 0:
                    norm = (sinf(2.0f * 3.14159265f * phase) + 1.0f) / 2.0f;
                    break;
                case 1:
                    norm = 1.0f - fabsf(2.0f * (phase - floorf(phase + 0.5f)));
                    break;
                case 2:
                    norm = phase - floorf(phase);
                    break;
                case 3:
                default:
                    norm = (phase - floorf(phase)) < 0.5f ? 1.0f : 0.0f;
                    break;
            }

            int32_t val = (int32_t)((float)center + (norm - 0.5f) * 2.0f * (float)half_amp);
            if (val < 0)         val = 0;
            if (val > SCREEN_H)  val = SCREEN_H;
            wave_buf[i] = (uint8_t)val;
        }
    }

    tjc_wave_cle("s0", 0);
    HAL_Delay(5);
    tjc_wave_addt("s0", 0, wave_buf, WAVE_POINTS);

    if (freq == 0 || half_amp == 0) {
        tjc_set_text("t_title", "No Signal");
    } else {
        tjc_set_text("t_title", "H743 Ready");
    }
    printf("[SCREEN] waveform sent: mode=%u freq=%u vpp=%ld amp=%u cycles=%u\r\n",
           mode, freq, (long)vpp, half_amp, disp_cycles);
}

/* 触摸事件回调: 覆写 tjc_usart_hmi.c 的 __weak tjc_on_touch
 * 控件需在屏幕编辑器中勾选"发送控件ID", 屏幕自动以 0x65 帧上报。
 * 解析器自动拆帧 -> 调此函数, 无需脚本。 
 */
void tjc_on_touch(uint8_t page, uint8_t id, uint8_t event)
{
    printf("[TJC][TOUCH] page=%u id=%u %s\r\n", page, id, event ? "press" : "release");

    if (page == 2 && id == 4 && event == 0) {
        printf("[APP] OK pressed -> reading n_freq, n_vpp ...\r\n");
        tjc_get_number("n_freq.val", (int32_t *)&g_n_freq);
        tjc_get_number("n_vpp.val",  (int32_t *)&g_n_vpp);
    }
}

/* 自定义帧回调: 覆写 tjc_usart_hmi.c 的 __weak tjc_on_frame
 * 屏端通过 printh 55; printh CMD; prints 数据...; printh FF FF FF 发送。
 * 解析器匹配 0x55 帧头 -> 拆出 cmd + data -> 调此函数。
 *
 * [添加新事件] 在此函数内加 cmd 分支即可:
 *   else if (cmd == 0x02) { ... 处理新事件 ... }
 * 数据格式: prints 发送的是变量内存原始字节 (小端序),
 *   例如 int val=1234 -> data[0]=0xD2, data[1]=0x04
 *   多个变量依次 prints: prints v1; prints v2; prints v3 -> data[] = v1|v2|v3 连续字节 
 */
void tjc_on_frame(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    if (cmd == 0x01 && len == 5) {
        uint16_t freq = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
        uint16_t vpp  = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
        uint8_t  mode = data[4];
        g_n_freq = freq;
        g_n_vpp  = vpp;
        g_sys0   = mode;
        printf("[APP] mode_change frame: freq=%u vpp=%u sys0=%u\r\n", freq, vpp, mode);
        draw_waveform();
    } else {
        printf("[TJC][FRAME] cmd=0x%02X len=%u data=", cmd, len);
        for (uint16_t i = 0; i < len; i++) printf("%02X ", data[i]);
        printf("\r\n");
    }
}

/* 数值返回回调: 覆写 tjc_usart_hmi.c 的 __weak tjc_on_number
 * 调用 tjc_get_number("obj.attr", &dst) 后, 屏幕回 0x71 帧, 解析器自动:
 *   ① 从 FIFO 队列取出 dst 指针 (按发出顺序配对)
 *   ② 将解析出的 int32_t 写入 *dst
 *   ③ 调此回调, value=解析值, dst=落地指针 (方便识别是哪个变量)
 * 注意: get 请求过多时可能丢帧, 多值回传优先用 0x55 打包。 
 */
void tjc_on_number(int32_t value, int32_t *dst)
{
    if (dst == (int32_t *)&g_n_freq) {
        printf("[APP] n_freq = %ld\r\n", (long)value);
    } else if (dst == (int32_t *)&g_n_vpp) {
        printf("[APP] n_vpp  = %ld\r\n", (long)value);
    } else if (dst == (int32_t *)&g_sys0) {
        printf("[APP] sys0   = %ld -> drawing waveform\r\n", (long)value);
        draw_waveform();
    } else {
        printf("[TJC][NUM] value=%ld\r\n", (long)value);
    }
}

/* 初始化串口屏通信 (在 main.c 初始化阶段调用一次)
 * 顺序不可颠倒: 先清 DMA 缓冲(防 SRAM 上电随机值) -> 清零环形缓冲
 * -> 关屏幕命令回显 -> 等回包消化 -> 启动 DMA+IDLE 接收 -> 跳页 
 */
void app_screen_init(void)
{
    tjc_dma_buf_init();
    tjc_ringbuf_init();
    tjc_send_raw("bkcmd=0");
    HAL_Delay(100);
    tjc_uart_rx_start();

    printf("System boot, TJC screen on USART1, debug on USART2\r\n");
    tjc_set_page("main");
    tjc_set_text("t_title", "H743 Ready");
}

/* 串口屏事件轮询: 放在 main 主循环中调用, 检查环形缓冲区并派发事件回调。
 * 不阻塞, 无帧则立即返回。 
 */
void app_screen_poll(void)
{
    tjc_parse_poll();
}

void app_screen_auto_scale(void)
{
    printf("[APP] AUTO: auto-scale waveform\r\n");
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
