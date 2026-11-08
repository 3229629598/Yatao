#include "data_process.h"

usb_tx tx_data;
uint8_t tx_len;


void data_process_init()
{
    tx_len = sizeof(usb_tx);
    tx_data.header = usb_tx_header;
}

/*==============================================================================
 *  BLE 光谱数据上报
 * ----------------------------------------------------------------------------
 *  一条通知只发一颗传感器，4 颗分 4 条发（每条 P2P_NOTIFY_SIZE = 33 字节）：
 *      [0]      传感器序号(0~3)，bit7=1 表示这颗本轮测量失败、数据无效
 *      [1..28]  14 个光谱通道，每个 2 字节小端
 *      [29..32] clear（32 位小端）
 *
 *  PC 端收到 4 条后按序号拼起来，一次性打印。
 *  拆成 4 条的好处：单条只有 33 字节，对 MTU 要求低，协议栈也不会因为一次塞
 *  128 字节而丢包。
 *============================================================================*/

/* 编译期检查：整包不能超过单条通知的上限（ATT_MTU - 3 = 156 - 3 = 153 字节）
 * 如果哪天把通道数/传感器数改大了，这里会直接编译不过，避免发出去被协议栈拒掉 */
typedef char p2p_notify_size_check[(P2P_NOTIFY_SIZE <= 153) ? 1 : -1];

/* 每颗传感器上报哪些通道（下标对应 tcs3448_data.adata[]，见 tcs3448.h 的通道表）
 * 表里的顺序就是报文里的顺序，想调整顺序或增删通道改这里即可 */
static const uint8_t p2p_channel_map[P2P_CHANNEL_NUM] =
{
    TCS3448_CH_F1, TCS3448_CH_F2, TCS3448_CH_F3, TCS3448_CH_F4,
    TCS3448_CH_F5, TCS3448_CH_F6, TCS3448_CH_F7, TCS3448_CH_F8,
    TCS3448_CH_FZ, TCS3448_CH_FY, TCS3448_CH_FXL, TCS3448_CH_NIR,
    TCS3448_CH_VIS_TL_0, TCS3448_CH_VIS_BR_0,
};

/* 一条通知的发送缓冲（每颗用完就重填） */
static uint8_t p2p_tx_buf[P2P_NOTIFY_SIZE];

/* 把第 s 颗传感器的数据填进 dst：
 *   buf[0]      = 序号(0~3)，bit7 = 1 表示这颗本轮测量失败、数据无效
 *   buf[1..28]  = 14 个光谱通道，每个 2 字节小端
 *   buf[29..32] = clear（32 位小端）
 */
static void p2p_build_one(uint8_t s, uint8_t *dst)
{
    uint8_t k;

    for(k = 0; k < P2P_NOTIFY_SIZE; k++)
    {
        dst[k] = 0;
    }

    dst[0] = (uint8_t)(s & 0x03);
    if(tcs_data[s].err != TCS3448_ERR_NONE)
    {
        dst[0] |= 0x80;             /* 告诉 PC：这颗本轮没测到有效数据 */
    }

    for(k = 0; k < P2P_CHANNEL_NUM; k++)
    {
        uint16_t v = tcs_data[s].adata[p2p_channel_map[k]];   /* 该通道原始计数值 */

        dst[1 + k * 2]     = (uint8_t)(v & 0xFF);   /* 低字节在前 */
        dst[1 + k * 2 + 1] = (uint8_t)(v >> 8);
    }

    /* clear（可见光总量 = vis_tl + vis_br），32 位小端 */
    dst[1 + P2P_CHANNEL_NUM * 2]     = (uint8_t)(tcs_data[s].clear & 0xFF);
    dst[1 + P2P_CHANNEL_NUM * 2 + 1] = (uint8_t)((tcs_data[s].clear >> 8) & 0xFF);
    dst[1 + P2P_CHANNEL_NUM * 2 + 2] = (uint8_t)((tcs_data[s].clear >> 16) & 0xFF);
    dst[1 + P2P_CHANNEL_NUM * 2 + 3] = (uint8_t)((tcs_data[s].clear >> 24) & 0xFF);
}

/* 对外接口：每 100ms 由 main_loop 调用一次。
 * 一个循环把 4 条通知（一条一颗）发出去。
 * 协议栈忙（返回非 SUCCESS）时，本轮剩下的直接跳过，等下一个 100ms 再发。*/
void data_process_notify_spectral(void)
{
    uint8_t s;

    for(s = 0; s < P2P_SENSOR_COUNT; s++)
    {
        p2p_build_one(s, p2p_tx_buf);

        if(P2PS_STM_App_Update_Char(P2P_NOTIFY_CHAR_UUID,
                                    p2p_tx_buf) != BLE_STATUS_SUCCESS)
        {
            return;           /* 协议栈忙：这一轮剩下的不发，直接下一轮 */
        }
    }
}
