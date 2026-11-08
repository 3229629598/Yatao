import asyncio
import re
import struct
import threading
import os
import time
from datetime import datetime

from openpyxl import Workbook
from openpyxl.styles import Font
from openpyxl.utils import get_column_letter

from stm32_ble import STM32BLEClient


# STM32 P2P Server 的特征值 UUID
SERVICE_UUID = "0000fe40-cc7a-482a-984a-7f2ed5b3e58f"
WRITE_UUID   = "0000fe41-8e22-4541-9d4c-21edae82ed19"
NOTIFY_UUID  = "0000fe42-8e22-4541-9d4c-21edae82ed19"

# 通知报文格式（必须与 STM32 端 Code/Inc/config.h 的 P2P_* 宏、bringup.c 的
# p2p_channel_map[] 保持一致）
#   一条通知只发一颗传感器 = 1 字节序号 + 32 字节数据 = 33 字节，小端
#   序号字节 bit1:0 = 传感器编号，bit7 = 1 表示这颗本轮测量失败（数据无效）
#   32 字节数据 = 14 个光谱通道 × 2 字节 + clear 4 字节
SENSOR_COUNT     = 4
CH_PER_SENSOR    = 14
BYTES_PER_SENSOR = CH_PER_SENSOR * 2 + 4              # 每颗传感器 32 字节
NOTIFY_SIZE      = 1 + BYTES_PER_SENSOR               # 一条通知 33 字节
CH_NAMES = ["F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8",
            "FZ", "FY", "FXL", "NIR", "VIS_TL", "VIS_BR", "CLEAR"]

PROMPT = "\n请输入要发送的数据: "


class FrameCollector:
    """收集 4 条通知（每条只含一颗传感器），凑齐 4 颗后回调一次

    同时把数据写进 data.xlsx（和脚本同目录），4 颗传感器各一个 sheet：
      sheet 名：S0 / S1 / S2 / S3
      A1 = "时间"，B1~P1 = 15 个通道名（F1..F8、FZ、FY、FXL、NIR、VIS_TL、VIS_BR、CLEAR）
      之后每收到一组就往下追加一行：时间 + 15 个值，历史全部保留，不覆盖

    终端不再打印收到的数据，只写 Excel。
    """

    def __init__(self, on_complete=None):
        self.on_complete = on_complete
        self.frames = {}
        self.bad = {}

        # ---------- Excel 初始化：4 颗传感器各一个 sheet（表头只写一次） ----------
        self.wb = Workbook()
        self.wb.remove(self.wb.active)          # 去掉默认的空 sheet
        self.ws = {}

        for s in range(SENSOR_COUNT):
            ws = self.wb.create_sheet(f"S{s}")
            ws.cell(row=1, column=1, value="时间").font = Font(bold=True)
            for k, name in enumerate(CH_NAMES):
                ws.cell(row=1, column=2 + k, value=name).font = Font(bold=True)
                ws.column_dimensions[get_column_letter(2 + k)].width = 10
            ws.column_dimensions["A"].width = 20
            ws.freeze_panes = "B2"              # 表头固定，往下翻也能看到通道名
            self.ws[s] = ws

        timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        self.excel_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), f"data_{timestamp}.xlsx")
        self.cnt = 0            # 已收到多少组
        self.locked = False     # data.xlsx 是否正被 Excel 占用
        self._save_time = 0.0   # 上次落盘的时间（10Hz 下限制写盘频率）

    def feed(self, packet: bytes) -> bool:
        """喂一条通知；凑齐 4 颗返回 True"""
        if len(packet) != NOTIFY_SIZE:
            print(f"\n⚠️ 收到 {len(packet)} 字节，与约定的 {NOTIFY_SIZE} 字节不符")
            return False

        idx = packet[0] & 0x03               # 传感器编号 0~3
        if idx == 0:                         # 收到 0 号说明是新的一轮
            self.frames.clear()
            self.bad.clear()

        self.frames[idx] = packet[1:]        # 32 字节数据
        self.bad[idx] = bool(packet[0] & 0x80)

        if len(self.frames) == SENSOR_COUNT:
            frames = dict(self.frames)
            bad = dict(self.bad)
            self.frames.clear()
            self.bad.clear()
            self.cnt += 1
            self.write_excel(frames, bad)    # 追加到 Excel
            if self.on_complete:
                self.on_complete(frames, bad)
            return True
        return False

    def write_excel(self, frames: dict, bad: dict):
        """把这一组数据追加到各自 sheet 的末尾（每行一条记录，历史保留）"""
        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        for s in range(SENSOR_COUNT):
            ws = self.ws[s]
            row = ws.max_row + 1                # 追加到下一行
            ws.cell(row=row, column=1, value=ts)

            payload = frames.get(s)

            if payload is None or bad.get(s):
                # 这一颗本轮没有有效数据：写 "--"，而不是一串 0
                for k in range(len(CH_NAMES)):
                    ws.cell(row=row, column=2 + k, value="--")
                continue

            values = struct.unpack_from("<14HI", payload)   # 14 个 uint16 + 1 个 uint32
            for k, v in enumerate(values):
                ws.cell(row=row, column=2 + k, value=v)

        self.save_throttled()

    def save_throttled(self, interval: float = 1.0):
        """10Hz 数据下不能每来一组就把整个文件重写一遍，最多每秒存一次；
        退出时还会再存一次，不会丢数据"""
        now = time.monotonic()
        if now - self._save_time >= interval:
            self._save_time = now
            self.save()

    def save(self):
        """写回 data.xlsx。文件被 Excel 打开时会写入失败，这里只提示一次"""
        try:
            self.wb.save(self.excel_path)
            self.locked = False
        except PermissionError:
            if not self.locked:
                print(f"\n⚠️ {os.path.basename(self.excel_path)} 正被 Excel 打开，"
                      f"暂时写不进去；关闭 Excel 后会自动继续保存")
                print(PROMPT, end="", flush=True)
            self.locked = True

    def ble_rx(self, packet: bytes):
        self.feed(packet)

