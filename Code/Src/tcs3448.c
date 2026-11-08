#include "tcs3448.h"
#include <string.h>

/*==============================================================================
 *  内部变量
 *============================================================================*/

/* 最近一次的配置，用来算"等测量完成"的超时时间。
 * 4 颗传感器挂在同一路 I²C 上、配置完全一样，所以共用一份缓存就够了。 */
static uint8_t  s_atime = TCS3448_DEFAULT_ATIME;
static uint16_t s_astep = TCS3448_DEFAULT_ASTEP;
static uint8_t  s_gain  = TCS3448_DEFAULT_AGAIN;
static uint8_t  s_smux  = TCS3448_SMUX_18CH;

/* 4 颗传感器的数据缓存（与原来 tcs34725.c 里的 rgbdata[4] 对应） */
tcs3448_data tcs_data[4];

/* 最近一次访问的传感器寄存器快照（调试用，见头文件说明） */
tcs3448_dbg_t tcs3448_dbg;

/*==============================================================================
 *  底层寄存器读写
 *  注意：TCS3448 不需要 TCS34725 那样的 0x80 命令位，寄存器地址直接就是 1 字节；
 *        器件内部的地址指针每读写 1 字节自动 +1，所以支持连续突发读写。
 *============================================================================*/

/* I2C 传输失败后重新初始化外设：DeInit/Init 会重新配置 GPIO 和时钟，
 * 用来从"某个从机把 SDA 拉死"之类的总线故障里恢复 */
static void TCS3448_I2C_Recover(I2C_HandleTypeDef *hi2c)
{
    HAL_I2C_DeInit(hi2c);
    HAL_I2C_Init(hi2c);
    HAL_I2CEx_ConfigAnalogFilter(hi2c, I2C_ANALOGFILTER_ENABLE);
    HAL_I2CEx_ConfigDigitalFilter(hi2c, 0);
}

/* 写单个寄存器 */
static HAL_StatusTypeDef TCS3448_WriteReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t val)
{
    uint8_t buf[2];
    HAL_StatusTypeDef st;

    buf[0] = reg;   /* 寄存器地址 */
    buf[1] = val;   /* 要写入的值 */

    st = HAL_I2C_Master_Transmit(hi2c, TCS3448_W_ADDR, buf, 2, TCS3448_I2C_TIMEOUT);
    if (st != HAL_OK)
    {
        /* 失败一次先恢复总线再重试一次，避免一次干扰之后整条总线都读不动 */
        TCS3448_I2C_Recover(hi2c);
        st = HAL_I2C_Master_Transmit(hi2c, TCS3448_W_ADDR, buf, 2, TCS3448_I2C_TIMEOUT);
    }
    return st;
}

/* 读单个寄存器 */
static HAL_StatusTypeDef TCS3448_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t *val)
{
    HAL_StatusTypeDef st;

    /* 先发地址，再读数据。两次传输中间虽然有 STOP，但器件内部的地址指针是保留的，
     * 所以这种写法（与原来 tcs34725.c 一致）完全可行。 */
    st = HAL_I2C_Master_Transmit(hi2c, TCS3448_W_ADDR, &reg, 1, TCS3448_I2C_TIMEOUT);
    if (st != HAL_OK)
    {
        TCS3448_I2C_Recover(hi2c);
        st = HAL_I2C_Master_Transmit(hi2c, TCS3448_W_ADDR, &reg, 1, TCS3448_I2C_TIMEOUT);
        if (st != HAL_OK)
            return st;
    }

    st = HAL_I2C_Master_Receive(hi2c, TCS3448_R_ADDR, val, 1, TCS3448_I2C_TIMEOUT);
    if (st != HAL_OK)
    {
        TCS3448_I2C_Recover(hi2c);
        st = HAL_I2C_Master_Receive(hi2c, TCS3448_R_ADDR, val, 1, TCS3448_I2C_TIMEOUT);
    }
    return st;
}

