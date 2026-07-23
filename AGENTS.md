# AGENTS.md — MSPM0G3507

> 详细学习计划: [`docs/LEARNING_PLAN.md`](docs/LEARNING_PLAN.md)

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

## 关键架构事实

### 文件结构
- `main.c` — 唯一用户代码入口，调用 `SYSCFG_DL_init()` 后进入主循环
- `main.syscfg` — 硬件配置入口，通过 SysConfig GUI 编辑
- `ti/driverlib/` — SDK 驱动库源文件 (符号链接)
- `Debug/ti_msp_dl_config.h/.c` — SysConfig 生成的配置代码，不可手动编辑
- SDK 示例路径: `C:\TI\mspm0_sdk_2_11_00_07\examples\nortos\LP_MSPM0G3507\driverlib\`

### SysConfig 当前引脚分配
| 信号 | 端口 | 引脚 | 宏 |
|------|------|------|-----|
| LED1 | PA0 | `LED_LED1_PIN` | `DL_GPIO_PIN_0` |
| RED | PB26 | `LED_RED_PIN` | `DL_GPIO_PIN_26` |
| GREEN | PB27 | `LED_GREEN_PIN` | `DL_GPIO_PIN_27` |
| BLUE | PB22 | `LED_BLUE_PIN` | `DL_GPIO_PIN_22` |
| 按键 LB2 | PA18 | `KEY_LB2_PIN` | `DL_GPIO_PIN_18` (内部上拉) |

### include 顺序
```c
#include <ti/driverlib/m0p/dl_interrupt.h>
#include <ti/devices/msp/msp.h>         // 或生成的头文件已包含
#include "ti_msp_dl_config.h"           // SysConfig 生成的配置
```

### 代码规范
- 永远通过 DriverLib API 操作外设，不直接写寄存器
- 中断用 `dl_interrupt.h` 的 API，不直接操作 NVIC
- 修改 `.syscfg` 后必须在 CCS Theia 中重新构建以触发 SysConfig 代码生成

## GPIO 操作常用 API

```c
DL_GPIO_setPins(GPIOA, LED_LED1_PIN);          // 置高
DL_GPIO_clearPins(GPIOA, LED_LED1_PIN);        // 置低
DL_GPIO_togglePins(GPIOA, LED_LED1_PIN);       // 翻转
uint32_t v = DL_GPIO_readPins(GPIOA, KEY_LB2_PIN); // 读取 (返回引脚掩码或 0)
```

## 延时

- `DL_Common_delayCycles(n)` 或宏 `delay_cycles(n)` — 阻塞延时 n 个 CPU 周期
- CPUCLK_FREQ = 32MHz，因此 `delay_cycles(32000)` ≈ 1ms
- 实现毫秒延时: `for(;ms;ms--) delay_cycles(32000);` (粗略，有函数调用开销)

## clangd

- `.clangd` 是自动生成的，不要提交到版本控制
- diagnostics 默认全部抑制 (`Suppress: '*'`)，如需启用需手动修改
