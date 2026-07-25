# FEATURES.md — 已实现功能与预期现象

## 源码结构

```
main.c          — 主循环入口 + TIMER_0 ISR (GREEN LED 闪烁)
main.syscfg     — SysConfig 硬件配置
User/uart.h/c   — UART0 串口调试（输出 + DMA TX + echo ISR）
User/uart1.h/c  — UART1 串口驱动（输出 + DMA TX + RX callback ISR）
User/spi.h/c    — SPI 主模式驱动（阻塞收发 + DMA 全双工）
User/tjc_usart_hmi.h/c — 淘晶驰 TJC 串口屏驱动（命令累积 DMA 发送 + ringbuf 帧解析）
User/app_screen.h/c — 串口屏应用逻辑（波形绘制、触摸事件回调）
User/utils.h/c  — 基础工具（延时、按键消抖、DSP 演示）
User/dac.h/c    — DAC12 DDS 正弦波发生器
User/adc.h/c    — ADC12 采样读取
```

## 模块 API

### utils — 基础工具

| 函数 | 功能 |
|------|------|
| `Delay_ms(ms)` | 阻塞毫秒延时 |
| `Key_Read()` | 按键 S2 (PB21) 消抖读取，返回 1=有效按下 |
| `DSP_Demo()` | CMSIS-DSP 演示 (统计/三角/FIR) |

### uart — 串口调试 (UART0, PA10/PA11)

| 函数 | 功能 |
|------|------|
| `UART_putchar(c)` | 阻塞发送 1 字节 |
| `UART_puts(s)` | 发送字符串 |
| `UART_println(s)` | 发送字符串 + 换行 |
| `UART_printNum(n)` | 发送十进制无符号数 |
| `UART_printFloat(f, d)` | 发送浮点数 (d 位小数) |
| `UART_DMA_send(buf, len)` | DMA 非阻塞发送缓冲区，返回 true=成功 |
| `UART_DMA_isBusy()` | 返回 true=DMA 传输进行中 |
| `UART_DMA_waitDone()` | 阻塞等待 DMA 传输完成 |

### uart1 — 串口驱动 (UART1, PB6/PB7)

| 函数 | 功能 |
|------|------|
| `UART1_putchar(c)` | 阻塞发送 1 字节 |
| `UART1_puts(s)` | 发送字符串 |
| `UART1_println(s)` | 发送字符串 + 换行 |
| `UART1_printNum(n)` | 发送十进制无符号数 |
| `UART1_printFloat(f, d)` | 发送浮点数 (d 位小数) |
| `UART1_DMA_send(buf, len)` | DMA 非阻塞发送缓冲区 |
| `UART1_DMA_isBusy()` | 返回 true=DMA 传输进行中 |
| `UART1_DMA_waitDone()` | 阻塞等待 DMA 传输完成 |
| `UART1_RX_setCallback(cb)` | 注册 RX 回调函数 (ISR 中调用) |

RX ISR: 不再 echo，而是调用注册的回调将字节写入 tjc ringbuf。

### spi — SPI 主模式驱动 (SPI0, PA12/PA13/PA14/PA2)

| 函数 | 功能 |
|------|------|
| `SPI_Master_Transmit(buf, len)` | 阻塞发送 (忽略接收数据) |
| `SPI_Master_Receive(buf, len)` | 阻塞接收 (发送 0x00 占位) |
| `SPI_Master_TransmitReceive(txBuf, rxBuf, len)` | 阻塞全双工收发 |
| `SPI_DMA_TransmitReceive(txBuf, rxBuf, len)` | DMA 非阻塞全双工收发 |
| `SPI_DMA_isBusy()` | 返回 true=DMA 传输进行中 |
| `SPI_DMA_waitDone()` | 阻塞等待 DMA 传输完成 |

ISR: SPI_0_INST_IRQHandler，处理 DMA_DONE_TX / DMA_DONE_RX / TX_EMPTY 中断。

### tjc_usart_hmi — TJC 串口屏驱动 (UART1, PB6/PB7)

协议格式: 指令字符串 + 0xFF 0xFF 0xFF 帧尾，TX 命令累积后 DMA 发送，RX 通过环形缓冲区异步接收并逐帧解析。