def parse_input(text: str):
    """
    解析终端输入，支持以下格式：
      - "1 2 3"           → [0x01, 0x02, 0x03]（十进制）
      - "0x01 0x02 0x03"  → [0x01, 0x02, 0x03]（十六进制）
      - "01 02 03"        → [0x01, 0x02, 0x03]（十六进制，无 0x 前缀）
      - "0A0B0C"          → [0x0A, 0x0B, 0x0C]（连续十六进制）
    返回 bytes，失败返回 None
    """
    text = text.strip()
    if not text:
        return None

    # 尝试按空格/逗号拆分
    parts = re.split(r"[\s,]+", text)

    # 情况1：每段都带 0x 前缀 → 十六进制
    if all(p.lower().startswith("0x") for p in parts):
        try:
            return bytes(int(p, 16) for p in parts)
        except ValueError:
            return None

    # 情况2：多个分段，每段都是 2 位以内的十六进制
    if len(parts) > 1 and all(re.fullmatch(r"[0-9a-fA-F]{1,2}", p) for p in parts):
        try:
            return bytes(int(p, 16) for p in parts)
        except ValueError:
            return None

    # 情况3：单段，可能是连续十六进制（如 "0A0B0C"），也可能是十进制
    if len(parts) == 1:
        p = parts[0]
        # 连续十六进制：长度是偶数且都是 hex 字符
        if re.fullmatch(r"[0-9a-fA-F]+", p) and len(p) % 2 == 0 and len(p) > 2:
            try:
                return bytes.fromhex(p)
            except ValueError:
                pass
        # 单个十进制数
        if p.isdigit():
            val = int(p)
            if 0 <= val <= 255:
                return bytes([val])

    # 情况4：全部是十进制数字
    if all(p.isdigit() for p in parts):
        try:
            values = [int(p) for p in parts]
            if all(0 <= v <= 255 for v in values):
                return bytes(values)
        except ValueError:
            pass

    return None


async def main():
    # 1. 创建客户端并连接
    ble = STM32BLEClient(
        device_name="STM32WB",          # 如果知道地址，用 device_address="XX:XX:XX:XX:XX:XX"
        write_char_uuid=WRITE_UUID,
        notify_char_uuid=NOTIFY_UUID,
    )

    print("正在扫描并连接 STM32 ...")
    if not await ble.connect(scan_timeout=8.0):
        print("连接失败，程序退出")
        return

    # 2. 开启接收：STM32 把 4 颗传感器分 4 条通知发过来（每条 33 字节）。
    #    收到后只写 Excel，终端不打印数据。
    collector = FrameCollector()

    await ble.start_receive(callback=collector.ble_rx, expected_length=0)

    # 3. 打印操作说明
    print("\n" + "=" * 55)
    print("✅ 已连接，可以开始发送数据")
    print("  输入示例：")
    print("    1 2 3              → 发送 0x01 0x02 0x03")
    print("    0x01 0x02 0x03     → 发送 0x01 0x02 0x03")
    print("    AA BB CC           → 发送 0xAA 0xBB 0xCC")
    print("    0A0B0C             → 发送 0x0A 0x0B 0x0C")
    print("    q / exit / 回车     → 退出程序")
    print("  (收到的数据只写入 data.xlsx，终端不显示)")
    print("=" * 55)

    # 4. 主循环：读取终端输入并发送
    # 用一个后台"守护线程"读输入，通过队列交给主循环：
    #   - Ctrl+C 时主线程能立刻退出，不会留下卡在 input() 里的线程
    #   - 也不会出现 run_in_executor 被取消时那串 CancelledError Traceback
    loop = asyncio.get_running_loop()
    input_queue = asyncio.Queue()

    def stdin_reader():
        while True:
            try:
                line = input()          # 提示符由外面统一打印，避免重复/错位
            except (EOFError, KeyboardInterrupt):
                line = None
            try:
                loop.call_soon_threadsafe(input_queue.put_nowait, line)
            except RuntimeError:
                return                 # 事件循环已经关闭
            if line is None:
                return

    print(PROMPT, end="", flush=True)
    threading.Thread(target=stdin_reader, daemon=True).start()

    try:
        while True:
            line = await input_queue.get()
            if line is None:           # 输入结束（EOF / Ctrl+C）
                print("\n退出程序")
                break

            line = line.strip()
            if line.lower() in ("q", "quit", "exit", ""):
                print("退出程序")
                break

            data = parse_input(line)
            if data is None:
                print("❌ 输入格式错误，请重新输入")
                print(PROMPT, end="", flush=True)
                continue

            if len(data) > 20:
                print("⚠️ 数据太长，BLE 单次建议不超过 20 字节")
                print(PROMPT, end="", flush=True)
                continue

            print(f"📤 发送: {data.hex(' ')}  (长度 {len(data)})")
            ok = await ble.send(data)
            if not ok:
                print("❌ 发送失败")
            print(PROMPT, end="", flush=True)

    except (KeyboardInterrupt, asyncio.CancelledError):
        print("\n用户中断")
    finally:
        # 退出时协议栈可能已经断开，这里不要让清理动作再抛异常
        try:
            collector.save()                   # 退出前再存一次（冲掉之前被占用没存上的）
        except Exception:
            pass
        try:
            await ble.disconnect()
        except Exception:
            pass


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except (KeyboardInterrupt, asyncio.CancelledError):
        # Ctrl+C 退出时静默结束，不打 Traceback
        pass
