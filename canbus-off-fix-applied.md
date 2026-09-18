# CAN Bus-Off 修复机制已应用到 a10_dual_batter 分支

## 完成时间
2025年（实际操作时间）

## 修改概述
已成功将 cmake_build 分支的 canbus-off 修复机制移植到 a10_dual_batter 分支。

## 具体修改内容

### 1. 修改 Core/Inc/fdcan.h
在 `USER CODE BEGIN Prototypes` 区域添加：
- `FDCAN_CheckAndRecoverAllBusOff()` 函数声明
- Bus-Off 恢复统计变量（6个，每个 FDCAN 2个）
- Bus-Off 诊断指标变量（6个，每个 FDCAN 2个 PSR/ECR）

### 2. 修改 Core/Src/fdcan.c

#### 2.1 在 USER CODE BEGIN 0 区域添加：
- Bus-Off 恢复配置宏定义
  - `FDCAN_BUSOFF_RECOVERY_DELAY_MS` (100ms)
  - `FDCAN_BUSOFF_RECOVERY_TIMEOUT_MS` (10ms)
- 状态机枚举 `FDCAN_BusOffState_t` (5个状态)
- 为 FDCAN1/2/3 定义状态变量（使用宏批量定义）
- `FDCAN_VerifyRecovery()` - 恢复有效性验证函数
- `FDCAN_HandleInstanceBusOff()` - 单个 FDCAN 实例的状态机处理函数
- `FDCAN_CheckAndRecoverAllBusOff()` - 主轮询函数

#### 2.2 在 USER CODE END 0 之前添加：
- `HAL_FDCAN_ErrorStatusCallback()` - Bus-Off 中断回调函数
  - 单次读取 PSR 寄存器
  - 记录代际号
  - 设置 PENDING 状态

#### 2.3 修改 FDCAN_BatteryCanStart() 函数：
在 `HAL_FDCAN_Start()` 之后，为 FDCAN1/2/3 使能 Bus-Off 中断：
```c
(void)HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_BUS_OFF, 0U);
(void)HAL_FDCAN_ActivateNotification(&hfdcan2, FDCAN_IT_BUS_OFF, 0U);
(void)HAL_FDCAN_ActivateNotification(&hfdcan3, FDCAN_IT_BUS_OFF, 0U);
```

#### 2.4 在发送函数中添加 Bus-Off 门禁保护：
- `FDCAN_SendCurrentReport()` - 检查 `fdcan3_busoff_flag`
- `FDCAN_SendChargeModeStatusToRk()` - 检查 `fdcan3_busoff_flag`
- `FDCAN_ForwardBatteryFrameToRk()` - 检查 `fdcan3_busoff_flag`
- `FDCAN_SendChargeReplyToCan2()` - 检查 `fdcan2_busoff_flag`
- `FDCAN_SendBatteryWakeFrame()` - 根据 hfdcan 实例检查 `fdcan1_busoff_flag` 或 `fdcan2_busoff_flag`

### 3. 修改 Core/Src/main.c
在主循环 `while(1)` 中添加调用：
```c
FDCAN_CheckAndRecoverAllBusOff(); // ★ 纯异步执行恢复状态机
```

### 4. 确认 Core/Src/stm32g4xx_it.c
已验证中断向量配置正确：
- `FDCAN1_IT0_IRQHandler()` ✓
- `FDCAN1_IT1_IRQHandler()` ✓
- `FDCAN2_IT0_IRQHandler()` ✓
- `FDCAN2_IT1_IRQHandler()` ✓
- `FDCAN3_IT0_IRQHandler()` ✓
- `FDCAN3_IT1_IRQHandler()` ✓

所有中断都正确路由到 `HAL_FDCAN_IRQHandler()`。

## 核心特性

### 1. 中断驱动 + 异步非阻塞状态机
- 全程 0ms 忙等
- 主循环轮询兜底（防中断丢失）

### 2. 代际号机制
- 避免 ABA 竞态问题
- 防止状态机处理旧事件时被新事件覆盖

### 3. 符合 ISO 11898-1 规范
- 不强行打断硬件同步过程
- 等待硬件完成 129 × 11 bit 隐性位序列