/* 从 reg 开始连续读 len 个字节（突发读，地址指针自动递增） */
static HAL_StatusTypeDef TCS3448_ReadRegs(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef st;

    st = HAL_I2C_Master_Transmit(hi2c, TCS3448_W_ADDR, &reg, 1, TCS3448_I2C_TIMEOUT);
    if (st != HAL_OK)
    {
        TCS3448_I2C_Recover(hi2c);
        st = HAL_I2C_Master_Transmit(hi2c, TCS3448_W_ADDR, &reg, 1, TCS3448_I2C_TIMEOUT);
        if (st != HAL_OK)
            return st;
    }

    st = HAL_I2C_Master_Receive(hi2c, TCS3448_R_ADDR, buf, len, TCS3448_I2C_TIMEOUT);
    if (st != HAL_OK)
    {
        TCS3448_I2C_Recover(hi2c);
        st = HAL_I2C_Master_Receive(hi2c, TCS3448_R_ADDR, buf, len, TCS3448_I2C_TIMEOUT);
    }
    return st;
}

/* 切换寄存器页码：
 *   0 -> 访问 0x80 及以上（ENABLE/STATUS/ASTATUS/DATA/CFG1/CFG20 等绝大多数寄存器）
 *   1 -> 访问 0x20~0x7F（ID/AUXID/REVID/GPIO/CFG10/CFG12）
 * 写完必须切回 0，否则后面读数据寄存器会失败。 */
static HAL_StatusTypeDef TCS3448_SetBank(I2C_HandleTypeDef *hi2c, uint8_t bank)
{
    return TCS3448_WriteReg(hi2c, TCS3448_REG_CFG0,
                            (bank != 0) ? TCS3448_CFG0_REG_BANK1 : 0x00);
}

/* 使能/停止光谱测量（只改 ENABLE 的 ALS_EN 位，保留 PON 等其它位） */
static HAL_StatusTypeDef TCS3448_SetALSEnable(I2C_HandleTypeDef *hi2c, uint8_t enable)
{
    uint8_t val;

    if (TCS3448_ReadReg(hi2c, TCS3448_REG_ENABLE, &val) != HAL_OK)
        return HAL_ERROR;

    if (enable)
        val |= TCS3448_ENABLE_ALS_EN;
    else
        val &= (uint8_t)(~TCS3448_ENABLE_ALS_EN);

    return TCS3448_WriteReg(hi2c, TCS3448_REG_ENABLE, val);
}

/* 由 ATIME/ASTEP 算出积分时间(ms)：t = (ATIME+1) × (ASTEP+1) × 0.00278ms */
static float TCS3448_CalcIntegrationMs(uint8_t atime, uint16_t astep)
{
    return ((float)atime + 1.0f) * ((float)astep + 1.0f) * 0.00278f;
}

/* 把一次突发读到的 37 字节（ASTATUS + 18 个通道）解析到结构体里 */
static void TCS3448_ParseFrame(const uint8_t *frame, tcs3448_data *data)
{
    uint8_t i;

    /* frame[0] = ASTATUS：
     *   bit7    ：本次锁存的数据是否饱和
     *   bit3:0  ：本次实际生效的增益档 */
    data->astatus      = frame[0];
    data->again_status = (uint8_t)(frame[0] & 0x0F);
    data->sat_astatus  = (uint8_t)((frame[0] >> 7) & 0x01);

    /* frame[1] 开始是 ADATA0~ADATA17，每个通道 2 字节，先低字节后高字节 */
    for (i = 0; i < TCS3448_CH_COUNT; i++)
    {
        data->adata[i] = (uint16_t)(frame[1 + i * 2] | ((uint16_t)frame[2 + i * 2] << 8));
    }

    /* 按物理含义把 18 个原始结果拆成 14 个光谱通道（对应关系见头文件表格） */
    data->fz      = data->adata[TCS3448_CH_FZ];
    data->fy      = data->adata[TCS3448_CH_FY];
    data->fxl     = data->adata[TCS3448_CH_FXL];
    data->nir     = data->adata[TCS3448_CH_NIR];
    data->f1      = data->adata[TCS3448_CH_F1];
    data->f2      = data->adata[TCS3448_CH_F2];
    data->f3      = data->adata[TCS3448_CH_F3];
    data->f4      = data->adata[TCS3448_CH_F4];
    data->f5      = data->adata[TCS3448_CH_F5];
    data->f6      = data->adata[TCS3448_CH_F6];
    data->f7      = data->adata[TCS3448_CH_F7];
    data->f8      = data->adata[TCS3448_CH_F8];
    data->vis_tl  = data->adata[TCS3448_CH_VIS_TL_0];
    data->vis_br  = data->adata[TCS3448_CH_VIS_BR_0];
    data->clear   = (uint32_t)data->vis_tl + (uint32_t)data->vis_br;
    /* 三个周期的透明通道理论上应该差不多，差别大说明光源在变化(比如有闪烁)，
     * 需要原始值的话直接看 data->adata[4]/[5]/[10]/[11]/[16]/[17] */
}

