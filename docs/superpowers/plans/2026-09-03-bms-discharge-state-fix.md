# BMS 放电状态判定修复实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Do not create test code unless the user explicitly requests it.

**Goal:** 修复 BMS 明确关闭电池包内部放电 MOS 时被误报为 CAN 掉线、并触发错误保电策略的问题。

**Architecture:** 将当前 `Power_IsBatteryCanReady()` 拆为通信存活、BMS 放电许可和本地放电路径三个独立概念。物理在线且纯 CAN 超时时保持已建立的本地放电路径；CAN 正常但 BMS 禁止放电时，关闭对应电源板本地路径并上报独立故障状态，避免把 BMS 保护动作伪装成通信故障。

**Tech Stack:** STM32G474 HAL、C11、CMake/Ninja、三路 FDCAN、GPIO 电源状态机。

## Global Constraints

- 保留 STM32CubeMX `USER CODE BEGIN/END` 区域和现有两空格 C 风格。
- 不改变既有 FDCAN 电池状态帧、MOS 状态帧的 ID、字节序和 2 秒超时基准。
- 不允许 CAN 掉线策略绕过物理拔出判定，也不允许绕过 BMS 明确的禁止放电状态。
- 在硬件协议定义未确认前，不把 MOS 状态值 `0` 解释为永久故障；必须区分“明确禁止”和“状态未知”。

---

### Task 1: 确认 BMS MOS 状态协议与故障策略

**Files:**
- Read: `Core/Src/fdcan.c:241-287`
- Read: `Core/Src/gpio.c:329-347`
- Read: `BATTERY_CAN_DISCONNECT_SAFETY_PLAN.md`

**Interfaces:**
- Consumes: BMS 状态帧和 MOS 状态帧的实际协议定义、BMS 内部 MOS 与电源板 `PA0/PA5` 的串联关系。
- Produces: 明确的 `BMS_DISCHARGE_BLOCKED` 状态语义，以及该状态下是否关闭对应本地 MOS 的决策。

- [ ] **Step 1: 核对报文定义**
  确认 MOS 状态帧 `rxData[1] == 0` 是否严格表示 BMS 主动禁止放电，还是也可能表示初始化、未知或报文无效；确认状态帧/MOS 帧的发送周期和恢复条件。

- [ ] **Step 2: 固化控制策略**
  推荐策略为：CAN 正常且 BMS 明确禁止放电时，关闭对应电源板本地放电 MOS 和回充 MOS；不调用双电池总关断，另一块电池继续独立运行；外设供电是否保持由物理在线状态单独决定。

### Task 2: 拆分 CAN 存活与 BMS 放电许可判定

**Files:**
- Modify: `Core/Src/gpio.c:329-347`
- Modify: `Core/Inc/gpio.h:80-115`
- Modify: `Core/Src/fdcan.c:266-287`

**Interfaces:**
- Consumes: `battery*_can_rx_count`, `battery*_can_mos_rx_count`, 两组最近接收 tick，以及 `battery*_can_discharge_mos_state`。
- Produces: 独立的通信存活判定、BMS 放电许可判定，以及可供告警报文读取的 BMS 禁止和状态未知状态。

- [ ] **Step 1: 增加独立谓词**
  将通信存活定义为计数非零且状态帧、MOS 帧均未超过 `POWER_BATTERY_CAN_TIMEOUT_MS`；该谓词不得检查 MOS 状态值。将 BMS 放电许可定义为“最近一次有效 MOS 状态帧明确允许放电”。

- [ ] **Step 2: 保留状态有效性**
  如果协议存在未知/初始化值，增加有效标志或枚举，不要用默认的 `0` 伪造“明确禁止”。只有有效帧中的明确禁止值才进入 `BMS_DISCHARGE_BLOCKED`。

- [ ] **Step 3: 统一状态码**
  在 `gpio.h` 中增加独立的 BMS 禁止放电和状态未知状态码，并确保 `fdcan.c` 的 `0x04400000U` 报文 Byte 0/1 能区分正常、CAN 掉线、物理拔出、BMS 禁止放电和 BMS 状态未知。

