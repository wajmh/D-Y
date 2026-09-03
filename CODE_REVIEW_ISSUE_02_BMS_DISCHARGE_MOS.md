# 问题 2：BMS 放电 MOS 状态与电源板放电 MOS 的语义混淆

## 结论

这里的 BMS 是电池包内部的电池管理系统。`battery1_can_discharge_mos_state`
和 `battery2_can_discharge_mos_state` 是电池包 BMS 上报的内部放电 MOS 状态，
不是电源板本身的放电 MOS 状态。

电源板还有独立的本地放电 MOS：

- 电池 1：`BAT1_DISCHARGE_MOS_Pin`，即 `PA0`；
- 电池 2：`BAT2_DISCHARGE_MOS_Pin`，即 `PA5`。

两类 MOS 通常处于同一供电路径的不同位置：BMS 内部 MOS 决定电池包是否允许输出，
电源板 MOS 决定该电池包是否接入本板母线。因此 BMS 关闭自己的 MOS 时，电源板
无法仅靠本地 MOS 强行让该电池正常输出；但本地 MOS 是否保持开启仍然会影响后续
重新接通、故障恢复和母线安全状态。

## 当前代码的问题

`Power_IsBatteryCanReady()`（`Core/Src/gpio.c`）同时要求：

1. BMS 状态帧未超时；
2. BMS MOS 状态帧未超时；
3. BMS 上报的放电 MOS 状态为开启。

因此“CAN 通信超时”和“CAN 通信正常但 BMS 因过流、过温或其他保护主动关闭
内部放电 MOS”都会得到同一个 `false` 结果。

随后 `Power_DischargeModeTask()` 把这个结果解释为
`BATTERY_ALARM_STATUS_CAN_COMM_LOST`。如果本地状态已经是
`POWER_BATTERY_STATE_DISCHARGE`，又因为物理电压仍在线而保持电源板本地放电 MOS
开启。这样会出现以下风险：

- BMS 已明确禁止放电，但电源板没有把该事件当作“禁止放电”处理；
- 告警报文错误地报告为“CAN 通信掉线”；
- BMS 后续恢复或重新闭合内部 MOS 时，本地 MOS 可能已经处于开启状态，导致未经过
  新的完整决策就重新接入母线。

## 建议的判定模型

应至少拆成三个独立状态：

- `canCommunicationAlive`：状态帧和 MOS 状态帧是否在超时周期内更新；
- `bmsDischargeAllowed`：最近一次 BMS 报文是否允许电池放电；
- `localDischargeEnabled`：电源板自身的放电 MOS 是否开启。

只有在“CAN 通信确实超时、没有收到明确的 BMS 禁止放电状态、且电池物理电压仍
在线”时，才适用本次方案的“保持动力供电”策略。若 CAN 仍然正常但 BMS 上报内部
放电 MOS 关闭，应视为 BMS 保护/禁止放电事件，不能标记为纯 CAN 掉线并无条件保持
本地放电路径。

## 需要硬件与协议确认的事项

1. 确认 BMS MOS 状态帧中的 bit/byte 定义，确认 `0` 是否明确表示 BMS 禁止放电，
   还是也可能表示状态未知、初始化或报文无效。
2. 确认 BMS 内部 MOS 与电源板本地 MOS 的实际串联位置、默认电平和故障复位行为。
3. 明确 BMS 保护触发后，电源板应关闭本地 MOS，还是仅保持本地 MOS 状态并交由
   小脑处理；该策略不能通过“CAN ready”一个布尔值隐式决定。
