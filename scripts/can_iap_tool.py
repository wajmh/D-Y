#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
STM32G474 CAN 通信 IAP 在线固件升级工具 (交互式 & 命令行双模式)

支持功能:
  1. 交互式菜单控制台:
     - [1] 查询单片机运行状态与固件版本 (3位: vX.Y.Z)
     - [2] IAP 在线固件升级 (全自动一体化: 默认直接使用同级目录固件，自动切入Bootloader -> 擦除 -> 烧录 -> 校验 -> 热跳转App -> 验证新版本)
     - [3] 修改 CAN 通信参数 (通道/波特率并重连)
     - [0] 退出工具
  2. 传统命令行一键模式 (兼容CI/自动化脚本):
     - 查询当前版本: python3 scripts/can_iap_tool.py -c can0 -q
     - 一键升级App (使用同级固件): python3 scripts/can_iap_tool.py -c can0 -u --trigger
     - 指定自定义固件升级: python3 scripts/can_iap_tool.py -c can0 -f custom.bin --trigger
"""

import sys
import os
import time
import struct
import binascii
import argparse

try:
    import readline
except ImportError:
    pass

try:
    import can
except ImportError:
    can = None

# ANSI 颜色转义码
CLR_RESET   = "\033[0m"
CLR_RED     = "\033[91m"
CLR_GREEN   = "\033[92m"
CLR_YELLOW  = "\033[93m"
CLR_BLUE    = "\033[94m"
CLR_MAGENTA = "\033[95m"
CLR_CYAN    = "\033[96m"
CLR_BOLD    = "\033[1m"

# CAN 协议 ID (扩展帧 29-bit)
CAN_CMD_ID  = 0x04700000
CAN_RESP_ID = 0x04700001

# 指令字 (CMD)
CMD_PING          = 0x01
CMD_START_UPGRADE = 0x02
CMD_ERASE_APP     = 0x03
CMD_DATA_PACKET   = 0x04
CMD_VERIFY_APP    = 0x05
CMD_RUN_APP       = 0x06

# 应答状态码 (ACK)
ACK_OK            = 0x00
ACK_ERR_PARAM     = 0x01
ACK_ERR_ERASE     = 0x02
ACK_ERR_WRITE     = 0x03
ACK_ERR_CRC       = 0x04
ACK_ERR_SEQ       = 0x05

# Flash 规格
MAX_APP_SIZE      = 104 * 1024  # 104 KB


class CanIapUpdater:
    def __init__(self, channel='can0', interface='socketcan', bitrate=250000, timeout=2.0):
        self.channel = channel
        self.interface = interface
        self.bitrate = bitrate
        self.timeout = timeout
        self.bus = None

    def connect(self, quiet=False):
        """建立 CAN 总线连接"""
        if can is None:
            print(f"{CLR_RED}[错误] 未检测到 python-can 库，请先安装：{CLR_RESET}")
            print("       pip install python-can")
            sys.exit(1)

        self.close()
        if not quiet:
            print(f"{CLR_BLUE}[*] 正在连接 CAN 总线 [接口: {self.interface}, 通道: {self.channel}, 波特率: {self.bitrate}]...{CLR_RESET}")
        try:
            if self.interface == 'socketcan':
                self.bus = can.interface.Bus(channel=self.channel, interface='socketcan')
            else:
                self.bus = can.interface.Bus(channel=self.channel, interface=self.interface, bitrate=self.bitrate)
            if not quiet:
                print(f"{CLR_GREEN}[+] CAN 总线连接成功！{CLR_RESET}")
            return True
        except Exception as e:
            print(f"{CLR_RED}[!] 连接 CAN 总线失败: {e}{CLR_RESET}")
            return False

    def close(self):
        if self.bus:
            try:
                self.bus.shutdown()
            except Exception:
                pass
            self.bus = None

    def send_frame(self, data):
        """发送 8 字节扩展数据帧"""
        if not self.bus:
            return
        if len(data) < 8:
            data = data + [0x00] * (8 - len(data))
        msg = can.Message(
            arbitration_id=CAN_CMD_ID,
            data=data[:8],
            is_extended_id=True
        )
        self.bus.send(msg)

    def wait_response(self, expected_cmd, timeout=None):
        """等待接收指定指令的 ACK 帧"""
        if not self.bus:
            return None
        t_out = timeout if timeout is not None else self.timeout
        start_time = time.time()

        while (time.time() - start_time) < t_out:
            msg = self.bus.recv(timeout=0.1)
            if msg and msg.arbitration_id == CAN_RESP_ID and msg.is_extended_id:
                if len(msg.data) >= 2 and msg.data[0] == expected_cmd:
                    return msg.data
        return None

    def ping(self, retries=3):
        """握手与状态查询，支持 3 位固件版本号 (vMajor.Minor.Patch)"""
        for _ in range(retries):
            self.send_frame([CMD_PING, 0, 0, 0, 0, 0, 0, 0])
            resp = self.wait_response(CMD_PING, timeout=0.5)
            if resp:
                ack = resp[1]
                state = resp[2]  # 0x01: Bootloader, 0x02: App
                major = resp[3]
                minor = resp[4]
                patch = resp[5] if len(resp) > 5 else 0
                app_valid = resp[6] if len(resp) > 6 else (resp[5] if len(resp) > 5 else 0)
                return ack, state, major, minor, patch, app_valid
        return None

    def get_device_info(self, retries=3):
        """获取结构化设备信息字典"""
        info = self.ping(retries=retries)
        if not info:
            return None
        ack, state, major, minor, patch, app_valid = info
        state_str = "App (业务固件)" if state == 0x02 else ("Bootloader (引导程序)" if state == 0x01 else f"未知模式(0x{state:02X})")
        return {
            'ack': ack,
            'state': state,
            'state_str': state_str,
            'major': major,
            'minor': minor,
            'patch': patch,
            'version': f"v{major}.{minor}.{patch}",
            'app_valid': bool(app_valid),
            'app_valid_str': "有效 (Valid)" if app_valid else "无效/损坏 (Invalid)"
        }

    def trigger_enter_bootloader(self):
        """向正在运行的 App 发送切入 Bootloader 指令"""
        print(f"{CLR_YELLOW}[*] 尝试触发 App 软重启进入 Bootloader...{CLR_RESET}")
        self.send_frame([CMD_START_UPGRADE, 0, 0, 0, 0, 0, 0, 0])
        resp = self.wait_response(CMD_START_UPGRADE, timeout=1.0)
        if resp:
            print(f"{CLR_GREEN}[+] App 已响应升级请求，保持主放电与 DCDC 供电并热跳转至 Bootloader...{CLR_RESET}")
            time.sleep(0.3)  # 等待平滑直接跳转完成
            return True
        return False

    def start_upgrade(self, fw_size):
        """发起固件升级请求"""
        size_bytes = list(struct.pack('>I', fw_size))
        self.send_frame([CMD_START_UPGRADE] + size_bytes)
        resp = self.wait_response(CMD_START_UPGRADE, timeout=2.0)
        if not resp:
            raise RuntimeError("MCU 未响应 START_UPGRADE 指令")
        if resp[1] != ACK_OK:
            raise RuntimeError(f"MCU 拒绝升级请求，错误码: {resp[1]}")
        print(f"{CLR_GREEN}[+] MCU 接受升级请求，Flash 页大小: {resp[2]} KB，最大支持容量: {resp[3]} KB{CLR_RESET}")

    def erase_app(self):
        """擦除 App 分区 Flash"""
        print(f"{CLR_BLUE}[*] 正在擦除单片机 App Flash 分区 (请稍候)...{CLR_RESET}")
        self.send_frame([CMD_ERASE_APP, 0x5A, 0xA5, 0, 0, 0, 0, 0])
        resp = self.wait_response(CMD_ERASE_APP, timeout=10.0)
        if not resp:
            raise RuntimeError("擦除超时，MCU 无响应")
        if resp[1] != ACK_OK:
            raise RuntimeError(f"Flash 擦除失败，错误码: {resp[1]}")
        print(f"{CLR_GREEN}[+] App Flash 分区擦除成功！{CLR_RESET}")

    def flash_data(self, fw_bytes):
        """流式分包发送固件数据"""
        total_len = len(fw_bytes)
        packet_idx = 0
        offset = 0
        start_time = time.time()

        print(f"{CLR_BLUE}[*] 开始分包烧录固件 [总大小: {total_len} 字节]...{CLR_RESET}")

        retries_left = 5
        block_start_idx = 0

        while offset < total_len:
            chunk = fw_bytes[offset:offset + 4]
            chunk_len = len(chunk)

            # 数据帧打包: CMD(1) + PacketIdx(2) + Len(1) + Payload(4)
            pkt_data = [
                CMD_DATA_PACKET,
                (packet_idx >> 8) & 0xFF,
                packet_idx & 0xFF,
                chunk_len
            ] + list(chunk)

            self.send_frame(pkt_data)

            is_last = (offset + chunk_len >= total_len)
            time.sleep(0.002)  # 2ms 微延时平滑总线流量

            # 每 16 包 (64 字节) 或传输结束时等待单片机 ACK
            if (packet_idx % 16 == 15) or is_last:
                resp = self.wait_response(CMD_DATA_PACKET, timeout=3.0)
                if not resp:
                    if retries_left > 0:
                        retries_left -= 1
                        packet_idx = block_start_idx
                        offset = packet_idx * 4
                        print(f"\n{CLR_YELLOW}[!] 等待 ACK 超时，自动回退至分块起始包号 {packet_idx} 重试 (剩余重试次数: {retries_left})...{CLR_RESET}")
                        time.sleep(0.05)
                        continue
                    raise RuntimeError(f"数据包第 {packet_idx} 包等待 ACK 超时！")

                if resp[1] == ACK_ERR_SEQ:
                    expected_pkt = (resp[2] << 8) | resp[3]
                    if retries_left > 0 and expected_pkt <= (len(fw_bytes) + 3) // 4:
                        retries_left -= 1
                        packet_idx = expected_pkt
                        offset = packet_idx * 4
                        block_start_idx = (packet_idx // 16) * 16
                        print(f"\n{CLR_YELLOW}[!] 偶发丢包序号同步 (MCU 期望包号: {expected_pkt})，正在自动重传 (剩余重试次数: {retries_left})...{CLR_RESET}")
                        time.sleep(0.02)
                        continue
                    raise RuntimeError(f"包序号不同步，重传次数耗尽 (MCU 期望包号: {expected_pkt})")

                if resp[1] != ACK_OK:
                    err_msg = f"MCU 报错代码: {resp[1]}"
                    if len(resp) >= 5:
                        hal_status = resp[2]
                        flash_err = resp[3] | (resp[4] << 8)
                        err_pkt = (resp[5] << 8) | resp[6] if len(resp) >= 7 else packet_idx
                        err_names = []
                        if flash_err & 0x0002: err_names.append("OPERR(操作错误)")
                        if flash_err & 0x0008: err_names.append("PROGERR(非0xFF写入/未擦除)")
                        if flash_err & 0x0010: err_names.append("WRPERR(写保护)")
                        if flash_err & 0x0020: err_names.append("PGAERR(对齐错误)")
                        if flash_err & 0x0040: err_names.append("SIZERR(位宽错误)")
                        if flash_err & 0x0080: err_names.append("PGSERR(编程时序冲突)")
                        if flash_err & 0x4000: err_names.append("RDERR(读保护)")
                        err_desc = ", ".join(err_names) if err_names else "无硬件错误位"
                        err_msg += f" (HAL状态: {hal_status}, FlashError: 0x{flash_err:04X} [{err_desc}], 报错包号: {err_pkt})"
                    raise RuntimeError(f"数据包烧写失败，{err_msg}")

                retries_left = 5
                block_start_idx = packet_idx + 1

                # 打印动态进度条
                cur_written = offset + chunk_len
                progress = min(100.0, (cur_written / total_len) * 100.0)
                elapsed = time.time() - start_time
                speed = (cur_written / 1024.0) / elapsed if elapsed > 0 else 0
                eta = (total_len - cur_written) / (speed * 1024.0) if speed > 0 else 0

                bar_len = 30
                filled = int(bar_len * progress / 100.0)
                bar = '=' * filled + '>' + ' ' * (bar_len - filled - 1) if filled < bar_len else '=' * bar_len

                sys.stdout.write(f"\r  进度: [{bar}] {progress:5.1f}% | 已传: {cur_written}/{total_len} B | 速度: {speed:5.1f} KB/s | 剩余: {eta:4.1f}s")
                sys.stdout.flush()

            offset += chunk_len
            packet_idx += 1

        sys.stdout.write("\n")
        print(f"{CLR_GREEN}[+] 固件数据全部传输并写入完成！{CLR_RESET}")

    def verify_app(self, expected_crc):
        """执行全局 CRC32 校验"""
        print(f"{CLR_BLUE}[*] 正在请求 MCU 计算固件全局 CRC32 (预期: 0x{expected_crc:08X})...{CLR_RESET}")
        crc_bytes = list(struct.pack('>I', expected_crc))
        self.send_frame([CMD_VERIFY_APP] + crc_bytes)

        resp = self.wait_response(CMD_VERIFY_APP, timeout=5.0)
        if not resp:
            raise RuntimeError("校验超时，MCU 无响应")

        actual_crc = struct.unpack('>I', bytes(resp[2:6]))[0]
        if resp[1] == ACK_OK:
            print(f"{CLR_GREEN}[+] 校验成功！MCU 实际 CRC32: 0x{actual_crc:08X} (核对一致){CLR_RESET}")
            return True
        else:
            raise RuntimeError(f"校验失败！MCU 实际 CRC32: 0x{actual_crc:08X} != 预期: 0x{expected_crc:08X}")

    def run_app(self):
        """指示 MCU 热跳转进入 App"""
        print(f"{CLR_BLUE}[*] 正在指示 MCU 退出 Bootloader 热跳转运行新固件...{CLR_RESET}")
        self.send_frame([CMD_RUN_APP, 0x01, 0, 0, 0, 0, 0, 0])
        resp = self.wait_response(CMD_RUN_APP, timeout=1.0)
        if resp and resp[1] == ACK_OK:
            print(f"{CLR_GREEN}[+] MCU 已确认并平滑热跳转进入 App！{CLR_RESET}")
        else:
            print(f"{CLR_YELLOW}[*] 命令已送出 (MCU 即将热跳转进入 App){CLR_RESET}")


def execute_upgrade_flow(updater, fw_path, auto_trigger=True):
    """
    执行完整的在线升级流程
    :param updater: CanIapUpdater 实例
    :param fw_path: 固件文件路径 (*.bin)
    :param auto_trigger: 若 MCU 当前处于 App 模式，是否自动切入 Bootloader
    :return: bool 是否成功
    """
    # 1. 检查并读取固件
    if not os.path.isfile(fw_path):
        print(f"{CLR_RED}[!] 错误: 固件文件不存在: {fw_path}{CLR_RESET}")
        return False

    try:
        with open(fw_path, "rb") as f:
            fw_bytes = f.read()
    except Exception as e:
        print(f"{CLR_RED}[!] 打开固件文件失败: {e}{CLR_RESET}")
        return False

    fw_size = len(fw_bytes)
    if fw_size == 0:
        print(f"{CLR_RED}[!] 固件文件为空！{CLR_RESET}")
        return False
    if fw_size > MAX_APP_SIZE:
        print(f"{CLR_RED}[!] 固件大小 ({fw_size} 字节) 超出 App 分区最大允许容量 (104 KB)！{CLR_RESET}")
        return False

    crc32_expected = binascii.crc32(fw_bytes) & 0xFFFFFFFF
    print(f"\n{CLR_CYAN}------------------- 固件升级信息 -------------------{CLR_RESET}")
    print(f"  固件路径: {CLR_BOLD}{fw_path}{CLR_RESET}")
    print(f"  固件大小: {fw_size} 字节 ({fw_size / 1024.0:.2f} KB)")
    print(f"  CRC32:   0x{crc32_expected:08X}")
    print(f"{CLR_CYAN}----------------------------------------------------{CLR_RESET}")

    # 2. 状态探测与切入 Bootloader
    print(f"{CLR_BLUE}[*] 正在探测单片机当前运行状态...{CLR_RESET}")
    info = updater.get_device_info(retries=3)

    if not info and auto_trigger:
        print(f"{CLR_YELLOW}[*] 未直接收到响应，尝试下发 App 升级触发帧...{CLR_RESET}")
        updater.trigger_enter_bootloader()
        time.sleep(0.5)
        info = updater.get_device_info(retries=5)

    if not info:
        print(f"{CLR_RED}[!] 无法连接到单片机！建议检查：{CLR_RESET}")
        print("    1. CAN H/L 接线与终端电阻是否正常；")
        print(f"    2. 本地接口是否打开: sudo ip link set {updater.channel} up type can bitrate {updater.bitrate}")
        print("    3. 单片机是否已正常供电。")
        return False

    if info['state'] == 0x02:  # App 模式
        print(f"{CLR_YELLOW}[*] 单片机当前正处于 App 运行态 (固件版本: {info['version']}){CLR_RESET}")
        if not auto_trigger:
            print(f"{CLR_YELLOW}[!] 提示: 单片机处于 App 中，需切入 Bootloader 方可升级。{CLR_RESET}")
            return False

        updater.trigger_enter_bootloader()
        info = updater.get_device_info(retries=5)
        if not info:
            print(f"{CLR_RED}[!] 切换至 Bootloader 失败：单片机无应答 (超时)！{CLR_RESET}")
            return False
        if info['state'] != 0x01:
            print(f"{CLR_RED}[!] 切换至 Bootloader 失败：当前仍处于 {info['state_str']}！{CLR_RESET}")
            return False

    print(f"{CLR_GREEN}[+] 单片机握手成功！处于 Bootloader 模式 (版本: {info['version']}, 原App: {info['app_valid_str']}){CLR_RESET}")

    # 3. 升级流程
    try:
        t_start = time.time()
        # 3.1 预备升级
        updater.start_upgrade(fw_size)
        # 3.2 擦除 Flash
        updater.erase_app()
        # 3.3 分包烧写
        updater.flash_data(fw_bytes)
        t_cost = time.time() - t_start
        # 3.4 校验
        updater.verify_app(crc32_expected)
        # 3.5 启动新固件
        updater.run_app()

        print(f"\n{CLR_CYAN}===================================================={CLR_RESET}")
        print(f"{CLR_GREEN}[✔] 固件在线升级成功！总耗时: {t_cost:.2f} 秒{CLR_RESET}")
        print(f"{CLR_CYAN}===================================================={CLR_RESET}")

        # 4. 再次查询新固件版本
        print(f"{CLR_BLUE}[*] 正在验证新固件运行状态...{CLR_RESET}")
        time.sleep(0.6)  # 等待 App 向量表跳转与 FDCAN 初始化
        new_info = updater.get_device_info(retries=5)
        if new_info:
            print(f"{CLR_GREEN}[+] 新固件已成功启动！运行状态: {new_info['state_str']} | 版本号: {CLR_BOLD}{new_info['version']}{CLR_RESET}")
        else:
            print(f"{CLR_YELLOW}[*] 新固件已送入运行，若暂未响应 PING，建议重新上电验证。{CLR_RESET}")

        print(f"{CLR_YELLOW}【温馨提示】{CLR_RESET}")
        print(f"  本次为“无缝热切换”在线升级，小脑全程不断电。建议在业务空闲时对整机重新上电一次完成冷启动自检。")
        return True

    except Exception as e:
        print(f"\n{CLR_RED}[!] 升级过程异常中断: {e}{CLR_RESET}")
        return False


def find_default_firmware():
    """
    获取与 can_iap_tool.py 同级目录下的 App bin 固件文件路径。
    默认优先查找 D-Y.bin，若不存在则寻找同级目录下的任意 *.bin 文件。
    """
    script_dir = os.path.dirname(os.path.abspath(__file__))
    primary_bin = os.path.join(script_dir, "D-Y.bin")
    if os.path.isfile(primary_bin):
        return primary_bin

    # 查找同级目录下的其他 .bin 固件
    try:
        bin_files = [
            os.path.join(script_dir, f)
            for f in os.listdir(script_dir)
            if f.endswith(".bin") and not f.startswith(".")
        ]
        if bin_files:
            # 按修改时间从新到旧排序，优先使用最新固件
            bin_files.sort(key=lambda p: os.path.getmtime(p), reverse=True)
            return bin_files[0]
    except Exception:
        pass

    return primary_bin


def interactive_menu(updater):
    """交互式控制台菜单"""
    while True:
        print(f"\n{CLR_CYAN}============================================================{CLR_RESET}")
        print(f"{CLR_BOLD}{CLR_CYAN}           STM32G474 CAN IAP 在线升级交互式终端             {CLR_RESET}")
        print(f"{CLR_CYAN}============================================================{CLR_RESET}")
        print(f"  [当前CAN配置] 接口: {CLR_YELLOW}{updater.interface}{CLR_RESET} | 通道: {CLR_YELLOW}{updater.channel}{CLR_RESET} | 波特率: {CLR_YELLOW}{updater.bitrate} bps{CLR_RESET}")
        print(f"{CLR_CYAN}------------------------------------------------------------{CLR_RESET}")
        print(f"  {CLR_BOLD}[1]{CLR_RESET} 查询单片机运行状态与固件版本 (Query Version)")
        print(f"  {CLR_BOLD}[2]{CLR_RESET} IAP 在线固件升级 (IAP Online Upgrade)")
        print(f"  {CLR_BOLD}[3]{CLR_RESET} 修改 CAN 通信参数 (CAN Settings)")
        print(f"  {CLR_BOLD}[0]{CLR_RESET} 退出升级工具 (Exit)")
        print(f"{CLR_CYAN}============================================================{CLR_RESET}")

        try:
            choice = input(f"{CLR_BOLD}请选择操作 [0-3]: {CLR_RESET}").strip()
        except (KeyboardInterrupt, EOFError):
            print("\n退出工具。")
            break

        if choice == '1':
            # 选项 1: 查询版本
            print(f"\n{CLR_BLUE}[*] 正在查询单片机运行状态与固件版本...{CLR_RESET}")
            info = updater.get_device_info(retries=3)
            if not info:
                print(f"{CLR_RED}[!] 未收到单片机应答！{CLR_RESET}")
                print(f"    - 请检查物理接线，确认板子已上电；")
                print(f"    - 确认 CAN 口状态: ip link show {updater.channel}")
            else:
                print(f"{CLR_GREEN}------------------- 单片机状态报告 -------------------{CLR_RESET}")
                print(f"  当前运行模式: {CLR_BOLD}{info['state_str']}{CLR_RESET}")
                print(f"  固件版本号:   {CLR_BOLD}{CLR_GREEN}{info['version']}{CLR_RESET}  (Major={info['major']}, Minor={info['minor']}, Patch={info['patch']})")
                print(f"  App 固件有效: {info['app_valid_str']}")
                print(f"{CLR_GREEN}------------------------------------------------------{CLR_RESET}")

        elif choice == '2':
            # 选项 2: IAP 在线固件升级 (默认直接使用同级目录下的固件，无需手动输入或选择路径)
            target_path = find_default_firmware()
            if not os.path.isfile(target_path):
                print(f"\n{CLR_RED}[!] 错误: 未在同级目录下找到固件文件 (*.bin)！{CLR_RESET}")
                print(f"    预期文件路径: {target_path}")
                print(f"    请将 App 固件 (*.bin) 拷贝至脚本同级目录: {os.path.dirname(target_path)} 后重试。")
                continue

            print(f"\n{CLR_BLUE}[*] 准备使用同级目录固件升级: {CLR_BOLD}{target_path}{CLR_RESET}")
            execute_upgrade_flow(updater, target_path, auto_trigger=True)

        elif choice == '3':
            # 选项 3: 修改 CAN 参数
            print(f"\n{CLR_CYAN}--- 修改 CAN 通信参数 ---{CLR_RESET}")
            try:
                new_ch = input(f"请输入 CAN 通道名称 [{updater.channel}]: ").strip()
                if new_ch:
                    updater.channel = new_ch

                new_br_str = input(f"请输入 CAN 波特率 [{updater.bitrate}]: ").strip()
                if new_br_str:
                    try:
                        updater.bitrate = int(new_br_str)
                    except ValueError:
                        print(f"{CLR_RED}[!] 波特率必须为整数！保持原值: {updater.bitrate}{CLR_RESET}")

                print(f"{CLR_BLUE}[*] 正在使用新参数重新连接 CAN 总线...{CLR_RESET}")
                updater.connect()
            except (KeyboardInterrupt, EOFError):
                print(f"\n{CLR_YELLOW}[*] 已取消配置修改{CLR_RESET}")

        elif choice in ('0', 'q', 'exit', 'quit'):
            print(f"\n{CLR_GREEN}[*] 正在断开连接，退出升级工具。再见！{CLR_RESET}")
            break
        else:
            print(f"{CLR_RED}[!] 无效的选择，请输入 0 ~ 3 之间的数字。{CLR_RESET}")


def main():
    parser = argparse.ArgumentParser(description="STM32G474 CAN IAP 在线升级工具")
    parser.add_argument("-f", "--file", default="", help="待烧录的 App 固件文件路径 (*.bin) [可选，默认使用脚本同级目录下的固件]")
    parser.add_argument("-c", "--channel", default="can0", help="CAN 通道名称 (默认: can0)")
    parser.add_argument("-i", "--interface", default="socketcan", help="CAN 接口类型 (默认: socketcan)")
    parser.add_argument("-b", "--bitrate", type=int, default=250000, help="波特率 (默认: 250000)")
    parser.add_argument("-t", "--trigger", action="store_true", help="若单片机当前处于 App 状态，尝试下发指令软重启进入 Bootloader")
    parser.add_argument("-q", "--query", action="store_true", help="[单次模式] 仅查询当前单片机固件版本与运行状态并退出")
    parser.add_argument("-u", "--upgrade", action="store_true", help="[单次模式] 执行一键在线升级 (默认使用同级目录下的固件，无需指定路径)")
    parser.add_argument("--interactive", action="store_true", help="强制进入交互式控制台菜单模式")

    args = parser.parse_args()

    # 初始化 CAN 更新器
    updater = CanIapUpdater(
        channel=args.channel,
        interface=args.interface,
        bitrate=args.bitrate
    )

    # 判断是否进入单次非交互模式：
    # 只要用户指定了 -u (一键升级)、-f (指定固件)、-q (查询) 或 -t (触发升级)，且未强制要求 --interactive，则执行单次任务
    is_batch_mode = bool(args.upgrade or args.file or args.query or args.trigger) and not args.interactive

    if is_batch_mode:
        updater.connect()
        try:
            if args.query:
                # 仅查询版本
                print(f"{CLR_BLUE}[*] 正在查询单片机状态与固件版本...{CLR_RESET}")
                info = updater.get_device_info(retries=3)
                if not info:
                    print(f"{CLR_RED}[!] 未收到单片机应答！{CLR_RESET}")
                    sys.exit(1)
                print(f"{CLR_GREEN}[+] 单片机运行状态: {info['state_str']}{CLR_RESET}")
                print(f"{CLR_GREEN}[+] 固件版本号:     {CLR_BOLD}{info['version']}{CLR_RESET}")
                print(f"{CLR_GREEN}[+] App 固件状态:   {info['app_valid_str']}{CLR_RESET}")
                sys.exit(0)

            # 单次升级流程 (默认使用同级目录下固件，无需指定路径)
            target_path = args.file if args.file else find_default_firmware()
            if not os.path.isfile(target_path):
                print(f"{CLR_RED}[!] 错误: 固件文件不存在: {target_path}{CLR_RESET}")
                print(f"    请将固件 (*.bin) 放置于脚本同级目录或使用 -f 指定有效路径。")
                sys.exit(1)

            auto_trigger = args.trigger or args.upgrade or (not args.file)
            success = execute_upgrade_flow(updater, target_path, auto_trigger=auto_trigger)
            sys.exit(0 if success else 1)
        finally:
            updater.close()
    else:
        # 进入交互式控制台模式
        updater.connect()
        try:
            interactive_menu(updater)
        finally:
            updater.close()


if __name__ == "__main__":
    main()