### Task 3: 重构放电状态机的优先级

**Files:**
- Modify: `Core/Src/gpio.c:197-260`
- Modify: `Core/Src/gpio.c:506-604`

**Interfaces:**
- Consumes: Task 2 的通信存活、BMS 放电许可、物理在线状态和当前本地状态机状态。
- Produces: 不会错误保电或错误重新接入的双电池路径控制。

- [ ] **Step 1: 明确每路决策顺序**
  对每块电池按以下优先级处理：物理拔出 -> 关闭本地路径；CAN 正常且 BMS 禁止放电 -> 关闭本地路径并保持 BMS 故障状态；CAN 正常且 BMS 允许放电 -> 使用现有预放电/放电流程；CAN 超时且物理在线 -> 仅在本地已处于 `DISCHARGE` 时保持本地 MOS，不从 `OFF` 状态启动新路径。

  CAN 正常但 BMS 状态未知或无效时，关闭对应本地路径并上报独立状态，不得套用 CAN 掉线保电策略。

  最近 MOS 状态帧明确禁放或无效时，即使另一类 BMS 状态帧已经超时，也必须优先关闭对应本地路径；只有没有新鲜禁放/无效 MOS 状态时才允许进入纯 CAN 掉线分支。

- [ ] **Step 2: 防止错误恢复**
  BMS 禁止放电后，本地路径必须回到 `POWER_BATTERY_STATE_OFF`；BMS 恢复允许放电后重新走现有预放电流程，不得直接因旧状态或本地 MOS 电平重新接入母线。

  使用独立的预放电电池索引保证双路预放电互斥；该索引不得承担“全局预放电已完成”的含义，每一路从 `OFF` 恢复时都必须完成自身的预放电。

- [ ] **Step 3: 保持双电池隔离**
  单路 BMS 禁止放电只调用对应的 `Power_DisableBatteryXPath()`；只有两路均物理拔出时才执行现有 `Power_AllMosOff()`，避免错误切掉仍可用的另一块电池和外设电源。

### Task 4: 更新告警发送边界并做编译验证

**Files:**
- Modify: `Core/Src/fdcan.c:373-416`
- Modify: `Core/Inc/fdcan.h:74-84`

**Interfaces:**
- Consumes: Task 2 生成的四态告警状态。
- Produces: 状态含义稳定的 `0x04400000U` 周期报文，包含正常、CAN 掉线、物理拔出、BMS 禁止和 BMS 状态未知五种状态。

- [ ] **Step 1: 校准发送逻辑**
  保持异常状态 50 ms 周期发送；在状态从异常恢复为正常时至少发送一次全零状态帧，避免小脑永久保持降级模式。若协议决定正常态不发送，则必须在小脑端定义超时清除规则并写入协议文档。

- [ ] **Step 2: 检查发送资源**
  确认告警帧与电流帧、急停帧共用 FDCAN3 Tx FIFO 时不会造成持续满队列；发送失败不得改变本地安全状态。

  正常清除帧的发送次数和发送时间只在报文成功加入 Tx FIFO 后更新，队列忙或发送失败时继续重试，不能把失败调用计为已发送。

- [ ] **Step 3: 执行已有工具链验证**
  使用仓库已有的交叉编译器执行 `gpio.c`、`fdcan.c` 语法检查；环境具备 CMake/Ninja 时再执行 `cmake --preset Debug && cmake --build --preset Debug`。不新增测试代码。

## Review Checklist

- [ ] BMS 内部 MOS 状态与电源板本地 MOS 状态在命名和逻辑上完全分离。
- [ ] CAN 超时、BMS 禁止放电、BMS 状态未知、物理拔出四种事件不会互相覆盖。
- [ ] BMS 恢复后必须重新经过允许条件和预放电流程。
- [ ] 两路预放电不会同时进行，且任一路从 `OFF` 恢复都不会绕过自身预放电。
- [ ] 单路故障不会误关另一块电池；双路物理拔出仍执行总关断。
- [ ] `0x04400000U` 的状态码与小脑端清除/恢复规则一致。