/* 读全部寄存器并解析（不包括"启动测量"的动作） */
static HAL_StatusTypeDef TCS3448_ReadFrame(I2C_HandleTypeDef *hi2c, tcs3448_data *data)
{
    uint8_t frame[1 + TCS3448_CH_COUNT * 2];  /* ASTATUS + 18×16bit = 37 字节 */
    uint8_t tmp;
    HAL_StatusTypeDef st;

    /* ★一次突发读：从 ASTATUS(0x94) 连续读 37 字节
     *   读 ASTATUS 会自动锁存后面的 36 字节光谱数据，保证 18 个通道是同一时刻的；
     *   而且"先低字节后高字节"的要求由连续读自然满足。 */
    st = TCS3448_ReadRegs(hi2c, TCS3448_REG_ASTATUS, frame, sizeof(frame));
    if (st != HAL_OK)
        return st;

    /* 解析 18 通道 + ASTATUS */
    TCS3448_ParseFrame(frame, data);

    /* 其余状态寄存器（读失败不算致命错误，能读到几个是几个） */
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_STATUS2, &tmp) == HAL_OK)
    {
        data->status2        = tmp;
        data->als_valid      = (uint8_t)((tmp >> 6) & 0x01);
        data->sat_digital    = (uint8_t)((tmp >> 4) & 0x01);
        data->sat_analog     = (uint8_t)((tmp >> 3) & 0x01);
        data->fd_sat_analog  = (uint8_t)((tmp >> 1) & 0x01);
        data->fd_sat_digital = (uint8_t)(tmp & 0x01);
    }
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_STATUS,    &tmp) == HAL_OK) data->status     = tmp;
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_STATUS4,   &tmp) == HAL_OK) data->status4    = tmp;
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_STATUS5,   &tmp) == HAL_OK) data->status5    = tmp;
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_FD_STATUS, &tmp) == HAL_OK) data->fd_status  = tmp;
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_FIFO_LVL,  &tmp) == HAL_OK) data->fifo_level = tmp;

    /* 把"当前实际配置"也读回来存进结构体（以寄存器实际值为准，不是本地缓存） */
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_ATIME, &tmp) == HAL_OK)
    {
        data->atime = tmp;
        s_atime     = tmp;
    }
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_ASTEP_L, &tmp) == HAL_OK)
    {
        uint8_t astep_h = 0;

        data->astep = tmp;
        s_astep     = tmp;
        if (TCS3448_ReadReg(hi2c, TCS3448_REG_ASTEP_H, &astep_h) == HAL_OK)
        {
            data->astep = (uint16_t)(tmp | ((uint16_t)astep_h << 8));
            s_astep     = data->astep;
        }
    }
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_CFG1, &tmp) == HAL_OK)
    {
        data->again_cfg = (uint8_t)(tmp & 0x1F);
        s_gain          = data->again_cfg;
    }
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_CFG20, &tmp) == HAL_OK)
    {
        data->smux_mode = (uint8_t)((tmp >> 5) & 0x03);
        s_smux          = data->smux_mode;
    }
    data->integration_ms = TCS3448_CalcIntegrationMs(data->atime, data->astep);

    /* 顺手算一份 RGB888，方便沿用原来 TCS34725 的显示/上报逻辑 */
    TCS3448_ComputeRGB888(data);

    return HAL_OK;
}