### 4. HAL 状态恢复
- 恢复后将 `hfdcan->State` 设为 `HAL_FDCAN_STATE_BUSY`
- 防止后续发送被 HAL 库拒绝

### 5. 发送门禁保护
- 所有发送函数检查 `busoff_flag`
- Bus-Off 期间拒绝发送，防止 FIFO 积压

### 6. 诊断能力
- 保存 PSR/ECR 寄存器快照
- 统计恢复成功/失败次数
- 便于故障分析

## 状态机流程

```
Bus-Off 触发
    ↓
[中断回调] 或 [主循环兜底检测]
    ↓
PENDING (100ms 稳定期，快速检查自愈)
    ↓
REQUEST_INIT (请求进入配置模式 CCCR.INIT=1)
    ↓
清除残留 TX 队列 (TXBCR = 0x7)
    ↓
START_SYNC (请求退出配置模式 CCCR.INIT=0)
    ↓
RECOVERING (等待硬件完成 129×11 bit 同步，无超时)
    ↓
硬件自主完成 (PSR.BO=0)
    ↓
恢复 HAL 状态，清除标志
    ↓
IDLE (正常通信)
```

## 遥测变量

### 恢复统计
- `fdcan1_busoff_recovery_count` - FDCAN1 恢复成功次数
- `fdcan1_busoff_recovery_fail_count` - FDCAN1 恢复失败次数
- `fdcan2_busoff_recovery_count` - FDCAN2 恢复成功次数
- `fdcan2_busoff_recovery_fail_count` - FDCAN2 恢复失败次数
- `fdcan3_busoff_recovery_count` - FDCAN3 恢复成功次数
- `fdcan3_busoff_recovery_fail_count` - FDCAN3 恢复失败次数

### 诊断快照
- `fdcan1_last_psr` - FDCAN1 最后一次 PSR 寄存器值
- `fdcan1_last_ecr` - FDCAN1 最后一次 ECR 寄存器值
- `fdcan2_last_psr` - FDCAN2 最后一次 PSR 寄存器值
- `fdcan2_last_ecr` - FDCAN2 最后一次 ECR 寄存器值
- `fdcan3_last_psr` - FDCAN3 最后一次 PSR 寄存器值
- `fdcan3_last_ecr` - FDCAN3 最后一次 ECR 寄存器值

PSR 中的 LEC 错误码：
- 3 = ACK Error（最常见，对端未响应）
- 5 = Bit0 Error（总线短路等）
- 7 = No Change（寄存器被读取后重置）

## 注意事项

1. **不要设置超时强行打断硬件同步**
   - RECOVERING 状态完全依赖硬件自主完成
   - 强行打断会导致永久死锁

2. **所有发送必须检查 busoff_flag**
   - 新增的发送函数也需要添加门禁检查
   - 防止 Bus-Off 期间 TX FIFO 积压

3. **临界区使用 PRIMASK 保存/恢复**
   - 避免嵌套调用破坏中断状态
   - 使用 `__get_PRIMASK()` / `__set_PRIMASK()` 而不是直接 `__enable_irq()`

4. **PSR 寄存器只读取一次**
   - 读取后硬件会重置 LEC 错误码
   - 二次读取会丢失真实故障信息

## 测试建议

1. **模拟对端掉线**
   - 断开 BMS 或小脑的 CAN 连接
   - 观察 `busoff_recovery_count` 是否递增

2. **监控恢复时间**
   - 正常情况下应在 100ms + 硬件同步时间内完成
   - 硬件同步时间取决于总线负载

3. **检查发送拒绝**
   - Bus-Off 期间所有发送应返回 `HAL_BUSY`
   - 确认 `battery_can_forward_drop_count` 正确递增

4. **查看诊断快照**
   - 通过调试器或遥测读取 `fdcanX_last_psr`
   - 分析 LEC 错误码确定故障原因

## 相关文档
- 详细设计文档：`canbus-off-fix.md`
- ISO 11898-1 CAN 协议规范
- STM32G4 FDCAN 参考手册
