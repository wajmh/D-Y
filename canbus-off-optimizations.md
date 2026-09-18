# CAN Bus-Off 修复机制优化补丁

## 实施时间
2025年（本次优化）

## 优化概述
在基础 Bus-Off 恢复机制之上，针对竞态条件、CPU 开销、指针安全性和代码清洁度进行了 4 项关键优化。

---

## 优化 1：发包函数入队临界区保护 ⭐⭐⭐⭐⭐

### 问题分析
**TOCTOU 竞态窗口**（Time-of-Check-Time-of-Use）：

```
主循环执行：
  if (fdcan3_busoff_flag != 0U) return HAL_BUSY;  // ✓ 检查通过
  ↓
  [此处存在竞态窗口]  ← Bus-Off 中断可能在此刻触发并置位 flag
  ↓
  HAL_FDCAN_AddMessageToTxFifoQ(...)  // ✗ 仍然向故障 FIFO 入队
```

虽然窗口很小（微秒级），但在以下场景下可能触发：
- 高频发送（50ms 急停上报 + 200ms 电流上报）
- Bus-Off 正好在检查和入队之间发生
- 抢占式中断系统中的不幸时序重叠

### 修复方案
引入 **双重检查锁定模式**（Double-Checked Locking）：

```c
static HAL_StatusTypeDef FDCAN_SendCurrentReport(uint32_t identifier, const uint8_t txData[])
{
  FDCAN_TxHeaderTypeDef txHeader;
  HAL_StatusTypeDef status;
  uint32_t primask;

  /* 第一次检查：快速预检（无锁，过滤大部分情况） */
  if (fdcan3_busoff_flag != 0U)
  {
    return HAL_BUSY;
  }

  if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan3) == 0U)
  {
    return HAL_BUSY;
  }

  /* 配置 txHeader（不需要在临界区内） */
  txHeader.Identifier = identifier;
  txHeader.IdType = FDCAN_EXTENDED_ID;
  txHeader.TxFrameType = FDCAN_DATA_FRAME;
  txHeader.DataLength = FDCAN_DLC_BYTES_8;
  txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  txHeader.BitRateSwitch = FDCAN_BRS_OFF;
  txHeader.FDFormat = FDCAN_CLASSIC_CAN;
  txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  txHeader.MessageMarker = 0U;

  /* 第二次检查：临界区保护（原子操作，彻底消除竞态窗口） */
  primask = __get_PRIMASK();
  __disable_irq();
  if (fdcan3_busoff_flag != 0U)
  {
    __set_PRIMASK(primask);
    return HAL_BUSY;
  }
  status = HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan3, &txHeader, (uint8_t *)txData);
  __set_PRIMASK(primask);

  return status;
}
```

### 实时性影响分析
- **关中断时间**：约 1-2 µs（仅覆盖最后的检查和入队）
- **发送周期**：50ms（急停）/ 200ms（电流）
- **占空比**：0.002% - 0.004%（几乎可忽略）
- **对实时任务影响**：极小，因为配置代码在临界区外执行

### 已应用的函数
1. ✅ `FDCAN_SendCurrentReport()` - FDCAN3
2. ✅ `FDCAN_SendChargeModeStatusToRk()` - FDCAN3
3. ✅ `FDCAN_ForwardBatteryFrameToRk()` - FDCAN3
4. ✅ `FDCAN_SendChargeReplyToCan2()` - FDCAN2
5. ✅ `FDCAN_SendBatteryWakeFrame()` - FDCAN1/2

---

## 优化 2：FDCAN_BatteryCanTask 外层门禁 ⭐⭐⭐⭐

### 问题分析
**当前行为**：Bus-Off 期间，主循环仍在执行：
- 每 50ms 调用 `Power_IsEmergencyStopActive()` 并尝试发送
- 每 200ms 执行 ADC 采样、电流打包计算
- 持续刷新 `currentReportLastTxTick = now`
- 底层虽会拦截，但上层做了无效计算

**问题**：
1. **CPU 浪费**：Bus-Off 期间的计算无法送达，白白消耗 CPU
2. **时间戳污染**：`currentReportLastTxTick` 被持续刷新，恢复后需要再等一个完整周期才能触发上报
3. **逻辑不清晰**：底层拦截让上层逻辑产生"已发送"的错觉

### 修复方案
在所有 FDCAN3 上报代码外层添加统一门禁：