/*==============================================================================
 *  对外函数
 *============================================================================*/

/* 读取器件 ID（ID 寄存器在 bank1，需要临时切换） */
uint8_t TCS3448_ReadID(I2C_HandleTypeDef *hi2c)
{
    uint8_t id = 0xFF;

    TCS3448_SetBank(hi2c, 1);
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_ID, &id) != HAL_OK)
        id = 0xFF;
    TCS3448_SetBank(hi2c, 0);   /* 必须切回 bank0，否则后面读不到数据寄存器 */

    return id;
}

/* 读取辅助 ID（bank1） */
uint8_t TCS3448_ReadAUXID(I2C_HandleTypeDef *hi2c)
{
    uint8_t id = 0xFF;

    TCS3448_SetBank(hi2c, 1);
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_AUXID, &id) != HAL_OK)
        id = 0xFF;
    TCS3448_SetBank(hi2c, 0);

    return (uint8_t)(id & 0x0F);
}

/* 读取版本号（bank1） */
uint8_t TCS3448_ReadREVID(I2C_HandleTypeDef *hi2c)
{
    uint8_t id = 0xFF;

    TCS3448_SetBank(hi2c, 1);
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_REVID, &id) != HAL_OK)
        id = 0xFF;
    TCS3448_SetBank(hi2c, 0);

    return (uint8_t)(id & 0x07);
}

/* 软件复位：写 CONTROL 的 SW_RESET 位，然后轮询直到器件能响应 I²C */
HAL_StatusTypeDef TCS3448_SoftwareReset(I2C_HandleTypeDef *hi2c)
{
    uint8_t tmp;
    uint8_t i;

    if (TCS3448_WriteReg(hi2c, TCS3448_REG_CONTROL, TCS3448_CONTROL_SW_RESET) != HAL_OK)
        return HAL_ERROR;

    /* 复位期间器件不响应，轮询 CFG0(0xBF) 是否读得到，读得到就说明复位完成 */
    for (i = 0; i < 50; i++)
    {
        HAL_Delay(1);
        if (TCS3448_ReadReg(hi2c, TCS3448_REG_CFG0, &tmp) == HAL_OK)
            return HAL_OK;
    }

    return HAL_ERROR;
}

/* 设置增益（CFG1 的 bit4:0 = AGAIN） */
HAL_StatusTypeDef TCS3448_SetGain(I2C_HandleTypeDef *hi2c, uint8_t again)
{
    uint8_t val;

    if (again > TCS3448_AGAIN_2048X)
        return HAL_ERROR;

    if (TCS3448_ReadReg(hi2c, TCS3448_REG_CFG1, &val) != HAL_OK)
        return HAL_ERROR;

    val = (uint8_t)((val & 0xE0) | (again & 0x1F));   /* 高位保留原值 */

    if (TCS3448_WriteReg(hi2c, TCS3448_REG_CFG1, val) != HAL_OK)
        return HAL_ERROR;

    s_gain = again;
    return HAL_OK;
}

