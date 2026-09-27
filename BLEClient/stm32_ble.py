import asyncio
import struct
from bleak import BleakScanner, BleakClient

class STM32BLEClient:
    """
    STM32 BLE 客户端类
    封装了扫描、连接、发送、接收、断开等操作
    """

    def __init__(self, device_name=None, device_address=None,
                 write_char_uuid=None, notify_char_uuid=None):
        """
        初始化 BLE 客户端

        :param device_name: 设备名称（如 "STM32WB"），用于扫描匹配
        :param device_address: 设备 MAC 地址（如 "XX:XX:XX:XX:XX:XX"）
        :param write_char_uuid: 写入特征值 UUID（PC -> STM32）
        :param notify_char_uuid: 通知特征值 UUID（STM32 -> PC）
        """
        self.device_name = device_name
        self.device_address = device_address
        self.write_char_uuid = write_char_uuid
        self.notify_char_uuid = notify_char_uuid

        self.client = None
        self.device = None
        self.is_connected = False

        # 接收数据缓冲区（用于分包重组）
        self._rx_buffer = bytearray()
        self._expected_length = 0
        self._packet_callback = None

        # 异步事件：用于等待连接建立
        self._connected_event = asyncio.Event()

    # ==================== 初始化/扫描 ====================

    async def scan(self, timeout=5.0):
        """
        扫描周围的 BLE 设备

        :param timeout: 扫描超时时间（秒）
        :return: 设备列表 [(name, address), ...]
        """
        print(f"正在扫描 BLE 设备（{timeout}秒）...")
        devices = await BleakScanner.discover(timeout=timeout)
        result = []
        for d in devices:
            result.append((d.name, d.address))
            print(f"  发现: {d.name} [{d.address}]")
        return result

    async def find_device(self, timeout=5.0):
        """
        根据名称或地址查找目标设备

        :param timeout: 扫描超时时间
        :return: BLEDevice 对象，找不到返回 None
        """
        print(f"正在查找设备: {self.device_name or self.device_address} ...")
        devices = await BleakScanner.discover(timeout=timeout)

        for d in devices:
            if self.device_address and d.address.upper() == self.device_address.upper():
                print(f"  已找到（按地址）: {d.name} [{d.address}]")
                return d
            if self.device_name and d.name and self.device_name in d.name:
                print(f"  已找到（按名称）: {d.name} [{d.address}]")
                return d

        print("  未找到目标设备")
        return None

    # ==================== 连接/断开 ====================

    async def connect(self, scan_timeout=5.0):
        """
        连接 STM32 设备

        :param scan_timeout: 扫描超时时间
        :return: True 表示连接成功
        """
        # 如果只给了地址，可以直接连接；否则先扫描
        if self.device_address and not self.device_name:
            target = self.device_address
        else:
            self.device = await self.find_device(timeout=scan_timeout)
            if self.device is None:
                print("连接失败：未找到设备")
                return False
            target = self.device

        try:
            self.client = BleakClient(target)
            await self.client.connect()
            self.is_connected = self.client.is_connected

            if self.is_connected:
                print(f"已连接: {self.client.address}")
                self._connected_event.set()
            else:
                print("连接失败")
            return self.is_connected
        except Exception as e:
            print(f"连接异常: {e}")
            self.is_connected = False
            return False

    async def disconnect(self):
        """断开连接"""
        if self.client and self.client.is_connected:
            # 先停止通知
            if self.notify_char_uuid:
                try:
                    await self.client.stop_notify(self.notify_char_uuid)
                except Exception:
                    pass
            await self.client.disconnect()
            print("已断开连接")
        self.is_connected = False
        self._connected_event.clear()

    # ==================== 发送数据 ====================

    async def send(self, data, response=False):
        """
        发送数据到 STM32

        :param data: bytes 或 bytearray，要发送的数据
        :param response: True=带响应写（Write），False=无响应写（Write Without Response）
        :return: True 表示发送成功
        """
        if not self.is_connected or not self.client:
            print("发送失败：未连接")
            return False

        if self.write_char_uuid is None:
            print("发送失败：未指定写入特征值 UUID")
            return False

        try:
            if isinstance(data, (bytes, bytearray)):
                payload = data
            else:
                payload = bytes(data)

            await self.client.write_gatt_char(
                self.write_char_uuid, payload, response=response
            )
            print(f"已发送 ({len(payload)}字节): {payload.hex()}")
            return True
        except Exception as e:
            print(f"发送异常: {e}")
            return False

    async def send_packet(self, packet: bytes, chunk_size: int = 2, delay: float = 0.02):
        """
        分包发送数据包（适用于特征值长度受限的情况）

        :param packet: 完整的 bytes 数据包
        :param chunk_size: 每包字节数（默认 2）
        :param delay: 包间延时（秒），避免协议栈缓冲区溢出
        """
        total = len(packet)
        for i in range(0, total, chunk_size):
            chunk = packet[i:i + chunk_size]
            ok = await self.send(chunk)
            if not ok:
                print(f"  第 {i // chunk_size + 1} 包发送失败，中止")
                return False
            await asyncio.sleep(delay)
        print(f"分包发送完成，共 {total} 字节")
        return True

    # ==================== 接收数据 ====================

    def _notification_handler(self, sender, data: bytearray):
        """
        内部通知回调：接收 STM32 发来的数据

        :param sender: 特征值 UUID
        :param data: 收到的数据（bytearray）
        """
        print(f"收到通知 [{sender}]: {data.hex()}")

        # 分包重组逻辑
        self._rx_buffer.extend(data)
        if self._packet_callback:
            if len(self._rx_buffer) >= self._expected_length > 0:
                packet = bytes(self._rx_buffer[:self._expected_length])
                self._rx_buffer = self._rx_buffer[self._expected_length:]
                self._packet_callback(packet)
            else:
                # 没设定长度 → 直接回调
                self._packet_callback(bytes(data))

    async def start_receive(self, callback=None, expected_length=0):
        """
        开启接收通知

        :param callback: 收到完整数据包后的回调函数 callback(packet: bytes)
        :param expected_length: 期望的完整数据包长度（用于分包重组，0 表示不重组）
        """
        if not self.is_connected or not self.client:
            print("接收失败：未连接")
            return False

        if self.notify_char_uuid is None:
            print("接收失败：未指定通知特征值 UUID")
            return False

        self._packet_callback = callback
        self._expected_length = expected_length
        self._rx_buffer = bytearray()

        try:
            await self.client.start_notify(
                self.notify_char_uuid, self._notification_handler
            )
            print("已开启通知，等待数据...")
            return True
        except Exception as e:
            print(f"开启通知异常: {e}")
            return False

    async def stop_receive(self):
        """停止接收通知"""
        if self.client and self.is_connected and self.notify_char_uuid:
            try:
                await self.client.stop_notify(self.notify_char_uuid)
                print("已停止通知")
            except Exception as e:
                print(f"停止通知异常: {e}")

    async def read(self):
        """
        主动读取一次特征值数据

        :return: bytes 数据，失败返回 None
        """
        if not self.is_connected or not self.client:
            print("读取失败：未连接")
            return None

        if self.notify_char_uuid is None:
            print("读取失败：未指定特征值 UUID")
            return None

        try:
            data = await self.client.read_gatt_char(self.notify_char_uuid)
            print(f"读取数据: {data.hex()}")
            return bytes(data)
        except Exception as e:
            print(f"读取异常: {e}")
            return None

    # ==================== 工具方法 ====================

    async def list_services(self):
        """打印设备的所有服务和特征值（调试用）"""
        if not self.is_connected or not self.client:
            print("未连接")
            return

        print("=== 服务与特征值列表 ===")
        for service in self.client.services:
            print(f"[Service] {service.uuid}")
            for char in service.characteristics:
                props = ",".join(char.properties)
                print(f"  [Char] {char.uuid}  ({props})")

    async def wait_connected(self, timeout=10.0):
        """等待连接建立（阻塞直到连接成功或超时）"""
        try:
            await asyncio.wait_for(self._connected_event.wait(), timeout=timeout)
            return True
        except asyncio.TimeoutError:
            return False