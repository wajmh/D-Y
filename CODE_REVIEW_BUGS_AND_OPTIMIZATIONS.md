# D-Y 项目代码审查与漏洞分析报告 (Bug Analysis & Optimization Guide)

---

## 摘要 (Executive Summary)

经过对本项目全套源代码（涵盖主控时钟、GPIO 状态机、双 ADC DMA 采集滤波、三路 FDCAN 网关通信以及硬件保护机制）的逐行静态代码审查与时序逻辑分析，共发现 **2 个致命级缺陷（Critical Bugs）**、**2 个严重级缺陷（Major Issues）**、**3 个中等级问题（Moderate Issues）** 以及若干代码规范与架构优化建议。

其中最核心的安全隐患包括：
1. **急停信号触发时，本地微控制器未执行 MOS 硬件级断电**（仅向上位机发送报文，底层功率回路仍持续供电）；
2. **防环流回充状态机逻辑死锁**（两电池压差平衡一次后，`rechargeCurrentOnlyMode` 被锁死在 1，后续若再次产生大压差，防倒灌保护彻底失效）；
3. **主任务循环中存在 200ms 的硬阻塞 `HAL_Delay`**，导致轮询模式的 FDCAN 硬件 FIFO 溢出丢包。

---

## 一、 缺陷等级汇总表

