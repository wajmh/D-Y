# STM32G474 CAN 通信 IAP 在线升级实现完成总结报告

## 一、 工作概述

根据需求与设计方案，在独立 Git 分支 **`feat/can-iap`** 上为本项目实现了基于 CAN 总线（FDCAN2）的高可靠在线固件升级（IAP）系统。

针对“**电源管理板 12V DCDC 为上位机小脑自身供电**”以及“**RGB 状态灯依赖 24V DCDC 供电**”的特殊硬件闭环特性，本系统完成了 **“无缝热接力（Warm Handover）”** 架构设计与底层实现，彻底消除了软复位导致的上位机断电崩溃与 MOS 开关抖动风险。

---

## 二、 关键设计演进与核心改动清单

### 1. Flash 空间布局与架构设计
- **Flash 空间划分**：
  - `0x08000000 ~ 0x080057FF` (22 KB): Bootloader 引导程序
  - `0x08005800 ~ 0x08005FFF` (2 KB): IAP 元数据/标志区（记录 `0xA5A55A5A` 有效标记、固件大小与 CRC32）
  - `0x08006000 ~ 0x0801FFFF` (104 KB): App 业务应用程序分区
- **单 App 原地擦写 + 永久驻留防变砖架构**：
  鉴于 STM32G474RBT6 总 Flash 仅 128KB，摒弃了会严重压缩可用空间的 ABC 双分区（A/B Ping-Pong）模式，确保 App 独享 104KB 充裕容量；升级过程中断或校验失败将永久停留在 Bootloader 中，支持无限次重刷，绝对不惧变砖。

### 2. 硬件供电拓扑与无缝热接力（Warm Handover）
- **供电拓扑识别**：
  - 小脑（RK3588）依赖板载 12V 隔离 DCDC（PA7）；
  - RGB 声光面板依赖板载 24V 隔离 DCDC（PC4）；
  - 12V/24V DCDC 的动力源均取自母线 VBUS（由电池主放电 MOS PB6/PC11 供给）。
- **无缝热接力实施**：
  - **热切入 Bootloader**：App 收到小脑升级指令时，仅关断高危充电/泄放回路，**保持主放电 MOS 与 12V/24V DCDC 持续开启**，使用软件平滑跳转（Direct Jump）交接控制权，小脑 12V 供电持续不断，RGB 蓝灯可正常慢闪指示；
  - **热切回 App**：升级校验成功后，Bootloader 在 `TAMP->BKP1R` 记录 `HOT_BOOT_MAGIC` 并直接跳转回 App；App 启动识别热启动标志，**跳过 400ms 预充延时和关断 MOS 时序**，无缝接力工作，MOS 绝不开关一遍，电源零抖动。
  - **冷启动保持安全（Cold Boot）**：出厂首次上电、硬件掉电或看门狗异常复位时，Bootloader 保持安全防护，强制断开所有 MOS。

### 3. FDCAN 时钟与波特率重大缺陷修复
- **缺陷排查**：发现原 Bootloader 漏设 `RCC_PERIPHCLK_FDCAN`，导致 FDCAN 内核时钟默认处于 HSE (24MHz)，波特率严重偏离为 35.3 kbps；
- **修复完成**：显式配置 `RCC_PERIPHCLK_FDCAN` 为 `RCC_FDCANCLKSOURCE_PCLK1`（170MHz），配合 `Prescaler=40, Seg1=13, Seg2=3`（分频 680），实现 250 kbps 精准波特率，通信完全正常。

### 4. 核心工程与工具清单
1. **Bootloader 微型工程 (`Bootloader/`)**：
   - `Bootloader/LinkerScript.ld`：Flash/RAM 空间精准约束；
   - `Bootloader/Core/Src/boot_main.c`：系统时钟、热启动与冷启动分支判断、LED 状态指示；
   - `Bootloader/Core/Src/boot_can.c`：FDCAN2 驱动（PB13-TX, PB5-RX, 250kbps 扩展帧）及 IAP 协议状态机；
   - `Bootloader/Core/Src/boot_flash.c`：页擦除、双字编程、CRC32 查表与元数据管理；
   - `Bootloader/Core/Src/boot_jump.c`：无缝安全平滑跳转（Direct Jump）驱动。
2. **App 业务固件适配 (`Core/` & 根目录)**：
   - `STM32G474xx_FLASH.ld`：Flash 起始地址重定位至 `0x08006000`（104KB）；
   - `Core/Src/main.c`：向量表重定向 `SCB->VTOR = 0x08006000`；
   - `Core/Src/fdcan.c`：监听 `0x04700000` 指令并执行热切入跳转；
   - `Core/Src/gpio.c`：增加热启动免预充与免关断接力处理。
3. **上位机升级工具与开发文档**：
   - `scripts/can_iap_tool.py`：支持 Linux SocketCAN（RK3588 端）的自动化升级工具；
   - `CAN_IAP_GUIDE.md`：系统架构图、通信协议及实操指南。

---

## 三、 验证与构建结果

在本地完成 App 与 Bootloader 的 Debug 与 Release 全量构建验证，所有目标均 0 错误、0 警告通过编译：

| 构建目标 | 模式 | Flash 占用 | Flash 容量上限 | 使用率 |
| :--- | :--- | :--- | :--- | :--- |
| **Bootloader** | Release | **9,308 B (9.1 KB)** | 22 KB | **41.32%** |
| **Bootloader** | Debug | **13,920 B (13.6 KB)** | 22 KB | **61.79%** |
| **App (D-Y)** | Release | **26,356 B (25.7 KB)** | 104 KB | **24.75%** |
| **App (D-Y)** | Debug | **45,672 B (44.6 KB)** | 104 KB | **42.89%** |

### 加载地址校验 (ELF Program Headers)
- `Bootloader.elf`: `PhysAddr = 0x08000000`
- `D-Y.elf` (App): `PhysAddr = 0x08006000`
- 两者分区完全物理隔离，绝不重叠冲突。
