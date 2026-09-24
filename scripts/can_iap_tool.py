#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
STM32G474 CAN 通信 IAP 在线固件升级上位机脚本
支持 Linux 原生 SocketCAN (RK3588) 以及各类 USB-CAN 盒 (python-can)

使用方法示例:
  # 1. 直接升级已处于 Bootloader 的板子:
  python3 scripts/can_iap_tool.py -c can0 -f build/Release/D-Y.bin

  # 2. 板子当前正在运行 App，自动下发命令触发进入 Bootloader 并完成升级:
  python3 scripts/can_iap_tool.py -c can0 -f build/Release/D-Y.bin --trigger

  # 3. 使用 USB-CAN 盒 (如 candleLight gs_usb):
  python3 scripts/can_iap_tool.py -i gs_usb -c 0 -b 250000 -f build/Release/D-Y.bin
"""

import sys
import time
import struct
import binascii
import argparse

try:
    import can
except ImportError:
    can = None

# CAN 协议 ID (扩展帧 29-bit)
CAN_CMD_ID  = 0x04700000
CAN_RESP_ID = 0x04700001

# 指令字
CMD_PING          = 0x01
CMD_START_UPGRADE = 0x02
CMD_ERASE_APP     = 0x03
CMD_DATA_PACKET   = 0x04
CMD_VERIFY_APP    = 0x05
CMD_RUN_APP       = 0x06

# 应答状态码
ACK_OK            = 0x00
ACK_ERR_PARAM     = 0x01
ACK_ERR_ERASE     = 0x02
ACK_ERR_WRITE     = 0x03
ACK_ERR_CRC       = 0x04
ACK_ERR_SEQ       = 0x05


class CanIapUpdater:
    def __init__(self, channel='can0', interface='socketcan', bitrate=250000, timeout=2.0):
        self.channel = channel
        self.interface = interface
        self.bitrate = bitrate
        self.timeout = timeout
        self.bus = None

    def connect(self):
        """建立 CAN 总线连接"""
        if can is None:
            print("\033[91m[错误] 未检测到 python-can 库，请先安装：\033[0m")
            print("       pip install python-can")
            sys.exit(1)
        print(f"\033[94m[*] 正在连接 CAN 总线 [接口: {self.interface}, 通道: {self.channel}, 波特率: {self.bitrate}]...\033[0m")
        try:
            if self.interface == 'socketcan':
                self.bus = can.interface.Bus(channel=self.channel, interface='socketcan')
            else:
                self.bus = can.interface.Bus(channel=self.channel, interface=self.interface, bitrate=self.bitrate)
            print("\033[92m[+] CAN 总线连接成功！\033[0m")
        except Exception as e:
            print(f"\033[91m[!] 连接 CAN 总线失败: {e}\033[0m")
            sys.exit(1)

    def close(self):
        if self.bus:
            self.bus.shutdown()

    def send_frame(self, data):
        """发送 8 字节扩展数据帧"""
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
        t_out = timeout if timeout is not None else self.timeout
        start_time = time.time()

        while (time.time() - start_time) < t_out:
            msg = self.bus.recv(timeout=0.1)
            if msg and msg.arbitration_id == CAN_RESP_ID and msg.is_extended_id:
                if len(msg.data) >= 2 and msg.data[0] == expected_cmd:
                    return msg.data
        return None

    def ping(self, retries=3):
        """握手与状态查询"""
        for i in range(retries):
            self.send_frame([CMD_PING, 0, 0, 0, 0, 0, 0, 0])
            resp = self.wait_response(CMD_PING, timeout=0.5)
            if resp:
                ack = resp[1]
                state = resp[2]  # 0x01: Bootloader, 0x02: App
                major = resp[3]
                minor = resp[4]
                app_valid = resp[5] if len(resp) > 5 else 0
                return ack, state, major, minor, app_valid
        return None

    def trigger_enter_bootloader(self):
        """向正在运行的 App 发送切入 Bootloader 指令"""
        print("\033[93m[*] 尝试触发 App 软重启进入 Bootloader...\033[0m")
        self.send_frame([CMD_START_UPGRADE, 0, 0, 0, 0, 0, 0, 0])
        resp = self.wait_response(CMD_START_UPGRADE, timeout=1.0)
        if resp:
            print("\033[92m[+] App 已响应升级请求，保持主放电与 DCDC 供电并热跳转至 Bootloader...\033[0m")
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
        print(f"\033[92m[+] MCU 接受升级请求，Flash 页大小: {resp[2]} KB，最大支持容量: {resp[3]} KB\033[0m")

    def erase_app(self):
        """擦除 App 分区 Flash"""
        print("\033[94m[*] 正在擦除单片机 App Flash 分区 (请稍候)...\033[0m")
        self.send_frame([CMD_ERASE_APP, 0x5A, 0xA5, 0, 0, 0, 0, 0])
        # Flash 擦除需要耗费一定时间 (52页通常需 0.5s ~ 2.0s)
        resp = self.wait_response(CMD_ERASE_APP, timeout=10.0)
        if not resp:
            raise RuntimeError("擦除超时，MCU 无响应")
        if resp[1] != ACK_OK:
            raise RuntimeError(f"Flash 擦除失败，错误码: {resp[1]}")
        print("\033[92m[+] App Flash 分区擦除成功！\033[0m")

    def flash_data(self, fw_bytes):
        """流式分包发送固件数据"""
        total_len = len(fw_bytes)
        packet_idx = 0
        offset = 0
        start_time = time.time()

        print(f"\033[94m[*] 开始分包烧录固件 [总大小: {total_len} 字节]...\033[0m")

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

            offset += chunk_len
            is_last = (offset >= total_len)

            time.sleep(0.002)  # 2ms 微延时平滑总线流量，杜绝 MCU 3-frame RX FIFO 溢出

            # 每 16 包 (64 字节) 或传输结束时等待单片机 ACK
            if (packet_idx % 16 == 15) or is_last:
                resp = self.wait_response(CMD_DATA_PACKET, timeout=3.0)
                if not resp:
                    raise RuntimeError(f"数据包第 {packet_idx} 包等待 ACK 超时！")
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

                # 打印进度条
                progress = min(100.0, (offset / total_len) * 100.0)
                elapsed = time.time() - start_time
                speed = (offset / 1024.0) / elapsed if elapsed > 0 else 0
                eta = (total_len - offset) / (speed * 1024.0) if speed > 0 else 0

                bar_len = 30
                filled = int(bar_len * progress / 100.0)
                bar = '=' * filled + '>' + ' ' * (bar_len - filled - 1) if filled < bar_len else '=' * bar_len

                sys.stdout.write(f"\r  进度: [{bar}] {progress:5.1f}% | 已传: {offset}/{total_len} B | 速度: {speed:5.1f} KB/s | 剩余: {eta:4.1f}s")
                sys.stdout.flush()

            packet_idx += 1

        sys.stdout.write("\n")
        print("\033[92m[+] 固件数据全部传输并写入完成！\033[0m")

    def verify_app(self, expected_crc):
        """执行全局 CRC32 校验"""
        print(f"\033[94m[*] 正在请求 MCU 计算固件全局 CRC32 (预期: 0x{expected_crc:08X})...\033[0m")
        crc_bytes = list(struct.pack('>I', expected_crc))
        self.send_frame([CMD_VERIFY_APP] + crc_bytes)

        resp = self.wait_response(CMD_VERIFY_APP, timeout=5.0)
        if not resp:
            raise RuntimeError("校验超时，MCU 无响应")

        actual_crc = struct.unpack('>I', bytes(resp[2:6]))[0]
        if resp[1] == ACK_OK:
            print(f"\033[92m[+] 校验成功！MCU 实际 CRC32: 0x{actual_crc:08X} (核对一致)\033[0m")
            return True
        else:
            raise RuntimeError(f"校验失败！MCU 实际 CRC32: 0x{actual_crc:08X} != 预期: 0x{expected_crc:08X}")

    def run_app(self):
        """指示 MCU 热跳转进入 App"""
        print("\033[94m[*] 正在指示 MCU 退出 Bootloader 热跳转运行新固件...\033[0m")
        self.send_frame([CMD_RUN_APP, 0x01, 0, 0, 0, 0, 0, 0])
        resp = self.wait_response(CMD_RUN_APP, timeout=1.0)
        if resp and resp[1] == ACK_OK:
            print("\033[92m[+] MCU 已确认并平滑热跳转进入 App！\033[0m")
        else:
            print("\033[93m[*] 命令已送出 (MCU 即将热跳转进入 App)\033[0m")


def main():
    parser = argparse.ArgumentParser(description="STM32G474 CAN IAP 在线升级工具")
    parser.add_argument("-f", "--file", required=True, help="待烧录的 App 固件文件路径 (*.bin)")
    parser.add_argument("-c", "--channel", default="can0", help="CAN 通道名称 (默认: can0)")
    parser.add_argument("-i", "--interface", default="socketcan", help="CAN 接口类型 (默认: socketcan)")
    parser.add_argument("-b", "--bitrate", type=int, default=250000, help="波特率 (默认: 250000)")
    parser.add_argument("-t", "--trigger", action="store_true", help="若单片机当前处于 App 状态，尝试下发指令软重启进入 Bootloader")

    args = parser.parse_args()

    # 1. 读取固件文件
    try:
        with open(args.file, "rb") as f:
            fw_bytes = f.read()
    except Exception as e:
        print(f"\033[91m[!] 打开固件文件失败: {e}\033[0m")
        sys.exit(1)

    fw_size = len(fw_bytes)
    if fw_size == 0:
        print("\033[91m[!] 固件文件为空！\033[0m")
        sys.exit(1)
    if fw_size > 104 * 1024:
        print(f"\033[91m[!] 固件大小 ({fw_size} 字节) 超出 App 分区最大允许容量 (104 KB)！\033[0m")
        sys.exit(1)

    # 计算预期 CRC32 (与 MCU 内部标准算法一致)
    crc32_expected = binascii.crc32(fw_bytes) & 0xFFFFFFFF
    print(f"\033[96m====================================================\033[0m")
    print(f"\033[96m  STM32G474 CAN IAP 在线固件升级工具\033[0m")
    print(f"\033[96m====================================================\033[0m")
    print(f"  固件路径: {args.file}")
    print(f"  固件大小: {fw_size} 字节 ({fw_size / 1024.0:.2f} KB)")
    print(f"  CRC32:   0x{crc32_expected:08X}")
    print(f"----------------------------------------------------")

    updater = CanIapUpdater(
        channel=args.channel,
        interface=args.interface,
        bitrate=args.bitrate
    )
    updater.connect()

    try:
        # 2. 握手与状态查询
        print("\033[94m[*] 正在探测单片机当前运行状态...\033[0m")
        info = updater.ping(retries=3)

        if not info:
            if args.trigger:
                print("\033[93m[*] 未探测到 Bootloader 响应，尝试发送 App 升级触发帧...\033[0m")
                updater.trigger_enter_bootloader()
                time.sleep(0.5)
                info = updater.ping(retries=5)

        if not info:
            print("\033[91m[!] 无法连接到单片机！请检查 CAN 物理接线、终端电阻、波特率以及供电状态。\033[0m")
            sys.exit(1)

        ack, state, major, minor, app_valid = info
        if state == 0x02:
            print(f"\033[93m[*] 单片机当前正处于 App 运行状态 (App 版本: v{major}.{minor})\033[0m")
            if not args.trigger:
                print("\033[93m[!] 提示: 单片机正运行在 App 中，请携带 --trigger 参数以自动重启进入 Bootloader。\033[0m")
                sys.exit(1)
            updater.trigger_enter_bootloader()
            info = updater.ping(retries=5)
            if not info:
                print("\033[91m[!] 切换至 Bootloader 失败：单片机无应答 (超时)！\033[0m")
                sys.exit(1)
            if info[0] != ACK_OK or info[1] != 0x01:
                state_str = "App" if info[1] == 0x02 else f"未知(0x{info[1]:02X})"
                print(f"\033[91m[!] 切换至 Bootloader 失败：当前仍处于 {state_str} 模式 (ACK: 0x{info[0]:02X})！\033[0m")
                sys.exit(1)
            ack, state, major, minor, app_valid = info

        print(f"\033[92m[+] 单片机成功握手！处于 Bootloader 模式 (版本: v{major}.{minor}, 原App有效性: {'有效' if app_valid else '无效/已损坏'})\033[0m")

        # 3. 发送升级预备请求
        updater.start_upgrade(fw_size)

        # 4. 擦除 Flash
        updater.erase_app()

        # 5. 分包烧录数据
        t_start = time.time()
        updater.flash_data(fw_bytes)
        t_cost = time.time() - t_start

        # 6. CRC32 校验
        updater.verify_app(crc32_expected)

        # 7. 重启运行新固件
        updater.run_app()

        print(f"\033[96m====================================================\033[0m")
        print(f"\033[92m[✔] 固件在线升级圆满完成！总耗时: {t_cost:.2f} 秒\033[0m")
        print(f"\033[96m====================================================\033[0m")
        print(f"\033[93m【重要系统安全提示】\033[0m")
        print(f"\033[93m  本次升级已通过“无缝热接力（Warm Handover）”完成，上位机与动力供电全程持续在线。\033[0m")
        print(f"\033[93m  为了使系统完成 ADC 电流零点重新偏置校准以及底层外设的完全冷启动重置，\033[0m")
        print(f"\033[93m  ★ 升级完成后必须在适当时机（如机器人空闲入库后）对整体系统重新上电一次！★\033[0m")
        print(f"\033[96m====================================================\033[0m")

    except KeyboardInterrupt:
        print("\n\033[93m[!] 用户手动中止升级操作\033[0m")
    except Exception as e:
        print(f"\n\033[91m[!] 升级过程出现异常中断: {e}\033[0m")
        sys.exit(1)
    finally:
        updater.close()


if __name__ == "__main__":
    main()
