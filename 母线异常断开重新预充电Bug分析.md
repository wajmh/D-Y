# 母线异常断开后无法重新进入预充电的Bug分析

## 问题描述
当母线异常断开欠压后瞬间恢复，示波器没有看到重新进入预充电，系统无法自动恢复供电。

## 根本原因：死锁循环

### 代码位置
`Core/Src/gpio.c` 中的 `Power_DischargeModeTask()` 函数（第319-335行和第356-378行）

### 死锁流程

```
1. 母线电压 < 20V 持续 20ms
   ↓
2. 触发异常断开逻辑（第356-378行）：
   - 关闭所有MOS（主放电、回充、预充）
   - 关闭外设供电（PERIPHERAL_POWER_Pin = OFF）  ← 关键问题
   - 关闭12V/24V DCDC
   - 设置 battery1AbnormalDisconnected = 1
   - 状态切换到 POWER_BATTERY_STATE_OFF
   ↓
3. 在OFF状态下尝试恢复（第319-335行）：
   - 检测到 battery1AbnormalDisconnected = 1
   - 需要满足条件：bat1CanAlive != 0 && bat1BmsAllowed != 0
   ↓
4. 【死锁点】外设供电已关闭 → CAN收发器断电
   ↓
5. 无法接收BMS CAN消息
   ↓
6. bat1CanAlive 永远为 0
   ↓
7. 恢复条件永远不满足
   ↓
8. 无法调用 Power_StartBattery1PreDischarge()
   ↓
9. 外设供电无法重新打开 → 回到步骤4（死锁）
```

## 代码片段分析

### 异常断开处理（第356-378行）
```c
if ((now - vbusLowVoltageStartTick) >= POWER_VBUS_DISCONNECT_DEBOUNCE_MS)
{
  /* 确认异常断开：切断主放电、回充、预充 MOS，并关断 12V/24V DCDC 与外设供电 */
  HAL_GPIO_WritePin(BAT1_DISCHARGE_MOS_GPIO_Port, BAT1_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_RECHARGE_MOS_GPIO_Port, BAT1_RECHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(BAT1_PRE_DISCHARGE_MOS_GPIO_Port, BAT1_PRE_DISCHARGE_MOS_Pin, POWER_SWITCH_OFF);
  HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_OFF);  // ← 问题1
  Power_SetDcdc12V(0U);  // ← 问题2
  Power_SetDcdc24V(0U);  // ← 问题3
  
  // ... 省略反电势泄放关闭代码 ...
  
  powerPreDischargeBatteryIndex = 0U;
  battery1Control.state = POWER_BATTERY_STATE_OFF;
  battery1AbnormalDisconnected = 1U;  // ← 设置异常断开标志
  battery1AlarmStatus = BATTERY_ALARM_STATUS_REMOVED;
  vbusLowVoltageStartTick = 0U;
  return;
}
```

### 恢复逻辑（第319-335行）
```c
if (battery1Control.state == POWER_BATTERY_STATE_OFF)
{
  if (battery1AbnormalDisconnected != 0U)
  {
    /* 经历过异常断开：必须等待 BMS 通信就绪且允许放电才重新启动预放电 */
    if ((bat1CanAlive != 0U) && (bat1BmsAllowed != 0U))  // ← 永远无法满足
    {
      battery1AbnormalDisconnected = 0U;
      Power_StartBattery1PreDischarge();  // ← 永远无法执行到这里
    }
  }
  else
  {
    /* 冷启动模式：在宽限期内直接启动软启动预放电 */
    Power_StartBattery1PreDischarge();
  }
}
```

## 修复方案

### 方案1：先恢复外设供电，等待通信恢复（推荐）

在OFF状态下，如果检测到VBUS电压已经恢复正常（例如 > 30V），先打开外设供电和DCDC，等待CAN通信恢复后再判断是否进入预充电。

```c
if (battery1Control.state == POWER_BATTERY_STATE_OFF)
{
  if (battery1AbnormalDisconnected != 0U)
  {
    float currentVbus = ADC_GetVbusVoltage();
    
    /* 如果VBUS电压已恢复到正常范围，先恢复外设供电以恢复CAN通信 */
    if (currentVbus >= 30.0f)  // 高于正常工作电压阈值
    {
      HAL_GPIO_WritePin(PERIPHERAL_POWER_GPIO_Port, PERIPHERAL_POWER_Pin, POWER_SWITCH_ON);
      Power_SetDcdc12V(1U);
      Power_SetDcdc24V(1U);
    }
    
    /* 等待 BMS 通信就绪且允许放电才重新启动预放电 */
    if ((bat1CanAlive != 0U) && (bat1BmsAllowed != 0U))
    {
      battery1AbnormalDisconnected = 0U;
      Power_StartBattery1PreDischarge();
    }
  }
  else
  {
    /* 冷启动模式：在宽限期内直接启动软启动预放电 */
    Power_StartBattery1PreDischarge();
  }
}
```

### 方案2：异常断开时保持外设供电（备选）

在异常断开时不关闭外设供电，仅关闭主放电、回充和预充MOS，保持CAN通信能力。

**风险评估：** 需要评估在电池断开情况下，外设供电的电源来源是否安全。

### 方案3：添加超时自动重试机制

如果在异常断开后一段时间内（例如5秒），无法满足BMS通信恢复条件，则：
- 先尝试恢复外设供电
- 或者放宽恢复条件，允许在没有BMS确认的情况下尝试预充电

## 测试验证建议

1. **示波器监测点：**
   - VBUS母线电压
   - 外设供电引脚（PA5）
   - 预充电MOS控制引脚（PB9）
   - 主放电MOS控制引脚（PB6）

2. **测试步骤：**
   - 正常上电进入放电状态
   - 模拟母线欠压（断开电池或短时拉低VBUS）
   - 恢复母线电压
   - 观察外设供电和预充电MOS是否重新启动

3. **预期行为：**
   - VBUS恢复后，应先看到外设供电恢复
   - 然后等待CAN通信恢复（可通过LED2观察）
   - 最后启动400ms预充电过程（LED3应2Hz闪烁）
   - 预充电完成后主放电MOS导通

## 其他发现

### 潜在的时序问题
`bat1CanAlive` 的判定依赖于CAN消息的接收时间戳（第536-537行）：
```c
return (((now - battery1_can_status_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS) &&
        ((now - battery1_can_mos_last_rx_tick) <= POWER_BATTERY_CAN_TIMEOUT_MS)) ? 1U : 0U;
```

即使外设供电恢复，从外设供电打开到CAN收发器初始化再到接收到BMS消息，可能需要几十到几百毫秒的时间。在此期间，恢复条件仍然不满足。

### 建议增加的调试日志
在异常断开和恢复逻辑中增加调试输出，便于排查：
- 异常断开触发时的VBUS电压
- 尝试恢复时的 bat1CanAlive 和 bat1BmsAllowed 状态
- 外设供电状态变化
- CAN通信恢复时间戳
