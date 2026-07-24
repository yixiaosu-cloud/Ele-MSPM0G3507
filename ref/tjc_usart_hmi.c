/**
 * @file    tjc_usart_hmi.c
 * @brief   淘晶驰 TJC 串口屏 STM32 HAL 驱动
 * @note    协议格式: 指令字符串 + 0xFF 0xFF 0xFF (3字节帧尾)
 *          专用串口 USART1: PA9=TX, PA10=RX, 115200-8N1
 *          注: USART1_IRQHandler 由 stm32h7xx_it.c 统一提供，本文件不再重复定义。
 *
 * [串口读取事件架构]
 *   DMA rx_buf --(IDLE中断)--> 环形缓冲 --(主循环轮询)--> 帧解析 --> __weak 回调
 *   - 中断层: HAL_UARTEx_RxEventCallback -> ringbuf_write (生产者)
 *   - 应用层: tjc_parse_poll() -> 拆帧 -> tjc_on_touch / tjc_on_frame / tjc_on_number (消费者)
 *   - 扩展: 新事件优先用 0x55 + cmd 号 (tjc_on_frame 内加分支), 不改解析器
 */

#include "tjc_usart_hmi.h"
#include <stdarg.h>

/* ========== DMA 缓冲区 (必须放 D2 SRAM, DMA1 才能访问) ========== */
#define TJC_TX_BUF_SIZE  512    /* 一条指令最大长度 (波形数据可达 256+) */
#define TJC_RX_DMA_SIZE  256    /* 单次 IDLE 接收最大帧长 */

static uint8_t tjc_tx_buf[TJC_TX_BUF_SIZE] __attribute__((section(".dma_buffer")));
static uint8_t tjc_rx_dma[TJC_RX_DMA_SIZE] __attribute__((section(".dma_buffer")));

/* 以下状态量供 CPU 使用, 留在默认 RAM(DTCM) 即可 */
static volatile uint16_t tjc_tx_len   = 0;   /* 当前累积字节数 */
static volatile uint8_t  tjc_tx_busy  = 0;   /* DMA 发送进行中标志 */

/* ========== 底层 UART 发送: 累积 -> DMA 一次性发出 ========== */

/* 等待上一次 DMA 发送完成, 然后复位累积缓冲区 (下条指令起点) */
static void tjc_tx_reset(void)
{
    while (tjc_tx_busy) { /* 等 TxCpltCallback 清标志; DMA 后台搬运, 不占串口 */ }
    tjc_tx_len = 0;
}

void tjc_uart_send_byte(uint8_t data)
{
    if (tjc_tx_len == 0) tjc_tx_reset();      /* 新指令首字节前先等空闲 */
    if (tjc_tx_len < TJC_TX_BUF_SIZE)
        tjc_tx_buf[tjc_tx_len++] = data;
}

void tjc_uart_send_bytes(const uint8_t *data, uint16_t len)
{
    if (tjc_tx_len == 0) tjc_tx_reset();
    for (uint16_t i = 0; i < len && tjc_tx_len < TJC_TX_BUF_SIZE; i++)
        tjc_tx_buf[tjc_tx_len++] = data[i];
}

/* 把累积缓冲区经 DMA 发出 (非阻塞, 立即返回) */
void tjc_flush(void)
{
    if (tjc_tx_len == 0) return;
    while (tjc_tx_busy) { }                   /* 确保上次已发完再复用缓冲区 */
    tjc_tx_busy = 1;
    if (HAL_UART_Transmit_DMA(&huart1, tjc_tx_buf, tjc_tx_len) != HAL_OK) {
        tjc_tx_busy = 0;                      /* 发送失败撤消忙标志, 避免死锁 */
        printf("[TJC][TX] FLUSH FAIL len=%u gState=%d dmaState=%d\r\n",
               tjc_tx_len, huart1.gState, huart1.hdmatx->State);
    }
    tjc_tx_len = 0;
}

/* ========== 帧头/帧尾 ========== */

void tjc_begin(void) {}

void tjc_end(void)
{
    tjc_uart_send_byte(0xFF);
    tjc_uart_send_byte(0xFF);
    tjc_uart_send_byte(0xFF);
    tjc_flush();                 /* 帧尾写完, 整条指令经 DMA 发出 */
}

/* ========== 控件操作 API ========== */

void tjc_set_text(const char *obj, const char *txt)
{
    tjc_uart_send_bytes((const uint8_t *)obj, strlen(obj));
    tjc_uart_send_byte('.');
    tjc_uart_send_bytes((const uint8_t *)"txt=", 4);
    tjc_uart_send_byte('"');
    tjc_uart_send_bytes((const uint8_t *)txt, strlen(txt));
    tjc_uart_send_byte('"');
    tjc_end();
}