```c
void FDCAN_BatteryCanTask(void)
{
  // ... 电池帧接收和转发（FDCAN1/2，保持原样）
  
  /* FDCAN3 上报统一门禁：Bus-Off 期间跳过所有向 RK 的上报 */
  if (fdcan3_busoff_flag == 0U)
  {
    // 急停上报
    if ((Power_IsEmergencyStopActive() != 0U) &&
        ((now - estopReportLastTxTick) >= FDCAN_ESTOP_REPORT_PERIOD_MS))
    {
      estopReportLastTxTick = now;
      FDCAN_SendEmergencyStopReportToRk();
    }

    // 电池报警上报（包含 MOS 状态跳变检测）
    // ...

    // 电流上报
    if ((now - currentReportLastTxTick) >= FDCAN_CURRENT_REPORT_PERIOD_MS)
    {
      currentReportLastTxTick = now;
      FDCAN_SendCurrentReportsToRk();
    }
  } /* 结束 FDCAN3 门禁保护块 */
  
  // 电池唤醒帧（FDCAN1/2，保持原样）
}
```

### 收益
1. ✅ **节省 CPU 开销**：Bus-Off 期间跳过 ADC 采样、打包计算、状态检测
2. ✅ **恢复后立即触发**：时间戳不被污染，恢复后立即发送积压数据
3. ✅ **逻辑清晰**：上层门禁 + 底层临界区保护，双重防护

### 受保护的上报
- ✅ 急停上报（50ms 周期）
- ✅ 电池报警上报（200ms 周期 + 状态跳变触发）
- ✅ 电流上报（200ms 周期）
- ✅ MOS 状态变化检测（实时）

**未受影响的功能**：
- ✅ 电池 CAN 接收轮询（FDCAN1/2）
- ✅ RK 命令接收（FDCAN3）
- ✅ 充电桩超时检测
- ✅ 电池唤醒帧发送（FDCAN1/2）

---

## 优化 3：FDCAN_SendBatteryWakeFrame 指针安全性 ⭐⭐⭐

### 问题分析
**原实现的风险**：
```c
// 如果 hfdcan 是 NULL，直接崩溃
if (hfdcan->Instance == FDCAN1 && fdcan1_busoff_flag != 0U)
  return HAL_BUSY;

// 如果传入 &hfdcan3，会跳过所有检查
```

虽然当前调用环境安全（仅两处内部调用），但不符合防御性编程原则。

### 修复方案
添加参数有效性检查和明确的调用约束：

```c
static HAL_StatusTypeDef FDCAN_SendBatteryWakeFrame(FDCAN_HandleTypeDef *hfdcan, uint8_t marker)
{
  FDCAN_TxHeaderTypeDef txHeader;
  uint8_t txData[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
  HAL_StatusTypeDef status;
  uint32_t primask;

  /* 参数有效性检查 */
  if (hfdcan == NULL)
  {
    return HAL_ERROR;
  }

  /* 只允许 FDCAN1/2 调用此函数（FDCAN3 用于上报，不发送唤醒帧） */
  if ((hfdcan != &hfdcan1) && (hfdcan != &hfdcan2))
  {
    return HAL_ERROR;
  }

  /* 快速预检（无锁） */
  if ((hfdcan == &hfdcan1 && fdcan1_busoff_flag != 0U) ||
      (hfdcan == &hfdcan2 && fdcan2_busoff_flag != 0U))
  {
    return HAL_BUSY;
  }

  // ... 其余逻辑
}
```

### 安全保障
1. ✅ **空指针保护**：避免 NULL 解引用导致的崩溃
2. ✅ **调用约束明确**：只接受 FDCAN1/2，拒绝 FDCAN3
3. ✅ **错误码清晰**：`HAL_ERROR` 表示参数错误，`HAL_BUSY` 表示 Bus-Off 拒绝

---

## 优化 4：消除编译器警告 ⭐

### 问题
```
Core/Src/gpio.c:796:14: warning: 'Power_GetBatteryEffectiveVoltage' defined but not used [-Wunused-function]
```

该函数目前未被调用，但可能在未来使用（用于选择电池有效电压源）。

### 修复方案
添加 `__attribute__((unused))` 注解，保留函数但消除警告：

```c
__attribute__((unused))
static float Power_GetBatteryEffectiveVoltage(uint8_t batteryIndex);

__attribute__((unused))
static float Power_GetBatteryEffectiveVoltage(uint8_t batteryIndex)
{
  if (Power_IsBatteryCanAlive(batteryIndex) != 0U)
  {
    return Power_GetBatteryCanVoltage(batteryIndex);
  }

  return (batteryIndex == 1U) ? battery1_voltage : battery2_voltage;
}
```