| 序号 | 缺陷/问题描述 | 涉及文件 | 严重等级 | 潜在影响 |
| :---: | :--- | :--- | :---: | :--- |
| **BUG-01** | **急停触发后本地未切断功率 MOS 管** | [`gpio.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/gpio.c) / [`main.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/main.c) | 🔴 **致命 (Critical)** | 急停功能在底层失灵，发生短路/失控时无法硬件断电 |
| **BUG-02** | **防环流回充逻辑单向锁死 (死锁状态机)** | [`gpio.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/gpio.c#L289-L328) | 🔴 **致命 (Critical)** | 压差平衡后保护失效，电池间大电流倒灌烧毁线缆 |
| **BUG-03** | **主循环中存在 200ms 阻塞 `HAL_Delay`** | [`gpio.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/gpio.c#L395-L414) | 🟠 **严重 (Major)** | 导致 FDCAN Rx FIFO 溢出丢包，急停响应产生 200ms 延迟 |
| **BUG-04** | **电池主回路电流缺失零点校准 (写死 1.6V)** | [`adc.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/adc.c#L234-L244) | 🟠 **严重 (Major)** | 静态零漂导致 1~2A 虚假底噪，电池电流采样失真 |
| **BUG-05** | **主循环无节拍调度导致中值/低通滤波失效** | [`main.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/main.c#L117-L128) / [`adc.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/adc.c#L79-L158) | 🟡 **中等 (Moderate)** | 同一瞬间数据快速填满滤波窗口，失去滤波作用 |
| **BUG-06** | **缺少 ADC 启动前的硬件内部校准** | [`main.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/main.c#L103-L104) / [`adc.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/adc.c#L160-L169) | 🟡 **中等 (Moderate)** | STM32G4 模拟采样偏置增大，采样精度下降 |
| **BUG-07** | **DMA 缓冲区未添加 `volatile` 关键字** | [`adc.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/adc.c#L26-L27) | 🟡 **中等 (Moderate)** | 编译器高优化等级下可能缓存寄存器导致数据不刷新 |
| **OPT-01** | **反电吸收引脚反向逻辑硬编码在业务代码中** | [`gpio.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/gpio.c#L164) | 🔵 **优化 (Minor)** | 硬件改版后容易因逻辑电平混淆烧毁吸收电阻 |
| **OPT-02** | **未使用的宏定义与死代码 (Dead Code)** | [`gpio.h`](file:///home/yq/ghf/workspace/D-Y/Core/Inc/gpio.h#L78-L82) | 🔵 **优化 (Minor)** | 电流均衡监测宏未实际实现，冗余混淆 |

---

## 二、 致命级与严重级缺陷详细剖析

---

### 🔴 BUG-01: 急停触发后本地微控制器未切断功率回路

#### 1. 问题代码定位
- 文件：[`Core/Src/gpio.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/gpio.c#L189-L214) 及 [`Core/Src/fdcan.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/fdcan.c#L373-L378)
```c
// fdcan.c 中仅在急停激活时发送 CAN 报文：
if ((Power_IsEmergencyStopActive() != 0U) &&
    ((now - estopReportLastTxTick) >= FDCAN_ESTOP_REPORT_PERIOD_MS))
{
  estopReportLastTxTick = now;
  FDCAN_SendEmergencyStopReportToRk(); // 仅仅发了 CAN 报文！
}

// gpio.c 中的主放电任务：
void Power_DischargeModeTask(void)
{
  ...
  // 此处完全没有任何关于 Power_IsEmergencyStopActive() 的判断！
  // 即使急停按钮被拍下，系统依然执行 Power_UpdateBattery1Path 等放电逻辑！
  Power_UpdateBattery1Path(battery1Present);
  Power_UpdateBattery2Path(battery2Present);
  ...
}
```

#### 2. 危害分析
急停开关（E-STOP）是工业和机器人系统中的**最高优先级硬件保护**。当前代码中，当操作员拍下急停按钮时，控制器仅通过 FDCAN3 向上位机发送了一个急停事件帧，而底层的 8 路 MOS 管和外设供电**全部保持通电！**  
如果上位机死机、CAN 总线拥堵或上位机尚未执行关机，底盘和执行器仍然带电，发生短路或飞车时将引发起火爆炸事故。

#### 3. 修复方案
在 `Power_DischargeModeTask()` 的最开始，必须增加急停判断，一旦检测到急停激活，立即切断所有 MOS 管并退出放电任务：
```c
void Power_DischargeModeTask(void)
{
  // 1. 增加硬件急停一票否决权
  if (Power_IsEmergencyStopActive() != 0U)
  {
    Power_AllMosOff();
    battery1Control.state = POWER_BATTERY_STATE_OFF;
    battery2Control.state = POWER_BATTERY_STATE_OFF;
    return;
  }

  if (dischargeModeEnabled == 0U)
  {
    return;
  }
  ...
```

---

### 🔴 BUG-02: 防环流回充状态机逻辑死锁 (`rechargeCurrentOnlyMode` 无法复位)

#### 1. 问题代码定位
- 文件：[`Core/Src/gpio.c: 289-328`](file:///home/yq/ghf/workspace/D-Y/Core/Src/gpio.c#L289-L328)
```c
static uint8_t Power_GetRechargeMaskByCanVoltage(uint8_t battery1Ready, uint8_t battery2Ready)
{
  float voltageDiff;

  if ((battery1Ready != 0U) && (battery2Ready != 0U))
  {
    if (rechargeCurrentOnlyMode == 0U) // <--- 一旦置 1，此 if 永远不进入！
    {
      voltageDiff = Power_GetBatteryCanVoltage(1U) - Power_GetBatteryCanVoltage(2U);

      if (voltageDiff > POWER_RECHARGE_BALANCE_DIFF)
      {
        return 0x01U; // 仅开 BAT1
      }

      if (voltageDiff < -POWER_RECHARGE_BALANCE_DIFF)
      {
        return 0x02U; // 仅开 BAT2
      }

      rechargeCurrentOnlyMode = 1U; // <--- 压差小于 0.1V 后置 1
    }

    return 0x03U; // 以后永远返回 0x03 (双开)！
  }

  rechargeCurrentOnlyMode = 0U; // 只有在有电池断电离线时才复位
  ...
}
```

#### 2. 危害分析
- **执行过程**：系统启动时若 BAT1 为 48.5V，BAT2 为 48.0V，系统正确地只打开 BAT1 回充 MOS；随着放电，BAT1 降至 48.05V，压差进入 0.1V 以内，`rechargeCurrentOnlyMode` 被设置为 `1`，返回 `0x03`（双开）。
- **死锁后果**：一旦 `rechargeCurrentOnlyMode = 1`，只要两块电池都在线，`if (rechargeCurrentOnlyMode == 0U)` 就**永远为假**。后续在整个运行生命周期中，**系统将不再计算压差，压差保护彻底失效**！如果后续一块电池由于大负载迅速掉电至 42V 而另一块仍为 48V，系统依然保持双路全开，高压电池将直接对低压电池进行高达几十安培的反向充电，极易烧毁线路或引起锂电池起火。

#### 3. 修复方案
防环流控制应基于**带迟滞（Hysteresis）的回差比较器**，而非单向置位锁死：
```c
#define POWER_RECHARGE_BALANCE_DIFF_HIGH   0.15f  // 退出双充的压差上限
#define POWER_RECHARGE_BALANCE_DIFF_LOW    0.08f  // 进入双充的压差下限

static uint8_t Power_GetRechargeMaskByCanVoltage(uint8_t battery1Ready, uint8_t battery2Ready)
{
  float voltageDiff;

  if ((battery1Ready != 0U) && (battery2Ready != 0U))
  {
    voltageDiff = Power_GetBatteryCanVoltage(1U) - Power_GetBatteryCanVoltage(2U);

    if (rechargeCurrentOnlyMode != 0U)
    {
      // 当前是双充模式：如果压差扩大超过 HIGH 阈值，退出双充模式
      if (voltageDiff > POWER_RECHARGE_BALANCE_DIFF_HIGH)
      {
        rechargeCurrentOnlyMode = 0U;
        return 0x01U;
      }
      else if (voltageDiff < -POWER_RECHARGE_BALANCE_DIFF_HIGH)
      {
        rechargeCurrentOnlyMode = 0U;
        return 0x02U;
      }
      return 0x03U;
    }
    else
    {
      // 当前是单充模式：压差缩小到 LOW 阈值以内才允许进入双充模式
      if (voltageDiff > POWER_RECHARGE_BALANCE_DIFF_LOW)
      {
        return 0x01U;
      }
      else if (voltageDiff < -POWER_RECHARGE_BALANCE_DIFF_LOW)
      {
        return 0x02U;
      }
      rechargeCurrentOnlyMode = 1U;
      return 0x03U;
    }
  }

  rechargeCurrentOnlyMode = 0U;
  return (battery1Ready != 0U) ? 0x01U : ((battery2Ready != 0U) ? 0x02U : 0U);
}
```

---

### 🟠 BUG-03: 主循环中存在 200ms 阻塞 `HAL_Delay`

#### 1. 问题代码定位
- 文件：[`Core/Src/gpio.c: 395-414`](file:///home/yq/ghf/workspace/D-Y/Core/Src/gpio.c#L395-L414)
```c
static void Power_EnableBattery1DischargePath(void)
{
  Power_DisableLowerRechargeMosBeforeNewDischarge(1U);
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_ON);
  HAL_Delay(POWER_PRE_DISCHARGE_DELAY); // <--- 这里调用了 HAL_Delay(200)!
  HAL_GPIO_WritePin(BACK_EMF_ABSORB_GPIO_Port, BACK_EMF_ABSORB_Pin, POWER_SWITCH_OFF);
  battery1Control.state = POWER_BATTERY_STATE_DISCHARGE;
}
```

#### 2. 危害分析
- `Power_EnableBattery1DischargePath()` 是在主循环 `while(1)` 执行期间调用的。
- 在 `HAL_Delay(200)` 运行的 200 毫秒内，主循环被完全挂起。
- **CAN 接收完全停滞**：项目的 FDCAN 采用轮询方式（在主循环中调 `FDCAN_PollBatteryRx`）。STM32G4 FDCAN 的 Rx FIFO0 硬件深度仅为 3 帧。200ms 期间 BMS 发来的周期报文必然会导致 **Rx FIFO 溢出丢包（Rx FIFO Overrun）**。
- **急停检测响应延迟 200ms**：在这 200ms 期间，急停按键输入无法被及时响应。

#### 3. 修复方案
将该延时逻辑移入非阻塞状态机中（利用 `HAL_GetTick()` 计时状态转移），严禁在主循环中调用 `HAL_Delay`。

---

### 🟠 BUG-04: 电池主回路电流未进行零点校准 (写死 1.600V)

#### 1. 问题代码定位
- 文件：[`Core/Src/adc.c: 234-244`](file:///home/yq/ghf/workspace/D-Y/Core/Src/adc.c#L234-L244)
```c
// Leg 1~4 和外设电流都使用了上电校准得到的 offset：
legCurrentSample[0] = ADC_LegCurrentFromRaw(0U, adc1_buffer[0]); // 使用了 legCurrentOffsetRaw[0]

// 但最核心的电池 1 和电池 2 电流却写死了 1.600V：
battery1Current = ADC_CurrentFromRaw(adc1_buffer[4], CURRENT_ZERO_VOLTAGE, BATTERY_CURRENT_AMPS_PER_VOLT);
battery2Current = ADC_CurrentFromRaw(adc1_buffer[5], CURRENT_ZERO_VOLTAGE, BATTERY_CURRENT_AMPS_PER_VOLT);
```

#### 2. 危害分析
电流传感器输出的零点静态电压受到供电纹波、分压电阻精度以及运放失调电压影响，极少恰好等于 1.600V（通常在 1.58V ~ 1.68V 之间分布）。  
由于传感器增益高达 $25\text{A/V}$，仅 $0.05\text{V}$ 的零点偏差就会被放大为：
$$\Delta I = 0.05\text{V} \times 25.0\text{A/V} = 1.25\text{A}$$
这会导致即使系统处于完全空载待机状态，采集到的电池电流也会显示为 $1.25\text{A}$ 的虚假电流，严重影响功率和能耗统计。

#### 3. 修复方案
将电池 1 和电池 2 的电流通道同样纳入 `ADC_CalibrateLegCurrentOffsets()` 的 500 次自校准序列中，计算得到 `battery1CurrentOffsetRaw` 和 `battery2CurrentOffsetRaw`。

---

## 三、 中等级缺陷与设计隐患剖析

---

### 🟡 BUG-05: 主循环无节拍调度导致中值与低通数字滤波失效

#### 1. 表现与原因
在 [`Core/Src/main.c`](file:///home/yq/ghf/workspace/D-Y/Core/Src/main.c#L117-L128) 中，`while (1)` 以 170MHz 主频全速狂跑，没有任何延时或调度节拍：
```c
while (1)
{
  ADC_UpdateCurrents();       // 每次循环调用中值滤波
  ADC2_UpdateBatteryVoltages();
  FDCAN_BatteryCanTask();
  Power_UpdateGpioDebugStates();
  Power_DischargeModeTask();
}
```
- 主循环一次迭代仅耗时数百纳秒，而 ADC 转换需要数微秒。
- 这意味着：在 DMA 缓冲区数据尚未更新时，**相同的 ADC 原始值在 2 微秒内被重复推入中值滤波队列 5 次**，滑动窗口瞬间被同一个值充满！
- 这种高频无意义更新使中值滤波**完全失去了在时间轴上滤除偶发尖峰毛刺的能力**，且浪费了巨量的 CPU 浮点运算资源。

#### 2. 修复建议
在主循环中引入基础的时间片调度器（例如 1ms、5ms、10ms 周期任务）：
```c
uint32_t lastTick1ms = 0;
while (1)
{
  uint32_t now = HAL_GetTick();
  if (now != lastTick1ms)
  {
    lastTick1ms = now;
    // 1ms 周期任务
    ADC_UpdateCurrents();
    ADC2_UpdateBatteryVoltages();
    Power_UpdateGpioDebugStates();
    Power_DischargeModeTask();
  }
  // CAN 报文实时轮询处理
  FDCAN_BatteryCanTask();
}
```

---

### 🟡 BUG-06: 缺少 ADC 启动前的硬件内部校准 (`HAL_ADCEx_Calibration_Start`)

#### 1. 表现与原因
STM32G4 系列微控制器内置高精度 12-bit ADC，ST 官方要求在使能 ADC 前必须执行内部失调校准：
```c
// 当前代码中直接调用了：
ADC1_StartDMA();
ADC2_StartDMA();
```
如果未先调用 `HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED)`，ADC 片上内部失调（Offset）未被校准，会带来静态绝对误差。

#### 2. 修复建议
在 `ADC1_StartDMA()` 和 `ADC2_StartDMA()` 函数内先执行硬件校准：
```c
void ADC1_StartDMA(void)
{
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_buffer, 6);
}
```

---

### 🟡 BUG-07: DMA 缓冲区缺少 `volatile` 修饰符

#### 1. 表现与原因
在 [`Core/Src/adc.c: 26-27`](file:///home/yq/ghf/workspace/D-Y/Core/Src/adc.c#L26-L27) 中：
```c
uint16_t adc1_buffer[6];
uint16_t adc2_buffer[4];
```
由于这些数组是在 DMA 中由硬件在后台直接修改内存的，CPU 本身并不感知。如果没有 `volatile` 关键字，在开启 `-O2` 或 `-O3` 编译优化时，编译器可能会把变量缓存在 CPU 寄存器中，导致软件读取到陈旧的历史数据。

#### 2. 修复建议
修改为：
```c
volatile uint16_t adc1_buffer[6];
volatile uint16_t adc2_buffer[4];
```

---

## 四、 代码优化与重构建议 (Optimization Suggestions)

1. **规范反电动势吸收逻辑的极性定义**：
   在 [`gpio.h`](file:///home/yq/ghf/workspace/D-Y/Core/Inc/gpio.h) 中增加：
   ```c
   #define BACK_EMF_ABSORB_ACTIVE   GPIO_PIN_RESET  /* 硬件反接适配 */
   #define BACK_EMF_ABSORB_INACTIVE GPIO_PIN_SET
   ```
   避免在业务逻辑中直接写死反向电平并留下“线接错了，反接下”的维护隐患。
2. **清理死代码与未使用的宏**：
   清理或实现 [`gpio.h`](file:///home/yq/ghf/workspace/D-Y/Core/Inc/gpio.h#L78-L82) 中的 `POWER_BATTERY_CURRENT_MIN_SHARE` 等未用宏。
3. **FDCAN 发送拥塞缓解机制**：
   上位机遥测上报时，如果 `HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan3) == 0`，当前直接丢弃。可引入轻量级环形发送缓冲区（RingBuffer），在主循环空闲时自动重发。

---

## 五、 总结与整改优先级建议

建议按如下优先级立即进行代码重构与整改：
1. **P0 (最高优先)**：修复 **BUG-01 (急停断电联动)** 与 **BUG-02 (防环流死锁)**，确保系统硬件与人身安全；
2. **P1 (高优先)**：修复 **BUG-03 (消除主循环阻塞延时)** 与 **BUG-04 (电池电流零点自校准)**；
3. **P2 (中优先)**：优化 **BUG-05 (主循环时间片调度)**、**BUG-06 (ADC 硬件校准)** 及 **BUG-07 (volatile 变量修饰)**。