void tjc_set_val(const char *obj, int val)
{
    char buf[12];
    int len = sprintf(buf, "%d", val);
    tjc_uart_send_bytes((const uint8_t *)obj, strlen(obj));
    tjc_uart_send_byte('.');
    tjc_uart_send_bytes((const uint8_t *)"val=", 4);
    tjc_uart_send_bytes((uint8_t *)buf, len);
    tjc_end();
}

void tjc_set_page(const char *page)
{
    tjc_uart_send_bytes((const uint8_t *)"page ", 5);
    tjc_uart_send_bytes((const uint8_t *)page, strlen(page));
    tjc_end();
}

void tjc_set_global_var(const char *name, int val)
{
    char buf[12];
    int len = sprintf(buf, "%d", val);
    tjc_uart_send_bytes((const uint8_t *)name, strlen(name));
    tjc_uart_send_byte('=');
    tjc_uart_send_bytes((uint8_t *)buf, len);
    tjc_end();
}

void tjc_send_raw(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        tjc_uart_send_bytes((uint8_t *)buf, len);
    }
    tjc_end();
}

/* ========== 环形缓冲区 (屏幕返回数据接收) ==========
 *
 * [数据流概览] 串口读取事件的三层架构:
 *   L1: DMA 硬件 -> tjc_rx_dma[] 缓冲区 (D2 SRAM, 放中断上下文可访问区域)
 *   L2: ISR 回调 -> g_ring 环形缓冲区 (DTCM, 线程安全的 FIFO)
 *   L3: 主循环   -> tjc_parse_poll() 拆帧派发 -> tjc_on_* 事件回调
 *
 * 环形缓冲区充当 L1->L3 的解耦层:
 *   - 生产者: HAL_UARTEx_RxEventCallback (中断上下文) 调用 ringbuf_write
 *   - 消费者: tjc_parse_poll (主循环上下文) 调用 ringbuf_read/ringbuf_pop
 *   - ringbuf_pop 内关中断保护 len 字段, 避免读-改-写竞态
 */

#define RING_BUF_SIZE  500

typedef struct {
    uint8_t  data[RING_BUF_SIZE];
    volatile uint16_t head;   /* 消费端(主循环)推进 */
    volatile uint16_t tail;   /* 生产端(中断)推进 */
    volatile uint16_t len;    /* 两端都改 -> 改写时需临界区保护 */
} RingBuf_t;

static RingBuf_t g_ring;

void tjc_ringbuf_init(void)
{
    g_ring.head = 0;
    g_ring.tail = 0;
    g_ring.len  = 0;
}

/* SRAM 上电值随机: DMA 满缓冲区时若实际数据不足, 尾部会是垃圾值。
   启动时清零, 后续由 DMA 覆写有效数据。 */
void tjc_dma_buf_init(void)
{
    memset(tjc_tx_buf, 0, TJC_TX_BUF_SIZE);
    memset(tjc_rx_dma, 0, TJC_RX_DMA_SIZE);
}

void tjc_ringbuf_write(uint8_t data)
{
    /* 生产端(中断上下文调用): 满则丢弃, len++ 在中断内本身原子 */
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
    /* 消费端(主循环): len -= size 是读-改-写, 需防中断在中途 len++ 丢计数 */
    if (size >= g_ring.len) {
        tjc_ringbuf_init();
        return;
    }
    g_ring.head = (g_ring.head + size) % RING_BUF_SIZE;
    __disable_irq();
    g_ring.len -= size;
    __enable_irq();
}

/* ========== DMA 收发中断回调 ==========
 * USART1_IRQHandler / DMA1_Stream2/3_IRQHandler 均在 stm32h7xx_it.c, 会调
 * HAL_UART_IRQHandler / HAL_DMA_IRQHandler, 由 HAL 回调下列函数。
 *
 * [RX 事件链] USART1 收到数据 -> 触发 IDLE 或 DMA TC ->
 *   HAL_UARTEx_RxEventCallback -> 把字节从 DMA 缓冲区搬进 ring buffer ->
 *   重启 DMA+IDLE 接收 -> 主循环 tjc_parse_poll 拿出来解析
 */