/* 设置积分时间：t_int = (ATIME+1) × (ASTEP+1) × 2.78us */
HAL_StatusTypeDef TCS3448_SetIntegrationTime(I2C_HandleTypeDef *hi2c, uint8_t atime, uint16_t astep)
{
    /* 0xFFFF 是保留值不能用；ATIME 与 ASTEP 也不允许同时为 0 */
    if (astep > 65534)
        return HAL_ERROR;
    if ((atime == 0) && (astep == 0))
        return HAL_ERROR;

    if (TCS3448_WriteReg(hi2c, TCS3448_REG_ATIME, atime) != HAL_OK)
        return HAL_ERROR;

    /* ASTEP 是 16bit 寄存器，datasheet 要求"先写低字节，紧接着写高字节" */
    if (TCS3448_WriteReg(hi2c, TCS3448_REG_ASTEP_L, (uint8_t)(astep & 0x00FF)) != HAL_OK)
        return HAL_ERROR;
    if (TCS3448_WriteReg(hi2c, TCS3448_REG_ASTEP_H, (uint8_t)((astep >> 8) & 0xFF)) != HAL_OK)
        return HAL_ERROR;

    s_atime = atime;
    s_astep = astep;
    return HAL_OK;
}

/* 设置自动 SMUX 扫描模式（CFG20 的 bit6:5） */
HAL_StatusTypeDef TCS3448_SetSmuxMode(I2C_HandleTypeDef *hi2c, uint8_t mode)
{
    uint8_t val;

    if (mode > TCS3448_SMUX_18CH)
        return HAL_ERROR;

    if (TCS3448_ReadReg(hi2c, TCS3448_REG_CFG20, &val) != HAL_OK)
        return HAL_ERROR;

    val = (uint8_t)((val & (uint8_t)(~0x60)) | (uint8_t)((mode & 0x03) << 5));

    if (TCS3448_WriteReg(hi2c, TCS3448_REG_CFG20, val) != HAL_OK)
        return HAL_ERROR;

    s_smux = mode;
    return HAL_OK;
}

/* LED 驱动（LDR 脚外接补光 LED）：enable 1=开 0=关，电流 = 4mA + 2mA × current_code */
HAL_StatusTypeDef TCS3448_SetLED(I2C_HandleTypeDef *hi2c, uint8_t enable, uint8_t current_code)
{
    uint8_t val;

    if (current_code > 127)
        current_code = 127;

    val = (uint8_t)(current_code & 0x7F);
    if (enable)
        val |= 0x80;    /* bit7 = LED_ACT */

    return TCS3448_WriteReg(hi2c, TCS3448_REG_LED, val);
}

/* 清 STATUS 里的标志：STATUS 是"写 1 清零"，把刚读到的值原样写回即可，
 * 这样既清掉了已经处理过的事件，又不会误清这之后新产生的事件 */
HAL_StatusTypeDef TCS3448_ClearStatus(I2C_HandleTypeDef *hi2c)
{
    uint8_t st;

    if (TCS3448_ReadReg(hi2c, TCS3448_REG_STATUS, &st) != HAL_OK)
        return HAL_ERROR;

    return TCS3448_WriteReg(hi2c, TCS3448_REG_STATUS, st);
}

/* 读一遍关键寄存器，填进 tcs3448_dbg（初始化后自动调用，调试时也可以手动调） */
void TCS3448_DumpRegisters(I2C_HandleTypeDef *hi2c)
{
    uint8_t v;

    tcs3448_dbg.auxid = TCS3448_ReadAUXID(hi2c);
    tcs3448_dbg.revid = TCS3448_ReadREVID(hi2c);
    tcs3448_dbg.id    = TCS3448_ReadID(hi2c);

    if (TCS3448_ReadReg(hi2c, TCS3448_REG_CFG0,    &v) == HAL_OK) tcs3448_dbg.cfg0     = v;
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_ENABLE,  &v) == HAL_OK) tcs3448_dbg.enable   = v;
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_CFG1,    &v) == HAL_OK) tcs3448_dbg.cfg1     = v;
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_ATIME,   &v) == HAL_OK) tcs3448_dbg.atime    = v;
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_ASTEP_L, &v) == HAL_OK) tcs3448_dbg.astep_l = v;
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_ASTEP_H, &v) == HAL_OK) tcs3448_dbg.astep_h = v;
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_CFG20,   &v) == HAL_OK) tcs3448_dbg.cfg20    = v;
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_STATUS,  &v) == HAL_OK) tcs3448_dbg.status   = v;
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_STATUS2, &v) == HAL_OK) tcs3448_dbg.status2  = v;
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_STATUS4, &v) == HAL_OK) tcs3448_dbg.status4  = v;
}