| 函数 | 功能 |
|------|------|
| `tjc_begin()` / `tjc_end()` | 帧开始/帧尾 (写 FF FF FF + DMA flush) |
| `tjc_set_text(obj, txt)` | 设置控件文本 |
| `tjc_set_val(obj, val)` | 设置控件数值 |
| `tjc_set_page(page)` | 切换页面 |
| `tjc_set_global_var(name, val)` | 设置全局变量 |
| `tjc_send_raw(str)` | 发送原始字符串 + 帧尾 |
| `tjc_flush()` | DMA 一次性发出累积的发送缓冲 |
| `tjc_parse_poll()` | 主循环帧解析 (放 app_screen_poll 内) |
| `tjc_get_number(attr, dst)` | 异步读取控件数值 (屏幕回 0x71 帧) |
| `tjc_wave_add(obj, ch, val)` | 波形控件加单点 |
| `tjc_wave_addt(obj, ch, data, qyt)` | 波形控件加多点 (批量传输) |
| `tjc_wave_cle(obj, ch)` | 清除波形通道 |
| `tjc_wave_send_dac(obj, ch, data, count)` | DAC 数据缩放后发送波形 |

帧解析支持的屏→MCU 帧类型:

| 帧头 | 说明 | 回调 |
|------|------|------|
| 0x65 | 触摸帧: `65 page id event FF FF FF` | `tjc_on_touch` |
| 0x71 | 数值返回: `71 b0 b1 b2 b3 FF FF FF` | `tjc_on_number` |
| 0x66 | 当前页面: `66 page FF FF FF` | (内部解析) |
| 0x55 | 自定义数据: `55 cmd data... FF FF FF` | `tjc_on_frame` |

所有回调均为 `__weak`，app_screen.c 中以强定义覆盖。

### app_screen — 串口屏应用逻辑

| 函数 | 功能 |
|------|------|
| `app_screen_init()` | 初始化屏幕通信 (注册 RX callback → 关回显 → 跳转 main) |
| `app_screen_poll()` | 主循环事件轮询 (调 tjc_parse_poll) |
| `app_screen_draw()` | 根据当前参数绘制波形 (正弦/三角/锯齿/方波) |
| `app_screen_auto_scale()` | 自动缩放演示 (freq=5, vpp=2800) |

### dac — 模拟输出

| 函数 | 功能 |
|------|------|
| `DAC_SetVoltage(mV)` | 输出固定电压 |
| `DAC_SetCode(code)` | 输出原始 12 位编码 |
| `DAC_Disable()` | 关闭 DAC |
| `DAC_SetFrequency(hz)` | DDS 频率设置 (0.0002Hz 分辨率) |
| `DAC_WaveConfig(off, amp)` | 波形配置 (偏置/幅度) |
| `DAC_Demo()` | DAC 正弦波演示 |

### adc — 模数采样

| 函数 | 功能 |
|------|------|
| `ADC_ReadRaw()` | 启动转换，阻塞等结果，返回 12 位原值 |
| `ADC_Read_mV()` | 同 + 自动换算为毫伏 |
| `ADC_CaptureStart(buf, n)` | 启动硬件触发批量采集 (n 个样本) |
| `ADC_CaptureIsDone()` | 返回 1=采集完成 |
| `ADC_Demo()` | 单次采样 10 点演示 |
| `ADC_CaptureDemo()` | 批量采集 128 点演示，按 S2 触发 |

## 运行现象

### 启动串口输出 (UART0, XDS110 虚拟串口)

```
=== MSPM0G3507 UART Debug ===
PA10=TX, PA11=RX, 115200-8N1
System boot, TJC screen on UART1
```

### 外设现象

| 外设 | 预期现象 |
|------|---------|
| GREEN LED (PB27) | 每秒闪烁 1 次 (TIMG0) |
| KEY S2 (PB21) | 按下后 LED1 翻转，串口打印 `[BTN] LED1 toggled, tick=N` |
| DAC OUT (PA15) | 1 kHz 正弦波，偏置 1.25V，幅值 1.0V (0.25V~2.25V) |
| ADC IN (PA25) | 接被测电压 0~2.5V，串口打印采样值 |
| UART0 (PA10/11) | 115200-8N1，DMA TX + RX echo ISR |
| UART1 (PB6/7) | 115200-8N1，TJC 淘晶驰串口屏，DMA TX + RX ringbuf，主循环帧解析 |
| SPI0 (PA12/13/14, PA2) | 500 kbps，MODE3，DMA CH2/CH3 全双工 |

## 修改维护

每次新增外设或功能时，需同步更新：
- `CONFIG.md` — 引脚分配表、外设配置参数
- `FEATURES.md` — 模块 API 列表、运行现象