/* 发送完成: 清忙标志, 允许发下一条 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        tjc_tx_busy = 0;
    }
}

/* 启动接收: DMA + IDLE 空闲中断, 一帧收完自动回调 */
void tjc_uart_rx_start(void)
{
    /* 注意: UART_Start_Receive_DMA 内部会设 XferHalfCplt=UART_DMARxHalfCplt,
       导致 HAL_DMA_Start_IT 使能 HT 中断。必须在 ReceiveToIdle_DMA 返回后再清除。 */
    HAL_StatusTypeDef s = HAL_UARTEx_ReceiveToIdle_DMA(&huart1, tjc_rx_dma, TJC_RX_DMA_SIZE);
    if (s == HAL_OK) {
        huart1.hdmarx->XferHalfCpltCallback = NULL;
        __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
    } else {
        printf("[TJC][RX] START FAIL ret=%d rxState=%d dmaState=%d\r\n",
               s, huart1.RxState, huart1.hdmarx->State);
    }
}

/* 收到一帧(空闲或满): size = 本次收到字节数, 复制进环形缓冲区后重启接收 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart->Instance == USART1) {
        for (uint16_t i = 0; i < size; i++)
            tjc_ringbuf_write(tjc_rx_dma[i]);
        /* HAL 在 CIRCULAR 模式下不清理 RxState, 必须在 re-arm 前手动置 READY */
        HAL_DMA_Abort(huart1.hdmarx);
        huart->RxState = HAL_UART_STATE_READY;
        HAL_StatusTypeDef s = HAL_UARTEx_ReceiveToIdle_DMA(&huart1, tjc_rx_dma, TJC_RX_DMA_SIZE);
        if (s == HAL_OK) {
            huart1.hdmarx->XferHalfCpltCallback = NULL;
            __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
        } else {
            printf("[TJC][RX] RE-ARM FAIL ret=%d rxState=%d dmaState=%d\r\n",
                   s, huart->RxState, huart1.hdmarx->State);
        }
    }
}

/* 接收错误回调 (关键: 无此函数则 ORE 溢出后 RX 永久停止, 表现为"只响应第一次")。
 * 普通模式 DMA + IDLE 接收在"停止->重启"的空窗期若又来字节, 会触发 ORE 使 HAL 中止接收;
 * 这里清错误标志并重启接收, 让 RX 自愈。 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        /* 清除 ORE/NE/FE/PE 等错误标志 */
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        __HAL_UART_CLEAR_PEFLAG(huart);
        huart->ErrorCode = HAL_UART_ERROR_NONE;
        /* HAL 在 CIRCULAR 模式下不清理 RxState, 必须在 re-arm 前手动置 READY */
        huart->RxState = HAL_UART_STATE_READY;
        /* 重启 DMA + IDLE 接收 */
        HAL_StatusTypeDef s = HAL_UARTEx_ReceiveToIdle_DMA(&huart1, tjc_rx_dma, TJC_RX_DMA_SIZE);
        if (s == HAL_OK) {
            huart1.hdmarx->XferHalfCpltCallback = NULL;
            __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
        } else {
            printf("[TJC][RX] ERROR RE-ARM FAIL ret=%d rxState=%d dmaState=%d\r\n",
                   s, huart->RxState, huart1.hdmarx->State);
        }
    }
}

/* ========== 波形控件 API ========== */

void tjc_wave_add(const char *obj, uint8_t ch, uint8_t val)
{
    char buf[48];
    int len = sprintf(buf, "add %s.id,%d,%d", obj, ch, val);
    tjc_uart_send_bytes((uint8_t *)buf, len);
    tjc_end();
}

void tjc_wave_addt(const char *obj, uint8_t ch, const uint8_t *data, uint16_t qyt)
{
    char buf[48];
    int len = sprintf(buf, "addt %s.id,%d,%d", obj, ch, qyt);
    tjc_uart_send_bytes((uint8_t *)buf, len);
    tjc_end();

    HAL_Delay(10);              /* 等屏幕就绪, 顺带让上条 DMA 发完 */

    tjc_uart_send_bytes(data, qyt);  /* 原始数据, 无帧尾 */
    tjc_flush();
}

void tjc_wave_cle(const char *obj, uint8_t ch)
{
    char buf[48];
    int len = sprintf(buf, "cle %s.id,%d", obj, ch);
    tjc_uart_send_bytes((uint8_t *)buf, len);
    tjc_end();
}

void tjc_wave_send_dac(const char *obj, uint8_t ch, const uint16_t *dac_data, uint16_t count)
{
    static uint8_t buf[256];
    uint16_t n = count > 256 ? 256 : count;

    for (uint16_t i = 0; i < n; i++) {
        buf[i] = (uint8_t)(dac_data[i] >> 4);
    }
    tjc_wave_addt(obj, ch, buf, n);
}