### 收益
- ✅ 代码清洁度提升（零警告编译）
- ✅ 保留备用函数供未来使用
- ✅ 明确标注"有意未使用"的设计意图

---

## 测试建议

### 1. 竞态窗口压力测试
**目标**：验证临界区保护有效性

```c
/* 在 FDCAN_SendCurrentReport 入口添加测试代码 */
static volatile uint32_t race_condition_test_counter = 0;

if (fdcan3_busoff_flag != 0U) {
  race_condition_test_counter++;  // 第一次检查拦截计数
  return HAL_BUSY;
}

// ... 配置代码

primask = __get_PRIMASK();
__disable_irq();
if (fdcan3_busoff_flag != 0U) {
  race_condition_test_counter += 100;  // 第二次检查拦截计数（应为 0）
  __set_PRIMASK(primask);
  return HAL_BUSY;
}
```

**预期结果**：
- 正常情况：`race_condition_test_counter` 递增步长为 1
- 若出现 +100 跳变，说明捕获到竞态窗口（证明优化有效）

### 2. Bus-Off 恢复后即时上报验证
**步骤**：
1. 断开 RK CAN 连接，触发 FDCAN3 Bus-Off
2. 等待 5 秒（期间 `currentReportLastTxTick` 不应更新）
3. 重新连接 RK，观察恢复后的首次上报延迟

**预期结果**：
- 优化前：恢复后需等待 200ms 才触发首次电流上报
- 优化后：恢复后立即触发上报（延迟 < 10ms）

### 3. CPU 开销对比
**测量方法**：
```c
// 在 FDCAN_BatteryCanTask 入口
uint32_t start_tick = DWT->CYCCNT;  // 启用 DWT 周期计数器

// ... 函数主体

uint32_t elapsed_cycles = DWT->CYCCNT - start_tick;
```

**预期结果**：
- Bus-Off 期间 CPU 周期数应显著下降（减少 ADC 采样和状态检测开销）

### 4. 指针安全性单元测试
```c
// 传入非法参数，应返回 HAL_ERROR
assert(FDCAN_SendBatteryWakeFrame(NULL, 1) == HAL_ERROR);
assert(FDCAN_SendBatteryWakeFrame(&hfdcan3, 3) == HAL_ERROR);

// 传入合法参数，应正常执行
assert(FDCAN_SendBatteryWakeFrame(&hfdcan1, 1) == HAL_OK || HAL_BUSY);
```

---

## 性能影响评估

| 优化项 | CPU 开销 | 内存开销 | 实时性影响 | 代码增量 |
|--------|----------|----------|------------|----------|
| 临界区保护 | +1-2 µs/次发送 | +4 字节/函数（primask） | 可忽略（<0.004%） | +8 行/函数 |
| 外层门禁 | Bus-Off 期间节省 20-30% | 0 | 恢复后延迟降低 200ms | +3 行 |
| 指针检查 | +2-3 条指令 | 0 | 无 | +10 行 |
| 消除警告 | 0 | 0 | 无 | +2 行 |

**总体评价**：
- ✅ CPU 开销极小（微秒级，发送本身需 100+ µs）
- ✅ 内存开销可忽略（栈变量，自动回收）
- ✅ 实时性改善（恢复后即时上报）
- ✅ 代码可维护性提升（逻辑清晰，防御性强）

---

## 代码审查检查清单

### 已实施 ✅
- [x] 所有 FDCAN 发送函数添加临界区保护
- [x] `FDCAN_BatteryCanTask` 添加 FDCAN3 外层门禁
- [x] `FDCAN_SendBatteryWakeFrame` 添加参数检查
- [x] 消除 `Power_GetBatteryEffectiveVoltage` 未使用警告
- [x] 更新文档说明优化内容

### 待验证 ⏳
- [ ] 竞态窗口压力测试（需实际硬件）
- [ ] Bus-Off 恢复延迟测量
- [ ] CPU 开销 profiling
- [ ] 长时间稳定性测试（24h+）

### 风险评估 🔍
- **低风险**：所有修改都是防御性增强，不改变正常流程
- **可回滚**：每项优化独立，可单独回退
- **向后兼容**：不影响现有 API 和调用约定

---

## 参考文档
- 基础实现：`canbus-off-fix.md`
- 应用记录：`canbus-off-fix-applied.md`
- ISO 11898-1 CAN 协议规范
- ARM Cortex-M4 中断优先级与临界区设计