/* 初始化 */
HAL_StatusTypeDef TCS3448_Init(I2C_HandleTypeDef *hi2c)
{
    uint8_t id;
    uint8_t tmp;
    uint8_t i;

    memset(&tcs3448_dbg, 0, sizeof(tcs3448_dbg));

    /* 0) 上电后器件约 200us 在做内部初始化，这段时间会 NAK；
     *    再加上 TCA9548A 通道切换和电源稳定的时间，这里统一先等 2ms */
    HAL_Delay(2);

    /* 1) 软复位，让所有寄存器回到默认值，避免上一轮运行留下奇怪的配置 */
    if (TCS3448_SoftwareReset(hi2c) != HAL_OK)
        return HAL_ERROR;

    /* 2) 校验器件 ID（0x5A 应该等于 0x81），顺便确认 I²C 通信正常 */
    id = TCS3448_ReadID(hi2c);
    tcs3448_dbg.id = id;
    if (id != TCS3448_CHIP_ID)
        return HAL_ERROR;

    /* 2.5) 等内部初始化结束：STATUS4(0xBC) 的 bit0 = INT_BUSY，
     *      刚复位后约 300us 内它是 1，这期间写寄存器不会生效。
     *      如果它一直是 1，说明器件根本没正常起来（供电/电平问题）。*/
    for (i = 0; i < 50; i++)
    {
        if (TCS3448_ReadReg(hi2c, TCS3448_REG_STATUS4, &tmp) == HAL_OK)
        {
            tcs3448_dbg.status4 = tmp;
            if ((tmp & TCS3448_ST4_INT_BUSY) == 0)
                break;
        }
        HAL_Delay(1);
    }
    if (i >= 50)
        return HAL_ERROR;

    /* 3) 上电 PON=1：内部振荡器启动，所有定时器和 ADC 才工作 */
    if (TCS3448_WriteReg(hi2c, TCS3448_REG_ENABLE, TCS3448_ENABLE_PON) != HAL_OK)
        return HAL_ERROR;

    /* 4) 关闭全部中断：本驱动用轮询(STATUS2 的 AVALID)方式取数据，不用 INT 脚 */
    if (TCS3448_WriteReg(hi2c, TCS3448_REG_INTENAB, 0x00) != HAL_OK)
        return HAL_ERROR;

    /* 5) 配置增益、积分时间、自动 SMUX 扫描模式 */
    if (TCS3448_SetGain(hi2c, TCS3448_DEFAULT_AGAIN) != HAL_OK)
        return HAL_ERROR;
    if (TCS3448_SetIntegrationTime(hi2c, TCS3448_DEFAULT_ATIME, TCS3448_DEFAULT_ASTEP) != HAL_OK)
        return HAL_ERROR;
    if (TCS3448_SetSmuxMode(hi2c, TCS3448_SMUX_18CH) != HAL_OK)
        return HAL_ERROR;

    /* 6) 清掉上电过程中产生的状态标志 */
    TCS3448_ClearStatus(hi2c);

    /* 7) LED 驱动默认关闭（没接 LED 就保持关闭，不用管这一句） */
    TCS3448_SetLED(hi2c, 0, 0);

    /* 8) 把配置读回来逐项核对。
     *    如果这里不通过，说明"写进去了但器件没真的生效"——
     *    最常见的原因是供电/电平不对（器件要求 VDD 1.8V、SCL/SDA ≤1.98V）。*/
    TCS3448_DumpRegisters(hi2c);

    if ((tcs3448_dbg.enable & TCS3448_ENABLE_PON) == 0)                    /* PON 没生效 */
        return HAL_ERROR;
    if ((tcs3448_dbg.cfg1 & 0x1F) != TCS3448_DEFAULT_AGAIN)                /* 增益没写进去 */
        return HAL_ERROR;
    if (tcs3448_dbg.atime != TCS3448_DEFAULT_ATIME)                        /* ATIME 没写进去 */
        return HAL_ERROR;
    if (((uint16_t)tcs3448_dbg.astep_l | ((uint16_t)tcs3448_dbg.astep_h << 8))
        != TCS3448_DEFAULT_ASTEP)                                          /* ASTEP 没写进去 */
        return HAL_ERROR;
    if (((tcs3448_dbg.cfg20 >> 5) & 0x03) != TCS3448_SMUX_18CH)            /* 扫描模式没写进去 */
        return HAL_ERROR;

    tcs3448_dbg.init_ok = 1;
    return HAL_OK;
}

