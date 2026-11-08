# STM32WB55CGU6

# 烧录FUS和协议栈：
https://blog.csdn.net/data_i/article/details/148493746?fromshare=blogdetail&sharetype=blogdetail&sharerId=148493746&sharerefer=PC&sharesource=lhy3229629598&sharefrom=from_link

烧录完成后要点击 Start Wireless Stack

# 蓝牙设置：
在 p2p_server_app.c 的 P2PS_STM_App_Notification 修改蓝牙行为

在 p2p_stm.c 的 P2PS_STM_Init 的 aci_gatt_add_char 修改蓝牙协议

# OTA:
1、有线烧录官方的 BLE_Ota 程序

2、在 .sct 文件将用户程序初始ROM地址设置为 0x08007000，初始RAM地址设置为 0x20000008

3、修改 ble_conf.h 的 BLE_CFG_OTA_REBOOT_CHAR 为 1

4、在 p2p_server_app.c 的 P2PS_STM_App_Notification 中添加 P2PS_STM_BOOT_REQUEST_EVT 分支

5、MagicKeywordAddress 地址设置为 0x08007140

6、生成 .bin 格式的可执行文件进行OTA

# 蓝牙客户端：
安装依赖
```
pip3 install -r requirements.txt
```
运行 ble_app.py 启动蓝牙客户端
