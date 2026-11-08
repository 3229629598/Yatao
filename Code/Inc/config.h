#ifndef config_h
#define config_h

#define tim1_f 10

/* ============================================================================
 * BLE 通知（光谱数据上报）配置
 * ----------------------------------------------------------------------------
 * 现在改成"一颗一条通知"，4 颗分 4 条发，PC 端凑齐 4 条再一起打印：
 *   一条 = 1 字节序号(bit7=错误标志) + 32 字节数据 = 33 字节
 *   每颗 32 字节 = 14 个光谱通道 × 2 字节 + clear(4 字节)
 *
 * 上限（由 BLE 协议栈决定，见 Middlewares 里的 shci.h / ble_gatt_aci.h 注释）：
 *   - 单条通知最大 = ATT_MTU - 3，本工程 CFG_BLE_MAX_ATT_MTU = 156 → 最长 153 字节
 *     （协议栈允许的 AttMtu 范围是 23 ~ 512，需要时可改 app_conf.h）
 *   - 单次 aci_gatt_update_char_value 最多传 249 字节
 *   33 字节对 MTU 的要求只有 36（Windows 会自动协商到 156），非常稳。
 * ==========================================================================*/
#define P2P_SENSOR_COUNT    4   /* 一共几颗传感器                                    */
#define P2P_CHANNEL_NUM     14  /* 每颗上报的光谱通道数（每个通道 2 字节）            */
#define P2P_CLEAR_BYTES     4   /* 末尾额外带 4 字节 clear(可见光总量，32 位)         */

/* 每颗传感器的字节数：14×2 + 4 = 32 */
#define P2P_SENSOR_BYTES    (P2P_CHANNEL_NUM * 2 + P2P_CLEAR_BYTES)
/* 一条通知的字节数：1 字节序号 + 32 字节数据 = 33（一条只发一颗传感器） */
#define P2P_NOTIFY_SIZE     (1 + P2P_SENSOR_BYTES)

#endif