/* ========== 主动读取数值 (get -> 0x71 返回帧) ========== */

/* get 请求发出后, 屏幕按顺序回 0x71 帧。用先进先出队列记录每次请求的落地指针,
 * 收到 0x71 时按发出顺序取回对应指针写入。 */
#define TJC_GET_QUEUE_LEN  8
static int32_t *tjc_get_dst[TJC_GET_QUEUE_LEN];
static uint8_t  tjc_get_head = 0;   /* 取(收到返回帧时) */
static uint8_t  tjc_get_tail = 0;   /* 存(发出请求时) */

void tjc_get_number(const char *obj_attr, int32_t *dst)
{
    /* 记录本次请求的落地指针(队列满则丢弃最旧的一个位置) */
    tjc_get_dst[tjc_get_tail] = dst;
    tjc_get_tail = (tjc_get_tail + 1) % TJC_GET_QUEUE_LEN;
    if (tjc_get_tail == tjc_get_head)
        tjc_get_head = (tjc_get_head + 1) % TJC_GET_QUEUE_LEN;

    /* 发送 "get obj.attr" + 帧尾 */
    tjc_uart_send_bytes((const uint8_t *)"get ", 4);
    tjc_uart_send_bytes((const uint8_t *)obj_attr, strlen(obj_attr));
    tjc_end();
}

/* ========== 帧解析 (屏 -> STM32) ==========
 *
 * [核心派发逻辑] tjc_parse_poll() 在主循环中被轮询调用:
 *   1. 从环形缓冲区取首字节 (lead byte)
 *   2. 查 tjc_fixed_frame_len() 判断是定长帧还是变长帧
 *   3. 定长帧: 等够字节 -> 验证帧尾 FF FF FF -> 匹配则派发, 不匹配则跳到下一个已知帧头
 *   4. 变长帧: 扫描 FF FF FF 帧尾 -> 找到则派发, 找不到则跳到下一个已知帧头
 *   5. 派发: 0x65->tjc_on_touch, 0x71->tjc_handle_number, 0x55->tjc_on_frame
 *
 * [添加新帧类型] 需要三步:
 *   ① 在 tjc_fixed_frame_len() 的 switch 中加 case, 定长返回帧长度, 变长返回 0
 *   ② 在本函数中加派发分支 (参考 0x55 的处理逻辑)
 *   ③ 在 tjc_usart_hmi.h 声明新的 __weak 回调, 在 .c 末写出默认弱实现
 */

#define TJC_FRAME_MAX  64   /* 单帧最大长度(含帧尾), 超长视为异常丢弃 */

/* 返回定长帧的总长度(含 3 字节帧尾); 返回 0 表示变长帧(需扫帧尾)。
 * 关键: 0x71 数值帧的数据段可能含 0xFF, 必须按定长处理, 不能扫帧尾。
 *
 * [添加新帧头] 在此 switch 中加 case:
 *   定长帧 -> return N (N = 数据长度 + 3)
 *   变长帧 -> return 0 (解析器自动扫描 FF FF FF 帧尾) */
static uint8_t tjc_fixed_frame_len(uint8_t lead)
{
    switch (lead) {
        case 0x65: return 7;   /* 触摸帧: 65 page id event FF FF FF */
        case 0x71: return 8;   /* 数值帧: 71 b0 b1 b2 b3 FF FF FF (小端 int32) */
        case 0x66: return 5;   /* 当前页面帧: 66 page FF FF FF (可选支持) */
        default:   return 0;   /* 其它(如 0x55 自定义帧): 变长 */
    }
}

/* 收到 0x71 数值帧: 从 FIFO 队列取回请求时的落地指针, 写入并回调。
 * 队列按 tjc_get_number 调用顺序先进先出, 保证每个 get 请求与返回帧正确配对。 */
static void tjc_handle_number(const uint8_t *f)
{
    int32_t v = (int32_t)((uint32_t)f[1] | ((uint32_t)f[2] << 8) |
                          ((uint32_t)f[3] << 16) | ((uint32_t)f[4] << 24));
    int32_t *dst = (int32_t *)0;
    if (tjc_get_head != tjc_get_tail) {          /* 队列非空: 取出本次请求的落地指针 */
        dst = tjc_get_dst[tjc_get_head];
        tjc_get_head = (tjc_get_head + 1) % TJC_GET_QUEUE_LEN;
    }
    if (dst) *dst = v;
    tjc_on_number(v, dst);
}