/* 停止测量（ALS_EN = 0） */
HAL_StatusTypeDef TCS3448_StopMeasurement(I2C_HandleTypeDef *hi2c)
{
    return TCS3448_SetALSEnable(hi2c, 0);
}

/* 非阻塞第 1 步：启动一次新测量（选好 TCA9548A 通道后调用）
 * 只做几次很短的 I2C 写，不会等测量结果 */
HAL_StatusTypeDef TCS3448_StartMeasurement(I2C_HandleTypeDef *hi2c, tcs3448_data *data)
{
    uint8_t tmp;

    if (data == NULL)
        return HAL_ERROR;

    /* 注意：这里只清错误码，不要把整个结构体清零！
     * BLE 发送可能和本轮测量交错进行，清零会把还没发出去的那颗变成全 0。
     * 数据只在测量成功后才被覆盖，失败时保留上一次的值（由 err 标志区分）。*/
    data->err = TCS3448_ERR_NONE;

    /* --- 1. 先停掉上一轮测量，清状态，并读一次 ASTATUS 把旧数据作废 --- */
    TCS3448_SetALSEnable(hi2c, 0);
    TCS3448_ClearStatus(hi2c);
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_ASTATUS, &tmp) != HAL_OK)
    {
        data->err = TCS3448_ERR_I2C;
        return HAL_ERROR;
    }

    /* --- 2. 启动新测量（ALS_EN=1） --- */
    if (TCS3448_SetALSEnable(hi2c, 1) != HAL_OK)
    {
        data->err = TCS3448_ERR_I2C;
        return HAL_ERROR;
    }

    /* --- 3. 读回 ENABLE 只作调试记录 ---
     *     刚写完马上读，器件可能还慢一拍，所以这里不作为失败条件；
     *     如果写入真没生效，后面等 AVALID 会超时，err 会变成 NOT_READY。 */
    if (TCS3448_ReadReg(hi2c, TCS3448_REG_ENABLE, &tmp) == HAL_OK)
    {
        data->reg_enable = tmp;
    }

    return HAL_OK;
}

/* 非阻塞第 2 步：查询测量是否完成（只读一次 STATUS2，约 50us）
 * 返回 1 = 数据就绪(AVALID)，0 = 还没好 / 出错（出错时 data->err 会置位） */
uint8_t TCS3448_MeasurementDone(I2C_HandleTypeDef *hi2c, tcs3448_data *data)
{
    static uint8_t fail_cnt = 0;   /* 连续读失败次数：偶发一次 NAK 不算错误 */
    uint8_t st2;

    if (data == NULL)
        return 0;

    if (TCS3448_ReadReg(hi2c, TCS3448_REG_STATUS2, &st2) != HAL_OK)
    {
        if (fail_cnt < 255)
            fail_cnt++;
        if (fail_cnt >= 20)                 /* 连续 20 次(约 20ms)读不通才算真错误 */
            data->err = TCS3448_ERR_I2C;
        return 0;
    }
    fail_cnt = 0;

    data->status2   = st2;
    data->als_valid = (uint8_t)((st2 & TCS3448_ST2_AVALID) ? 1 : 0);
    tcs3448_dbg.status2 = st2;          /* 调试时能实时看到 AVALID 位 */

    return data->als_valid;
}

