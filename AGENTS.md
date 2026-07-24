# AGENTS.md — MSPM0G3507

> 详细学习计划: [`docs/LEARNING_PLAN.md`](docs/LEARNING_PLAN.md)
> 引脚分配: [`CONFIG.md`](CONFIG.md)
> 功能列表: [`FEATURES.md`](FEATURES.md)

## 项目概要

- **芯片**: TI MSPM0G3507 (Cortex-M0+, armv6-m)
- **开发板**: LP_MSPM0G3507 LaunchPad
- **IDE**: Code Composer Studio (CCS) Theia 20.x — 无法用命令行构建
- **SDK**: `mspm0_sdk_2_11_00_07` (`C:\TI\mspm0_sdk_2_11_00_07`)
- **工具链**: `arm-none-eabi-gcc 9.2.1` (`C:\TI\gcc_arm_none_eabi_9_2_1`)
- **调试器**: XDS110 (SWD: PA20/SWCLK, PA19/SWDIO)
- **C 标准**: C99, 纯 C 无 C++

## 构建方式

**只能通过 CCS Theia IDE 构建**，无命令行构建。构建前 SysConfig 自动运行。
输出: `Debug/empty_driverlib_src.out` (ELF) + `.map`

## 文件结构

- `main.c` — 主循环入口 + TIMER_0 ISR (GREEN LED 闪烁)
- `main.syscfg` — 硬件配置入口，通过 SysConfig GUI 或直接编辑
- `User/` — 用户模块目录
  - `utils.h/c` — 延时、按键消抖、DSP 演示
  - `uart.h/c` — UART0 串口调试（输出 + DMA TX + echo ISR）
  - `uart1.h/c` — UART1 串口驱动（输出 + DMA TX + RX callback ISR）
  - `tjc_usart_hmi.h/c` — 淘晶驰 TJC 串口屏驱动（命令累积 DMA 发送 + ringbuf 帧解析）
  - `app_screen.h/c` — 串口屏应用逻辑（波形绘制、触摸事件回调）
  - `dac.h/c` — DAC12 DDS 正弦波发生器 + ISR
  - `adc.h/c` — ADC12 采样 + 批量采集 + ISR
- `ti/driverlib/` — SDK 驱动库源文件 (符号链接)
- `Debug/ti_msp_dl_config.h/.c` — SysConfig 生成的配置代码，不可手动编辑
- SDK 示例路径: `C:\TI\mspm0_sdk_2_11_00_07\examples\nortos\LP_MSPM0G3507\driverlib\`

## 外设 ISR 分配

| ISR 函数名 | 外设 | 源文件 | 用途 |
|------|------|------|------|
| `TIMER_0_INST_IRQHandler` | TIMG0 | main.c | GREEN LED 闪烁 |
| `TIMER_2_INST_IRQHandler` | TIMG12 | User/adc.c | 启动 ADC 转换 |
| `UART_0_INST_IRQHandler` | UART0 | User/uart.c | RX echo + DMA done |
| `UART_1_INST_IRQHandler` | UART1 | User/uart1.c | RX→callback (tjc ringbuf) + DMA done |
| `ADC12_0_INST_IRQHandler` | ADC0 | User/adc.c | 存储采样结果 |
| `DAC12_IRQHandler` | DAC0 | User/dac.c | 填充 FIFO |

## 当前引脚分配 (详见 CONFIG.md)

| 信号 | 引脚 | 宏 |
|------|------|-----|
| LED1 | PA0 | `LED_LED1_PIN` |
| RED | PB26 | `LED_RED_PIN` |
| GREEN | PB27 | `LED_GREEN_PIN` |
| BLUE | PB22 | `LED_BLUE_PIN` |
| KEY S2 | PB21 | `KEY_S2_PIN` (内部上拉) |
| UART TX | PA10 | `GPIO_UART_0_TX_PIN` |
| UART RX | PA11 | `GPIO_UART_0_RX_PIN` |
| UART1 TX (屏) | PB6 | `GPIO_UART_1_TX_PIN` |
| UART1 RX (屏) | PB7 | `GPIO_UART_1_RX_PIN` |
| DAC OUT | PA15 | `GPIO_DAC12_OUT_PIN` |
| ADC CH2 | PA25 | `GPIO_ADC12_0_C2_PIN` |

## include 顺序

```c
#include <ti/driverlib/m0p/dl_interrupt.h>
#include "ti_msp_dl_config.h"
#include <ti/driverlib/driverlib.h>
#include "User/uart.h"
#include "User/uart1.h"
#include "User/utils.h"
// ...
```

## 代码规范

- 永远通过 DriverLib API 操作外设，不直接写寄存器
- 中断用 `dl_interrupt.h` 的 API，不直接操作 NVIC
- 修改 `.syscfg` 后必须在 CCS Theia 中重新构建以触发 SysConfig 代码生成
- 新增外设时同步更新 `CONFIG.md` + `FEATURES.md`

## GPIO 操作常用 API

```c
DL_GPIO_setPins(GPIOA, LED_LED1_PIN);       // 置高
DL_GPIO_clearPins(GPIOA, LED_LED1_PIN);     // 置低
DL_GPIO_togglePins(GPIOA, LED_LED1_PIN);    // 翻转
uint32_t v = DL_GPIO_readPins(GPIOB, KEY_S2_PIN); // 读取 (返回引脚掩码或 0)
```

## DMA 常用 API

```c
DL_DMA_setSrcAddr(DMA, DMA_CH0_CHAN_ID, (uint32_t)buf);
DL_DMA_setDestAddr(DMA, DMA_CH0_CHAN_ID, (uint32_t)(&UART_0_INST->TXDATA));
DL_DMA_setTransferSize(DMA, DMA_CH0_CHAN_ID, len);
DL_DMA_enableChannel(DMA, DMA_CH0_CHAN_ID);
```

## 延时

- `DL_Common_delayCycles(n)` 或宏 `delay_cycles(n)` — 阻塞延时 n 个 CPU 周期
- CPUCLK_FREQ = 32MHz，因此 `delay_cycles(32000)` ≈ 1ms
- `Delay_ms(ms)` 位于 `User/utils.c`

## CMSIS-DSP

- 头文件: `dsp/Include/arm_math.h`
- 静态库: `dsp/arm_cortexM0l_math.a`
- 编译定义: `-DARM_MATH_CM0` (已在 `.cproject` 中配置)

## clangd

- `.clangd` 是自动生成的，不要提交到版本控制
- diagnostics 默认全部抑制 (`Suppress: '*'`)，如需启用需手动修改

## 定时器分布

| 实例 | 外设 | 时钟 | 周期 | 事件通道 | 用途 |
|------|------|------|------|:---:|------|
| TIMER_0 | TIMG0 | LFCLK/9 | 1 s | 1 | 心跳 LED |
| TIMER_1 | TIMG6 | BUSCLK | 1 μs | 2 | DAC 触发 |
| TIMER_2 | TIMG12 | BUSCLK | 10 μs | 3 | ADC 采集触发 |

## DMA 分布

| 通道 | 外设 | 方向 | 用途 |
|:---:|------|------|------|
| DMA_CH0 | UART0 TX | BUF→FIFO | 串口调试 DMA 发送 |
| DMA_CH1 | UART1 TX | BUF→FIFO | 串口屏命令 DMA 发送 |