/* 主循环轮询: 按帧类型逐帧解析并分派 */
void tjc_parse_poll(void)
{
    uint16_t avail = tjc_ringbuf_len();

    while (avail >= 4) {
        uint8_t lead = tjc_ringbuf_read(0);
        uint8_t fixed = tjc_fixed_frame_len(lead);

        if (fixed) {
            /* 定长帧: 等够字节后校验帧尾 */
            if (avail < fixed) return;                     /* 不足一帧, 等更多数据 */
            if (tjc_ringbuf_read(fixed - 3) != 0xFF ||
                tjc_ringbuf_read(fixed - 2) != 0xFF ||
                tjc_ringbuf_read(fixed - 1) != 0xFF) {
                static int _sync_lost = 0;
                if (++_sync_lost <= 3) {
                    printf("[TJC][PARSE] sync lost head=0x%02X pos=%d avail=%u\r\n",
                           lead, fixed-3, avail);
                    printf("[TJC][PARSE] buf: ");
                    uint16_t dump_n = avail > 64 ? 64 : avail;
                    for (uint16_t d = 0; d < dump_n; d++)
                        printf("%02X ", tjc_ringbuf_read(d));
                    printf("\r\n");
                }
                /* 定长帧尾不匹配: 跳过该字节并扫描到下一个已知帧头 */
                tjc_ringbuf_pop(1);
                avail = tjc_ringbuf_len();
                /* 快速跳过非帧头字节直到下一个 65/66/71/55 */
                uint16_t skip = 0;
                while (skip < avail && tjc_fixed_frame_len(tjc_ringbuf_read(skip)) == 0
                       && tjc_ringbuf_read(skip) != 0x55) {
                    skip++;
                }
                if (skip > 0) { tjc_ringbuf_pop(skip); avail -= skip; }
                continue;
            }
            uint8_t frame[8];
            for (uint8_t i = 0; i < fixed; i++) frame[i] = tjc_ringbuf_read(i);
            if (lead == 0x65)      tjc_on_touch(frame[1], frame[2], frame[3]);
            else if (lead == 0x71) tjc_handle_number(frame);
            /* 0x66 当前页面帧: 如需可在此处理 frame[1] */
            tjc_ringbuf_pop(fixed);
        } else {
            /* 变长帧(如 0x55 自定义帧): 扫描 FF FF FF 帧尾 */
            int frame_end = -1;
            for (uint16_t i = 0; i + 2 < avail; i++) {
                if (tjc_ringbuf_read(i) == 0xFF && tjc_ringbuf_read(i + 1) == 0xFF &&
                    tjc_ringbuf_read(i + 2) == 0xFF) { frame_end = (int)i; break; }
            }
            if (frame_end < 0) {
                /* 无帧尾: 跳到下一个已知帧头 65/66/71/55 或全丢 */
                uint16_t skip = 0;
                while (skip < avail && tjc_fixed_frame_len(tjc_ringbuf_read(skip)) == 0
                       && tjc_ringbuf_read(skip) != 0x55) {
                    skip++;
                }
                if (skip == 0) skip = 1;  /* 未知字节且找不到帧头: 至少丢 1 */
                tjc_ringbuf_pop(skip);
                return;
            }
            uint8_t frame[TJC_FRAME_MAX];
            uint16_t flen = (frame_end < TJC_FRAME_MAX) ? (uint16_t)frame_end : TJC_FRAME_MAX;
            for (uint16_t i = 0; i < flen; i++) frame[i] = tjc_ringbuf_read(i);
            if (flen > 0 && frame[0] == 0x55)
                tjc_on_frame(frame[1], (flen > 2) ? &frame[2] : (const uint8_t *)0,
                             (flen > 2) ? (flen - 2) : 0);
            tjc_ringbuf_pop(frame_end + 3);
        }
        avail = tjc_ringbuf_len();
    }
}

/* ========== 弱定义回调 (默认打印到调试口 USART2, 可在 main.c 重写) ========== */

__attribute__((weak)) void tjc_on_touch(uint8_t page, uint8_t id, uint8_t event)
{
    printf("[TJC][TOUCH] page=%u id=%u %s\r\n",
           page, id, event ? "press" : "release");
}

__attribute__((weak)) void tjc_on_frame(uint8_t cmd, const uint8_t *data, uint16_t len)
{
    printf("[TJC][FRAME] cmd=0x%02X len=%u data=", cmd, len);
    for (uint16_t i = 0; i < len; i++) printf("%02X ", data[i]);
    printf("\r\n");
}

__attribute__((weak)) void tjc_on_number(int32_t value, int32_t *dst)
{
    (void)dst;
    printf("[TJC][NUM] value=%ld\r\n", (long)value);
}
