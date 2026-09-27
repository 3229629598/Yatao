import asyncio
import re
import ctypes
from stm32_ble import STM32BLEClient


# STM32 P2P Server 的特征值 UUID
SERVICE_UUID = "0000fe40-cc7a-482a-984a-7f2ed5b3e58f"
WRITE_UUID   = "0000fe41-8e22-4541-9d4c-21edae82ed19"
NOTIFY_UUID  = "0000fe42-8e22-4541-9d4c-21edae82ed19"


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

    rgb_data = [bytearray(3) for _ in range(4)]
    flags = ctypes.c_uint16(0)

    # 2. 开启接收
    def ble_rx(packet: bytes):
        rgb_data[packet[0] // 3][packet[0] % 3] = packet[1]        
        flags.value |= (1 << packet[0])
        if flags.value == 0x0fff:
            print(f"\n📥 收到数据: {[list(row) for row in rgb_data]}")
            flags.value = 0
            print("\n请输入要发送的数据: ", end="", flush=True)
        if packet[0] == 11:
            flags.value = 0
        # print(f"\n📥 收到 STM32 回复: {packet.hex(' ')}  (长度 {len(packet)})")
        # print("请输入要发送的数据（直接回车退出）: ", end="", flush=True)

    await ble.start_receive(callback=ble_rx, expected_length=0)

    # 3. 打印操作说明
    print("\n" + "=" * 55)
    print("✅ 已连接，可以开始发送数据")
    print("  输入示例：")
    print("    1 2 3              → 发送 0x01 0x02 0x03")
    print("    0x01 0x02 0x03     → 发送 0x01 0x02 0x03")
    print("    AA BB CC           → 发送 0xAA 0xBB 0xCC")
    print("    0A0B0C             → 发送 0x0A 0x0B 0x0C")
    print("    q / exit / 回车     → 退出程序")
    print("=" * 55)

    # 4. 主循环：读取终端输入并发送
    loop = asyncio.get_event_loop()
    try:
        while True:
            # 用 run_in_executor 避免阻塞 asyncio 事件循环
            line = await loop.run_in_executor(
                None, lambda: input("\n请输入要发送的数据: ")
            )

            line = line.strip()
            if line.lower() in ("q", "quit", "exit", ""):
                print("退出程序")
                break

            data = parse_input(line)
            if data is None:
                print("❌ 输入格式错误，请重新输入")
                continue

            if len(data) > 20:
                print("⚠️ 数据太长，BLE 单次建议不超过 20 字节")
                continue

            print(f"📤 发送: {data.hex(' ')}  (长度 {len(data)})")
            ok = await ble.send(data)
            if not ok:
                print("❌ 发送失败")

    except KeyboardInterrupt:
        print("\n用户中断")
    finally:
        await ble.disconnect()


if __name__ == "__main__":
    asyncio.run(main())