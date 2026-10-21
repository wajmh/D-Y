# 母线异常断开Bug修复 - 关键信息

## ⚠️ Bug严重性：高危（硬短路风险）

### 问题现象
示波器观察到：VBUS跌到10V后直接拉到80V，**没有预充电**，触发BMS短路保护。

### 真正原因
**消抖期间VBUS恢复导致无预充电硬短路：**
1. 母线欠压（$V_{BUS} < 30\text{V}$，低于正常工作范围）开始 10ms 消抖
2. 如果在 10ms 内 VBUS 恢复（电池接触不良瞬间恢复或开关反弹）
3. 旧逻辑曾清零计时器，继续保持 DISCHARGE 状态
4. **主放电 MOS 持续导通，没有预充电保护**
5. 电池 80V 直接硬短路到 VBUS → BMS 保护

### 修复方案
增加 `vbusLowVoltageDetected` 标志，并提炼 `Power_TriggerAbnormalDisconnect()`：
- 一旦检测到欠压（$V_{BUS} < 30\text{V}$）就标记
- 无论是持续欠压达到 10ms 还是在消抖期内回弹，均立即强制关闭所有 MOS
- 进入异常断开流程，**严格等待 BMS 通信就绪后重新执行 400ms 预充电**

### 关键修复代码
```c
if (currentVbus < POWER_VBUS_UNDER_VOLTAGE_THRESHOLD) // 30.0f
{
  if (vbusLowVoltageStartTick == 0U) {
    vbusLowVoltageStartTick = now;
    vbusLowVoltageDetected = 1U;  // 记住欠压
  } else if ((now - vbusLowVoltageStartTick) >= POWER_VBUS_DISCONNECT_DEBOUNCE_MS) {
    Power_TriggerAbnormalDisconnect();
    return;
  }
}
else if (vbusLowVoltageDetected != 0U)  // ← 关键
{
  /* 消抖期内恢复：立即强制关断，防止硬短路 */
  Power_TriggerAbnormalDisconnect();
  return;
}
```

## 测试验证（核心）

### 操作
快速晃动电池连接器，模拟接触不良（VBUS短暂跌落<20ms后恢复）

### 示波器监测
- CH1: VBUS - 跌落后快速恢复
- CH2: PB6主放电MOS - 应关闭后重新打开
- **CH3: PB9预充电MOS - 应出现400ms脉冲** ✓核心验证
- CH4: PA5外设供电 - 短暂关闭后恢复

### 成功标志
✅ 看到400ms预充电过程（修复前看不到）  
✅ 没有大电流冲击  
✅ **BMS不报警**

## Git提交
```
5c7cca1 - fix: 修复消抖期间VBUS恢复导致无预充电硬短路的严重Bug
0c1b6b4 - fix: 修复母线异常断开后无法重新进入预充电的死锁问题
```

## 编译状态
✅ 编译通过 - Flash: 45204字节 (42.45%)

## 详细文档
- `真正的Bug分析_消抖期恢复导致硬短路.md` - 详细技术分析
- `最终修复报告.md` - 完整修复报告
- `Bug修复验证测试方案.md` - 测试步骤

---
**修复日期**: 2026-10-20  
**优先级**: 🔴 高危 - 建议立即测试验证
