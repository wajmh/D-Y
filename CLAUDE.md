# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

D-Y 是一个双电池智能电源管理系统，运行在 STM32G474RBT6 微控制器上。核心功能包括：

- **双电池并联与防环流控制**：智能管理两组独立锂电池包的并联供电，防止高低压电池间的倒灌
- **软启动与预充保护**：通过预放电回路限制母线大电容上电浪涌电流
- **多通道高精度采样**：DMA + 双 ADC 采集 4 路电机电流、2 路电池电压/电流及外设电流
- **三路 FDCAN 网关**：FDCAN1/2 对接双电池 BMS，FDCAN3 对接上位机（如 RK3588），实现协议解析、状态转发和遥测上报
- **硬件级安全保护**：反电动势吸收、外设供电联动、急停检测及异常快速关断

主要应用场景：四足机器人、轮式移动机器人、双电池无人移动平台等高可靠性系统。

## 构建命令

项目使用 CMake + Ninja + arm-none-eabi-gcc 工具链构建：

```bash
# 配置 Debug 构建
cmake --preset Debug

# 编译 Debug 版本
cmake --build --preset Debug

# 配置 Release 构建
cmake --preset Release

# 编译 Release 版本
cmake --build --preset Release

# 清理构建产物
cmake --build --preset Debug --target clean
```

构建产物位于 `build/Debug/` 或 `build/Release/`，包括 `D-Y.elf`、`D-Y.hex` 和 `D-Y.bin`。

**注意**：如果使用 STM32Cube 工具链，需要确保环境变量 `PATH` 包含相应的工具路径：
```bash
export PATH=/home/yq/.local/share/stm32cube/bundles/cmake/4.3.1+st.1/bin:/home/yq/.local/share/stm32cube/bundles/ninja/1.13.2+st.1/bin:$PATH
```

## 代码架构

### 核心模块

项目代码组织在 `Core/` 目录下，主要模块包括：

- **gpio (`Core/Src/gpio.c`, `Core/Inc/gpio.h`)**：电池放电状态机、MOS 管控制、预充保护、回充均衡、急停检测
- **adc (`Core/Src/adc.c`, `Core/Inc/adc.h`)**：双 ADC + DMA 采样、零点校准、数字滤波（低通滤波 + 中值滤波）
- **fdcan (`Core/Src/fdcan.c`, `Core/Inc/fdcan.h`)**：三路 FDCAN 管理、BMS 协议解析、网关转发、遥测上报
- **main (`Core/Src/main.c`)**：主循环，协调各模块任务执行
- **usart (`Core/Src/usart.c`)**：RS485 工业总线接口（带硬件方向控制）
- **i2c (`Core/Src/i2c.c`)**：硬件 I2C 接口（用于传感器或 EEPROM 扩展）

### 电池放电状态机

每块电池有三个状态：
- `POWER_BATTERY_STATE_OFF`：关闭/未就绪
- `POWER_BATTERY_STATE_PRE_DISCHARGE`：预放电阶段（400ms 软启动）
- `POWER_BATTERY_STATE_DISCHARGE`：主放电已使能

**预充流程**：检测到电池就绪后，先开启预放电 MOS（串联限流电阻）延时 400ms 完成母线电容预充，然后闭合主放电 MOS 和回充 MOS，最后释放反电吸收回路（仅首路电池执行，标志位 `backEmfAbsorbReleased` 确保第二路电池热插拔时不重复延时）。

**防环流算法**：通过 `Power_GetRechargeMaskByCanVoltage()` 实时比较双电池电压差。若压差 > 0.10V，仅开通高压侧回充 MOS；压差 ≤ 0.10V 时同时开通双路回充 MOS。

**CAN 掉线与物理拔出区分**：系统通过 ADC 采集的物理电压（阈值 60.0V）区分"电池物理拔出"与"CAN 通信掉线"。CAN 掉线时保持放电回路和外设供电，通过 FDCAN3 上报状态码让上位机执行安全姿态规划。

### FDCAN 通信架构

```
电池包 1 BMS <--FDCAN1--> STM32G474 <--FDCAN3--> 上位机/主控板
电池包 2 BMS <--FDCAN2--> (网关中枢)            (例如 RK3588)
```

- **FDCAN1/2**：250kbps，接收 BMS 状态帧（电压、电流、MOS 状态、故障码），并转发到 FDCAN3
- **FDCAN3**：250kbps，向上位机上报桥臂电流（`0x04100000U`，200ms）、外设电流（`0x04200000U`，200ms）、急停告警（`0x04300000U`，50ms）、电池与 MOS 状态（`0x04400000U`，50ms 故障/100ms 心跳/事件即时）

BMS 唤醒帧每 2000ms 广播一次（`ID: 0x0400FF80U`），超时未收到状态帧则判定 CAN 掉线。

### ADC 采样与滤波

- **ADC1（6 通道）**：4 路桥臂电流 + 2 路电池电流（DMA 循环模式）
- **ADC2（4 通道）**：2 路电池电压 + 1 路外设电流（DMA 循环模式）

