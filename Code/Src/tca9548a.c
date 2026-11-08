#include "tca9548a.h"

HAL_StatusTypeDef TCA9548A_SelectChannel(uint8_t channel, I2C_HandleTypeDef *hi2c)
{
    if(channel > 7)
        return HAL_ERROR;
		
    uint8_t data = 1 << channel;
    return HAL_I2C_Master_Transmit(hi2c, TCA9548A_ADDR << 1, &data, 1, 10);
}

void TCA9548A_DisableAll(void)
{
    uint8_t data = 0x00;

    HAL_I2C_Master_Transmit(&hi2c1, TCA9548A_ADDR << 1, &data, 1, 10);
}

/*==============================================================================
 *  初始化：逐颗选通并初始化
 *  注意：某颗初始化失败不再调用 Error_Handler 把整机卡死，而是记录下来
 *        （调试时看 i2c_sensor_ok[] / tcs_dbg[] / tcs_data[i].err 即可）
 *============================================================================*/
uint8_t       i2c_sensor_ok[4];   /* 1 = 该通道传感器初始化成功 */
tcs3448_dbg_t tcs_dbg[4];         /* 4 颗各自的寄存器快照（调试用） */

void i2c_my_init()
{
    for(int i = 0; i < 4; i++)
    {
        i2c_sensor_ok[i] = 0;

        if(TCA9548A_SelectChannel(i, &hi2c1) != HAL_OK)
        {
            continue;             /* 多路开关都没写成功，这颗跳过 */
        }

        /* 初始化：内部会校验 ID=0x81，并把配置读回核对 */
        if(TCS3448_Init(&hi2c1) == HAL_OK)
        {
            i2c_sensor_ok[i] = 1;
        }

        /* 不管成功失败都留一份寄存器快照，方便在 Keil 里看现场 */
        tcs_dbg[i] = tcs3448_dbg;
        HAL_Delay(5);
    }
}

/*==============================================================================
 *  非阻塞采集状态机
 * ----------------------------------------------------------------------------
 *  以前是"选通道 -> GetAllData(&hi2c1, ...) 一直等到 AVALID"，一颗 17ms、
 *  四颗就要 70ms 卡在 main_loop 里；器件不出数据时会一直空转到超时。
 *  现在拆成 启动 / 查询 / 读取 三步，每次调用只做一两笔很短的 I2C 传输，
 *  主循环（以及 BLE 协议栈）不会被卡住；某颗超时就记错误并跳到下一颗。
 *============================================================================*/
#define I2C_SCAN_PERIOD_MS      100   /* 每多少 ms 采一整轮 4 颗         */
#define I2C_AVALID_TIMEOUT_MS   500   /* 单颗等 AVALID 的超时            */
#define I2C_POLL_INTERVAL_MS    1     /* 最快每 1ms 查一次，别把 I²C 占满 */

static uint8_t  s_ch;             /* 当前正在测第几颗 */
static uint8_t  s_busy;           /* 1 = 正在等这一颗的测量完成 */
static uint32_t s_start_ms;       /* 这颗的启动时刻 */
static uint32_t s_cycle_start_ms; /* 本轮开始的时刻 */
static uint32_t s_next_cycle_ms;  /* 下一轮的开始时刻 */
static uint32_t s_last_poll_ms;   /* 上次查询的时刻（限速用） */
static uint8_t  s_retry;          /* 当前这颗已经重试过几次 */

static void sensor_advance(void);

/* 选通通道并启动一颗的测量 */
static void sensor_start(uint8_t ch)
{
    s_ch = ch;

    /* 选通道或启动测量失败：记错误，立刻跳到下一颗，不浪费 500ms 超时 */
    if((TCA9548A_SelectChannel(ch, &hi2c1) != HAL_OK) ||
       (TCS3448_StartMeasurement(&hi2c1, &tcs_data[ch]) != HAL_OK))
    {
        if(tcs_data[ch].err == TCS3448_ERR_NONE)
        {
            tcs_data[ch].err = TCS3448_ERR_I2C;
        }
        /* 偶发一次通信失败很常见，先重测一次；两次都不行才跳到下一颗 */
        if(s_retry < 1)
        {
            s_retry++;
            sensor_start(ch);
            return;
        }
        s_busy = 0;
        sensor_advance();
        return;
    }

    s_start_ms = HAL_GetTick();
    s_busy     = 1;
}

/* 这一颗结束，去下一颗；四颗都完了就等下一轮 */
static void sensor_advance(void)
{
    s_retry = 0;                      /* 换下一颗，重试次数清零 */

    if(s_ch + 1 >= 4)
    {
        s_busy = 0;
        /* 下一轮按"本轮开始时刻 + 周期"排，保证固定采样率；
         * 如果这一轮本身已经超过周期，就立刻开始下一轮 */
        s_next_cycle_ms = s_cycle_start_ms + I2C_SCAN_PERIOD_MS;
    }
    else
    {
        sensor_start((uint8_t)(s_ch + 1));
    }
}

/* 放在 main_loop 里每次调用（非阻塞，内部自己控制节奏） */
void i2c_rgb_update()
{
    uint32_t now = HAL_GetTick();

    /* 限速：最多每 1ms 查询一次状态 */
    if((now - s_last_poll_ms) < I2C_POLL_INTERVAL_MS)
    {
        return;
    }
    s_last_poll_ms = now;

    /* 空闲：到点了就从第 0 颗开始新一轮 */
    if(s_busy == 0)
    {
        if((int32_t)(now - s_next_cycle_ms) >= 0)
        {
            s_cycle_start_ms = now;
            s_retry = 0;
            sensor_start(0);
        }
        return;
    }

    /* 正在等当前这颗：AVALID 置位就读回来 */
    if(TCS3448_MeasurementDone(&hi2c1, &tcs_data[s_ch]))
    {
        TCS3448_ReadResult(&hi2c1, &tcs_data[s_ch]);
        sensor_advance();
    }
    /* 等太久了：记错误、停测量、跳到下一颗，绝不卡死 */
    else if((now - s_start_ms) > I2C_AVALID_TIMEOUT_MS)
    {
        /* 先重测一次，避免偶发的时序抖动让整颗变成 0 */
        if(s_retry < 1)
        {
            s_retry++;
            sensor_start((uint8_t)s_ch);
            return;
        }

        if(tcs_data[s_ch].err == TCS3448_ERR_NONE)
        {
            tcs_data[s_ch].err = TCS3448_ERR_NOT_READY;   /* 器件在应答但不出数据 */
        }
        TCS3448_StopMeasurement(&hi2c1);
        sensor_advance();
    }
}
