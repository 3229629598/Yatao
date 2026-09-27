# STM32WB55CGU6

# 烧录FUS和协议栈：
https://blog.csdn.net/data_i/article/details/148493746?fromshare=blogdetail&sharetype=blogdetail&sharerId=148493746&sharerefer=PC&sharesource=lhy3229629598&sharefrom=from_link

烧录完成后要点击 Start Wireless Stack

# 蓝牙设置：
在 p2p_server_app.c 的 P2PS_STM_App_Notification 修改蓝牙行为，在 p2p_stm.c 的 P2PS_STM_Init 的 aci_gatt_add_char 修改蓝牙协议

# 蓝牙客户端：
安装依赖
```
pip3 install -r requirements.txt
```
运行 ble_app.py 启动蓝牙客户端
