# CONFIG.md — 引脚与硬件配置

## 引脚分配

| 引脚 | 外设 | 信号 | 说明 |
|------|------|------|------|
| PA0 | GPIO | LED1 | 红色 LED |
| PA10 | UART0 | TX | 串口发送 → XDS110 虚拟串口 |
| PA11 | UART0 | RX | 串口接收 ← XDS110 虚拟串口 |
| PA15 | DAC0 | OUT | 模拟电压输出 |
| PA19 | DEBUGSS | SWDIO | 调试数据线 |
| PA20 | DEBUGSS | SWCLK | 调试时钟线 |
| PA25 | ADC0 | CH2 | ADC 采样输入 (0~VREF) |
| PB6 | UART1 | TX | 串口屏 TX (TJC 淘晶驰) |
| PB7 | UART1 | RX | 串口屏 RX (TJC 淘晶驰) |
| PB21 | GPIO | KEY S2 | 用户按键 (内部上拉) |
| PB22 | GPIO | BLUE | RGB 蓝色 LED |
| PB26 | GPIO | RED | RGB 红色 LED |
| PB27 | GPIO | GREEN | RGB 绿色 LED |

## 外设配置

### 时钟

| 时钟源 | 频率 | 用途 |
|--------|------|------|
| CPUCLK (SYSOSC) | 32 MHz | 系统主时钟 |
| LFCLK | 32768 Hz | TIMG0 |
| BUSCLK | 32 MHz | UART0、UART1、TIMG6 (DAC)、TIMG12 (ADC) |
| MFPCLK | 32 MHz | VREF、DAC12 |
| ULPCLK ÷ 8 | 4 MHz | ADC12 采样时钟 |

### 定时器

| 实例 | 外设 | 时钟 | 周期 | 模式 | 事件通道 | 用途 |
|------|------|------|------|------|:---:|------|
| TIMER_0 | TIMG0 | LFCLK/9 | 1000 ms | 周期性 | 1 | GREEN LED 闪烁 + 事件发布 |
| TIMER_1 | TIMG6 | BUSCLK | 1 μs | 周期性 | 2 | DAC 硬件触发 (1 Msps) |
| TIMER_2 | TIMG12 | BUSCLK | 10 μs | 周期性 | 3 | ADC 采样触发 (100 ksps) |

### 事件总线路由

| 发布者 | 通道 | 事件 | 订阅者 |
|--------|:---:|------|--------|
| TIMG0 | 1 | ZERO | (预留) |
| TIMG6 | 2 | ZERO | DAC12 HWTRIG0 |
| TIMG12 | 3 | ZERO | (预留) |

### ADC12

| 参数 | 值 |
|------|-----|
| 外设 | ADC0 |
| 通道 | CH2 (PA25) |
| 时钟 | ULPCLK ÷ 8 (4 MHz) |
| 基准 | VREF 内部 2.5V |
| 采样时间 | 12.5 μs |
| 触发方式 | 软件触发 (TIMER_2 ISR → startConversion) |
| 中断 | MEM0_RESULT_LOADED |

### DAC12

| 参数 | 值 |
|------|-----|
| 外设 | DAC0 |
| 输出引脚 | PA15 |
| 基准 | VREF 内部 2.5V |
| 放大器 | ON |
| FIFO | 开启 (4 深度) |
| 触发源 | HWTRIG0 (事件通道 2) |
| 中断 | FIFO_TWO_QTRS_EMPTY |

### UART

#### UART0 (调试串口)

| 参数 | 值 |
|------|-----|
| 外设 | UART0 |
| 时钟 | BUSCLK |
| 波特率 | 115200 |
| 过采样 | 16x |
| 数据位 | 8 |
| FIFO | 开启 |
| TX DMA | DMA_CH0, Buffer→FIFO (b2f) |
| 中断 | RX, DMA_DONE_TX, EOT_DONE |

#### UART1 (串口屏)

| 参数 | 值 |
|------|-----|
| 外设 | UART1 |
| 时钟 | BUSCLK |
| 波特率 | 115200 |
| 过采样 | 16x |
| 数据位 | 8 |
| FIFO | 开启 |
| 回环 | 关闭 |
| TX DMA | DMA_CH1, Buffer→FIFO (b2f) |
| RX 方式 | 逐字节中断 → callback → tjc_ringbuf_write |
| 中断 | RX, DMA_DONE_TX, EOT_DONE |
| 用途 | TJC 淘晶驰串口屏驱动 |

### DMA

| 通道 | 外设 | 方向 | 模式 | 用途 |
|:---:|------|------|------|------|
| DMA_CH0 | UART0 TX | BUF→FIFO (b2f) | 单次传输 | 串口调试 |
| DMA_CH1 | UART1 TX | BUF→FIFO (b2f) | 单次传输 | 串口屏命令 |

### VREF

| 参数 | 值 |
|------|-----|
| 输出电压 | 2.5V |
| 用途 | ADC12 基准 + DAC12 基准 |