**零点校准**：上电后延时 20ms，采样 500 次计算静态零点 Offset，消除运放失调和温漂。

**滤波策略**：
- 桥臂电流和外设电流：一阶滞后低通滤波（α = 0.20），平衡噪声抑制与动态响应
- 电池电压和电池电流：5 阶滑动窗口中值滤波，抑制接触毛刺和阶跃尖峰

**物理量计算**：
- 电池电压：`V_bat = V_ADC × 28.75 + 0.9V`（分压比 55.5kΩ:2.0kΩ）
- 电流：`I = (V_ADC - V_offset) × 25.0A/V`

## 开发约定

### 代码风格

- 使用 C11 标准，两空格缩进
- 命名约定：
  - 模块 API：`PascalCase`（例如 `Power_DischargeModeTask`）
  - 宏/常量：`UPPER_SNAKE_CASE`（例如 `POWER_PRE_DISCHARGE_DELAY_MS`）
  - 文件内部状态：`camelCase`（例如 `battery1_voltage`）
- 使用固定宽度类型（`uint32_t`、`uint16_t` 等）处理寄存器和通信数据
- 检查 HAL 函数返回值，处理错误情况

### STM32CubeMX 集成

保持 `USER CODE BEGIN` 和 `USER CODE END` 标记区域，手写逻辑放在这些区域内，避免 CubeMX 重新生成时被覆盖。硬件配置文件 `D-Y.ioc` 包含引脚、时钟、外设配置。

### 测试与验证

项目无自动化单元测试框架。代码变更需要：
1. 编译 Debug 版本并检查编译警告
2. 在 STM32G474 实际硬件上验证：
   - ADC 零点校准结果
   - 双电池预充/MOS 切换时序
   - 急停功能（PD2 低电平有效，30ms 消抖）
   - FDCAN1/2/3 报文收发（使用 CAN 分析仪）
3. 在 PR 中记录测试结果、CAN ID、时序测量、示波器截图

### 提交与 PR

- 提交信息可使用中文或英文，保持简洁明确
- PR 需说明行为和硬件影响、构建/烧录测试结果、关联 Issue（如有）
- 涉及安全或接线变化时附上示波器波形、CAN 捕获或照片

## 关键常量与阈值

- **预放电延时**：`POWER_PRE_DISCHARGE_DELAY_MS = 400` (毫秒)
- **反电吸收释放延时**：`POWER_PRE_DISCHARGE_DELAY = 200` (毫秒)
- **回充均衡压差**：`POWER_RECHARGE_BALANCE_DIFF = 0.10` (伏特)
- **电池物理在线判定电压**：`BATTERY_PHYSICAL_PRESENT_VOLTAGE = 60.0` (伏特)
- **CAN 通信超时**：`POWER_BATTERY_CAN_TIMEOUT_MS = 2000` (毫秒)
- **BMS 唤醒周期**：`FDCAN_BATTERY_WAKE_PERIOD_MS = 2000` (毫秒)
- **ADC 零点校准采样次数**：`ADC_LEG_CURRENT_OFFSET_SAMPLE_COUNT = 500`
- **一阶低通滤波系数**：`ADC_LEG_CURRENT_FILTER_ALPHA = 0.20`
- **中值滤波窗口大小**：5 阶

## 硬件引脚映射

### 电池 1 控制
- `PC12`：充电 MOS
- `PA0`：主放电 MOS
- `PA4`：回充 MOS
- `PB0`：预放电 MOS

### 电池 2 控制
- `PC11`：充电 MOS
- `PA5`：主放电 MOS
- `PC4`：回充 MOS
- `PB1`：预放电 MOS

### 公共控制
- `PB11`：反电吸收 MOS
- `PB10`：外设供电控制
- `PD2`：急停输入（低电平有效）

### ADC 采样
- ADC1：`PC0`~`PC3`(桥臂 1~4 电流)、`PA2`(电池 1 电流)、`PB12`(电池 2 电流)
- ADC2：`PA6`(电池 1 电压)、`PA7`(电池 2 电压)、`PB2`(外设电流)

### 通信接口
- FDCAN1：`PA11`(RX)、`PA12`(TX) - 电池 1 BMS
- FDCAN2：`PB5`(RX)、`PB13`(TX) - 电池 2 BMS
- FDCAN3：`PA8`(RX)、`PB4`(TX) - 上位机
- USART2：`PA1`(DE)、`PA3`(RX)、`PB3`(TX) - RS485
- I2C1：`PA15`(SCL)、`PB7`(SDA)

## 参考文档

- `SYSTEM_ANALYSIS_AND_FUNCTIONS.md`：详细的系统分析与功能说明文档
- `BATTERY_CAN_DISCONNECT_SAFETY_PLAN.md`：电池 CAN 掉线安全处理方案
- `fdcan3_busoff_analysis.md`：FDCAN3 总线关闭分析
- `AGENTS.md`：早期的仓库指南（本文件是其扩展版本）
- `SCH_Schematic1_2026-05-09.pdf`：硬件原理图