/* 非阻塞第 3 步：读回一次完整测量结果，并停止测量 */
HAL_StatusTypeDef TCS3448_ReadResult(I2C_HandleTypeDef *hi2c, tcs3448_data *data)
{
    HAL_StatusTypeDef st;

    if (data == NULL)
        return HAL_ERROR;

    st = TCS3448_ReadFrame(hi2c, data);

    /* 读完立刻停测量：省电，同时防止下一轮测量把寄存器内容改掉 */
    TCS3448_SetALSEnable(hi2c, 0);

    if (st != HAL_OK)
        data->err = TCS3448_ERR_I2C;

    return st;
}

/* 阻塞版（内部就是上面三步），保留给不方便做状态机的地方用
 * 注意：单颗要等一个积分时间（默认约 17ms），不要放在 BLE 高频回调里 */
HAL_StatusTypeDef TCS3448_GetAllData(I2C_HandleTypeDef *hi2c, tcs3448_data *data)
{
    uint32_t t_start;
    uint32_t timeout_ms;
    HAL_StatusTypeDef st;

    if (data == NULL)
        return HAL_ERROR;

    st = TCS3448_StartMeasurement(hi2c, data);
    if (st != HAL_OK)
        return st;

    /* 根据当前积分时间算超时：积分时间 × 2 + 200ms 余量 */
    timeout_ms = (uint32_t)(TCS3448_CalcIntegrationMs(s_atime, s_astep) * 2.0f) + 200;

    t_start = HAL_GetTick();
    while (TCS3448_MeasurementDone(hi2c, data) == 0)
    {
        if ((HAL_GetTick() - t_start) > timeout_ms)
        {
            TCS3448_SetALSEnable(hi2c, 0);   /* 超时就停止测量，避免一直空转 */
            data->err = TCS3448_ERR_NOT_READY;
            return HAL_TIMEOUT;
        }
    }

    return TCS3448_ReadResult(hi2c, data);
}

/* 不启动测量，直接读当前寄存器里的数据（用于需要高频取数的场合） */
HAL_StatusTypeDef TCS3448_ReadData(I2C_HandleTypeDef *hi2c, tcs3448_data *data)
{
    if (data == NULL)
        return HAL_ERROR;

    memset(data, 0, sizeof(tcs3448_data));

    return TCS3448_ReadFrame(hi2c, data);
}

/* 用 3 个宽频可见通道换算 RGB888：
 *   FXL ≈ 596nm 当作 R，FY ≈ 560nm 当作 G，FZ ≈ 450nm 当作 B
 * 先除以透明通道(clear)得到与绝对亮度无关的色比，再按最大值归一化到 0~255，
 * 算法与原来的 TCS34725_GetRGB888 完全一致，方便直接替换 */
void TCS3448_ComputeRGB888(tcs3448_data *data)
{
    float rf, gf, bf, max;

    if (data == NULL)
        return;

    /* 全黑（透明通道没有计数）时直接返回 0 */
    if (data->clear == 0)
    {
        data->r = 0;
        data->g = 0;
        data->b = 0;
        return;
    }

    rf = (float)data->fxl / (float)data->clear;
    gf = (float)data->fy  / (float)data->clear;
    bf = (float)data->fz  / (float)data->clear;

    max = rf;
    if (gf > max) max = gf;
    if (bf > max) max = bf;
    if (max <= 0.0f)
    {
        data->r = 0;
        data->g = 0;
        data->b = 0;
        return;
    }

    data->r = (uint8_t)(rf / max * 255.0f);
    data->g = (uint8_t)(gf / max * 255.0f);
    data->b = (uint8_t)(bf / max * 255.0f);
}
