# FEATURES.md — 已实现功能与预期现象

## 源码结构

```
main.c          — 主循环入口 + TIMER_0 ISR (GREEN LED 闪烁)
main.syscfg     — SysConfig 硬件配置
User/uart.h/c   — UART0 串口调试（输出 + echo ISR）
User/uart1.h/c  — UART1 串口打印（输出 + echo ISR，供串口屏/外部设备）
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

### uart — 串口调试 (UART0)

| 函数 | 功能 |
|------|------|
| `UART_putchar(c)` | 阻塞发送 1 字节 |
| `UART_puts(s)` | 发送字符串 |
| `UART_println(s)` | 发送字符串 + 换行 |
| `UART_printNum(n)` | 发送十进制无符号数 |
| `UART_printFloat(f, d)` | 发送浮点数 (d 位小数) |

### uart1 — 串口打印 (UART1)

| 函数 | 功能 |
|------|------|
| `UART1_putchar(c)` | 阻塞发送 1 字节 |
| `UART1_puts(s)` | 发送字符串 |
| `UART1_println(s)` | 发送字符串 + 换行 |
| `UART1_printNum(n)` | 发送十进制无符号数 |
| `UART1_printFloat(f, d)` | 发送浮点数 (d 位小数) |

RX 中断服务: 字节自动 echo 回显，用于调试/自测。

### tjc_usart_hmi — TJC 串口屏驱动 (UART1, PB6/PB7) [待实现]

### app_screen — 串口屏应用逻辑 [待实现]

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

### 启动串口输出

```
=== MSPM0G3507 UART Debug ===
PA10=TX, PA11=RX, 9600-8N1
=== MSPM0G3507 UART1 Test ===
PB6=TX, PB7=RX, 115200-8N1
CPUCLK=32000000 Hz
tick=5
tick=10
...
```

### 外设现象

| 外设 | 预期现象 |
|------|---------|
| GREEN LED (PB27) | 每秒闪烁 1 次 (TIMG0) |
| KEY S2 (PB21) | 按下后 LED1 翻转，串口打印 `[BTN] LED1 toggled, tick=N` |
| DAC OUT (PA15) | 1 kHz 正弦波，偏置 1.25V，幅值 1.0V (0.25V~2.25V) |
| ADC IN (PA25) | 接被测电压 0~2.5V，串口打印采样值 |
| UART0 (PA10/11) | 9600-8N1，阻塞 TX 发送，RX 字节自动 echo |
| UART1 (PB6/7) | 115200-8N1，启动打印 + 每 5 秒 tick 输出，RX 字节自动 echo |

## 修改维护

每次新增外设或功能时，需同步更新：
- `CONFIG.md` — 引脚分配表、外设配置参数
- `FEATURES.md` — 模块 API 列表、运行现象
