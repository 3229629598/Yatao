#ifndef tcs3448_h
#define tcs3448_h

#include "i2c.h"

/*==============================================================================
 *  TCS3448 —— 14 通道多光谱颜色传感器 驱动（STM32 HAL 版）
 * ----------------------------------------------------------------------------
 *  器件简介（详见 ams OSRAM 官方 datasheet，文档号 DS001121）：
 *    - 5x5 光电二极管阵列 + 片上干涉滤光片，内部有 6 个 16bit ADC
 *    - 14 个光谱通道（380~1000nm）：
 *        F1~F8：8 个窄带通道，中心波长 407/424/473/516/546/636/687/748nm
 *        FZ(450nm 宽带) / FY(560nm 宽带) / FXL(596nm 宽带) / NIR(855nm)
 *        VIS：透明(clear)通道，阵列左上 1 个 + 右侧 2 个并联
 *    - 额外还有一路闪烁检测(flicker detect)通道，本驱动只读取它的状态位，
 *      没有做闪烁频率的计算
 *    - 自动 SMUX 模式：一次测量内部自动跑 3 个周期、每个周期 6 个 ADC，
 *      把 18 个结果依次放进 ADATA0~ADATA17（正好覆盖上面 14 个通道）
 *
 *  【硬件接线注意事项】把 TCS34725 换成 TCS3448 后，必须确认这几点：
 *    1) I²C 从机地址变成 0x59（TCS34725 是 0x29）。
 *       4 颗传感器地址都一样，只能靠 TCA9548A 分时选通，流程与原来相同。
 *    2) VDD 只能 1.8V；SCL/SDA 的上拉电阻只能接到 1.2V 或 1.8V，
 *       **不能接 3.3V**（SDA/SCL 不是 3.6V 耐压引脚）。
 *       如果原来 TCS34725 用的是 3.3V 上拉，必须改成 1.8V 上拉或加电平转换。
 *    3) GPIO 引脚在上电 600us 时被采样，用来决定 I²C 电平：
 *         GPIO > 1.5V  ->  SCL/SDA 按 1.8V 电平工作
 *         GPIO < 1.5V  ->  SCL/SDA 按 1.2V 电平工作
 *       该引脚绝对不能悬空，必须接 VSS 或 VDD(3.3V / 1.8V 都可以)。
 *       另外上电后 600us 之内禁止任何 I²C 通信。
 *    4) 上电后器件要花约 200us 做内部初始化，这段时间它会 NAK，
 *       所以第一次访问前要先延时（本驱动统一延时 2ms）。
 *    5) 本驱动的 16bit 寄存器(ASTEP)严格按 datasheet 要求"先写低字节再写高字节"；
 *       读数据时用一次 37 字节突发读，天然满足"先低后高"的要求。
 *
 *  使用示例（配合 TCA9548A，4 颗传感器轮流读）：
 *      // 上电初始化一次
 *      for (int i = 0; i < 4; i++) {
 *          TCA9548A_SelectChannel(i, &hi2c1);   // 选通到第 i 颗
 *          TCS3448_Init(&hi2c1);
 *      }
 *      // 主循环里周期读取
 *      for (int i = 0; i < 4; i++) {
 *          TCA9548A_SelectChannel(i, &hi2c1);
 *          TCS3448_GetAllData(&hi2c1, &tcs_data[i]);   // 测量 + 读回全部数据
 *          // tcs_data[i].f1 ... .f8 / .fz / .fy / .fxl / .nir / .clear 都是原始计数值
 *      }
 *============================================================================*/

/*==============================================================================
 *  1. I²C 地址
 *  TCS3448 的 7bit 地址固定为 0x59，没有地址选择脚。
 *============================================================================*/
#define TCS3448_ADDR        0x59
#define TCS3448_W_ADDR      ((TCS3448_ADDR << 1) | 0x00)
#define TCS3448_R_ADDR      ((TCS3448_ADDR << 1) | 0x01)

/*==============================================================================
 *  2. 寄存器地址
 *  注意：TCS3448 的寄存器地址就是"1 字节地址 + 数据"，不需要像 TCS34725 那样
 *        在地址上加 0x80 命令位；寄存器地址指针读写后会自动 +1，支持连续读写。
 *  注意：带 "(bank1)" 的寄存器位于 0x20~0x7F 区间，必须先设置 CFG0(0xBF) 的
 *        REG_BANK 位为 1 才能访问，访问完要切回 bank0。
 *============================================================================*/
#define TCS3448_REG_AUXID       0x58    /* (bank1) 辅助 ID，只读                        */
#define TCS3448_REG_REVID       0x59    /* (bank1) 版本号，只读 [2:0]                   */
#define TCS3448_REG_ID          0x5A    /* (bank1) 器件 ID，只读，TCS3448 固定为 0x81    */
#define TCS3448_REG_CFG10       0x65    /* (bank1) 闪烁检测持续次数 FD_PERS[2:0]        */
#define TCS3448_REG_CFG12       0x66    /* (bank1) 中断/持续滤波使用的 ADC 通道选择     */
#define TCS3448_REG_GPIO        0x6B    /* (bank1) GPIO 方向/输出/输入等配置             */

#define TCS3448_REG_ENABLE      0x80    /* 使能寄存器：FDEN/SMUXEN/WEN/ALS_EN/PON       */
#define TCS3448_REG_ATIME       0x81    /* 积分时间步数 ATIME[7:0]                      */
#define TCS3448_REG_WTIME       0x83    /* 两次测量之间的等待时间 WTIME[7:0]            */
#define TCS3448_REG_ALS_TH_L    0x84    /* ALS 低阈值，16bit，先低字节后高字节(0x84/0x85)*/
#define TCS3448_REG_ALS_TH_H    0x86    /* ALS 高阈值，16bit，先低字节后高字节(0x86/0x87)*/
#define TCS3448_REG_STATUS2     0x90    /* 状态2：AVALID + 模拟/数字饱和标志            */
#define TCS3448_REG_STATUS3     0x91    /* 状态3：ALS 中断是高阈值还是低阈值触发        */
#define TCS3448_REG_STATUS      0x93    /* 主状态寄存器，"写 1 清零"                    */
#define TCS3448_REG_ASTATUS     0x94    /* 数据状态：读它会锁存全部 36 字节光谱数据      */
#define TCS3448_REG_ADATA0      0x95    /* 18 个通道数据起始地址(0x95~0xB8)，每通道 2 字节*/
#define TCS3448_REG_STATUS5     0xBB    /* 状态5：闪烁检测/SMUX 完成标志                */
#define TCS3448_REG_STATUS4     0xBC    /* 状态4：FIFO 溢出/过温/时序错误/休眠标志      */
#define TCS3448_REG_CFG0        0xBF    /* 配置0：LOW_POWER / REG_BANK(bit4) / WLONG    */
#define TCS3448_REG_CFG1        0xC6    /* 配置1：AGAIN[4:0] 模拟增益                    */
#define TCS3448_REG_CFG3        0xC7    /* 配置3：SAI 中断后进入休眠                    */
#define TCS3448_REG_CFG8        0xC9    /* 配置8：FIFO 阈值 FIFO_TH[7:6]                */
#define TCS3448_REG_CFG9        0xCA    /* 配置9：系统中断使能(SIEN_FD / SIEN_SMUX)     */
#define TCS3448_REG_LED         0xCD    /* LED 驱动：LED_ACT(bit7) + LED_DRIVE[6:0]     */
#define TCS3448_REG_PERS        0xCF    /* ALS 中断持续滤波 APERS[3:0]                  */
#define TCS3448_REG_ASTEP_L     0xD4    /* 积分步长 ASTEP 低字节                         */
#define TCS3448_REG_ASTEP_H     0xD5    /* 积分步长 ASTEP 高字节                         */
#define TCS3448_REG_CFG20       0xD6    /* 配置20：auto_smux[6:5] 自动通道扫描模式       */
#define TCS3448_REG_AGC_GAIN_MAX 0xD7   /* 闪烁检测自动增益上限 AGC_FD_GAIN_MAX[7:4]     */
#define TCS3448_REG_AZ_CONFIG   0xDE    /* 自动调零(auto zero)周期                       */
#define TCS3448_REG_FIFO_CFG0   0xDF    /* FIFO 配置0：FIFO_WRITE_FD(bit7)              */
#define TCS3448_REG_FD_TIME_1   0xE0    /* 闪烁检测积分时间(低 8 位)                     */
#define TCS3448_REG_FD_TIME_2   0xE2    /* 闪烁检测增益[7:3] + 积分时间高 3 位[2:0]      */
#define TCS3448_REG_FD_STATUS   0xE3    /* 闪烁检测结果状态                              */
#define TCS3448_REG_CFG6        0xF5    /* 配置6：SMUX_CMD[4:3] 手动 SMUX 命令           */
#define TCS3448_REG_INTENAB     0xF9    /* 中断使能：ASIEN/ALS_IEN/F_IEN/SIEN            */
#define TCS3448_REG_CONTROL     0xFA    /* 控制：SW_RESET(bit3)/ALS_MAN_AZ/FIFO_CLR     */
#define TCS3448_REG_FIFO_MAP    0xFC    /* FIFO 里写入哪些通道的数据                     */
#define TCS3448_REG_FIFO_LVL    0xFD    /* FIFO 中已存了多少条数据(每条 2 字节)          */
#define TCS3448_REG_FDATA_L     0xFE    /* FIFO 数据低字节（读它推进 FIFO 指针）         */
#define TCS3448_REG_FDATA_H     0xFF    /* FIFO 数据高字节                               */

/*==============================================================================
 *  3. 寄存器位定义
 *============================================================================*/
/* --- ENABLE (0x80) --- */
#define TCS3448_ENABLE_PON      0x01    /* bit0：上电，内部振荡器启动（所有功能的前提）  */
#define TCS3448_ENABLE_ALS_EN   0x02    /* bit1：光谱测量使能                            */
#define TCS3448_ENABLE_WEN      0x08    /* bit3：使能"两次测量之间的等待时间"(WTIME)     */
#define TCS3448_ENABLE_SMUXEN   0x10    /* bit4：启动手动 SMUX 命令（自动完成后自动清 0）*/
#define TCS3448_ENABLE_FDEN     0x40    /* bit6：闪烁检测使能                            */

/* --- CFG0 (0xBF) --- */
#define TCS3448_CFG0_WLONG      0x04    /* bit2：WTIME 放大 16 倍                        */
#define TCS3448_CFG0_REG_BANK1  0x10    /* bit4：置 1 才能访问 0x20~0x7F 的寄存器        */
#define TCS3448_CFG0_LOW_POWER  0x20    /* bit5：空闲时自动进入低功耗                    */

/* --- CONTROL (0xFA) --- */
#define TCS3448_CONTROL_SW_RESET 0x08   /* bit3：写 1 触发器件软复位（上电复位级别）     */

/* --- STATUS2 (0x90) --- */
#define TCS3448_ST2_FDSAT_DIG   0x01    /* bit0：闪烁检测数字饱和                        */
#define TCS3448_ST2_FDSAT_ANA   0x02    /* bit1：闪烁检测模拟饱和                        */
#define TCS3448_ST2_ASAT_ANA    0x08    /* bit3：光谱模拟饱和（光太强，应降低增益）      */
#define TCS3448_ST2_ASAT_DIG    0x10    /* bit4：光谱数字饱和（计数值到顶）              */
#define TCS3448_ST2_AVALID      0x40    /* bit6：一次光谱测量已完成，数据有效            */

/* --- STATUS (0x93)，读到的值原样写回即可清除 --- */
#define TCS3448_ST_SINT         0x01    /* bit0：系统中断（闪烁状态变化 / SMUX 完成）    */
#define TCS3448_ST_FINT         0x04    /* bit2：FIFO 达到阈值中断                       */
#define TCS3448_ST_AINT         0x08    /* bit3：ALS 超过/低于阈值中断                   */
#define TCS3448_ST_ASAT         0x80    /* bit7：ALS 饱和                                */

/* --- STATUS4 (0xBC) --- */
#define TCS3448_ST4_INT_BUSY    0x01    /* bit0：器件正在初始化（上电后约 300us 为 1）   */
#define TCS3448_ST4_SAI_ACTIVE  0x02    /* bit1：因中断进入 SLEEP                        */
#define TCS3448_ST4_ALS_TRIG    0x04    /* bit2：时序错误：WTIME 相对 ATIME 太短         */
#define TCS3448_ST4_FD_TRIG     0x08    /* bit3：闪烁检测时序错误                        */
#define TCS3448_ST4_OVTEMP      0x20    /* bit5：器件温度过高                            */
#define TCS3448_ST4_FIFO_OV     0x80    /* bit7：FIFO 溢出，有数据丢失                   */

/* --- FD_STATUS (0xE3) --- */
#define TCS3448_FD_100HZ_FLICKER 0x01   /* bit0：检测到 100Hz 闪烁                       */
#define TCS3448_FD_100HZ_VALID   0x04   /* bit2：100Hz 判定结果有效                      */
#define TCS3448_FD_120HZ_FLICKER 0x02   /* bit1：检测到 120Hz 闪烁                       */
#define TCS3448_FD_120HZ_VALID   0x08   /* bit3：120Hz 判定结果有效                      */
#define TCS3448_FD_SATURATION    0x10   /* bit4：本次闪烁测量饱和                        */
#define TCS3448_FD_MEAS_VALID    0x20   /* bit5：闪烁测量完成                            */

/*==============================================================================
 *  4. 自动 SMUX(18 通道)模式下 ADATA0~ADATA17 与物理通道的对应关系
 *  ★ 顺序是"器件内部自动扫描的顺序"，不是波长顺序，取数据时务必按下表：
 *
 *   下标  寄存器        周期  ADC 通道  通道名  中心波长 / FWHM      说明
 *   ---------------------------------------------------------------------------
 *     0   0x95/0x96     1     CH0      FZ      450nm / 67nm       宽带蓝
 *     1   0x97/0x98     1     CH1      FY      560nm / 123nm      宽带绿
 *     2   0x99/0x9A     1     CH2      FXL     596nm / 93nm       宽带红
 *     3   0x9B/0x9C     1     CH3      NIR     855nm / 61nm       近红外
 *     4   0x9D/0x9E     1     CH4      VIS_TL  透明(左上 PD)        可见光总强度
 *     5   0x9F/0xA0     1     CH5      VIS_BR  透明(右侧 2 个 PD)   可见光总强度
 *     6   0xA1/0xA2     2     CH0      F2      424nm / 29nm
 *     7   0xA3/0xA4     2     CH1      F3      473nm / 38nm
 *     8   0xA5/0xA6     2     CH2      F4      516nm / 48nm
 *     9   0xA7/0xA8     2     CH3      F6      636nm / 58nm
 *    10   0xA9/0xAA     2     CH4      VIS_TL  透明(左上 PD)        第二周期
 *    11   0xAB/0xAC     2     CH5      VIS_BR  透明(右侧 2 个 PD)   第二周期
 *    12   0xAD/0xAE     3     CH0      F1      407nm / 28nm
 *    13   0xAF/0xB0     3     CH1      F7      687nm / 63nm
 *    14   0xB1/0xB2     3     CH2      F8      748nm / 77nm
 *    15   0xB3/0xB4     3     CH3      F5      546nm / 44nm
 *    16   0xB5/0xB6     3     CH4      VIS_TL  透明(左上 PD)        第三周期
 *    17   0xB7/0xB8     3     CH5      VIS_BR  透明(右侧 2 个 PD)   第三周期
 *
 *  说明：默认情况下(auto_smux 且 FDEN=0)，每个周期的第 5、6 个 ADC 读的是
 *        两个透明通道；如果使能了闪烁检测(FDEN=1)，CH5 会被闪烁检测占用。
 *============================================================================*/
#define TCS3448_CH_FZ         0
#define TCS3448_CH_FY         1
#define TCS3448_CH_FXL        2
#define TCS3448_CH_NIR        3
#define TCS3448_CH_VIS_TL_0   4    /* 第 1 周期的透明通道(左上)                     */
#define TCS3448_CH_VIS_BR_0   5    /* 第 1 周期的透明通道(右侧)                     */
#define TCS3448_CH_F2         6
#define TCS3448_CH_F3         7
#define TCS3448_CH_F4         8
#define TCS3448_CH_F6         9
#define TCS3448_CH_VIS_TL_1   10   /* 第 2 周期透明通道，正常情况下应与 _0 很接近   */
#define TCS3448_CH_VIS_BR_1   11
#define TCS3448_CH_F1         12
#define TCS3448_CH_F7         13
#define TCS3448_CH_F8         14
#define TCS3448_CH_F5         15
#define TCS3448_CH_VIS_TL_2   16   /* 第 3 周期透明通道                             */
#define TCS3448_CH_VIS_BR_2   17

#define TCS3448_CH_COUNT      18   /* 自动 SMUX 一共产生 18 个结果                  */

/*==============================================================================
 *  5. 器件 ID 与配置取值
 *============================================================================*/
#define TCS3448_CHIP_ID       0x81  /* 读 ID 寄存器(0x5A)应该得到这个值，用来判断通信是否正常 */

/* 增益 AGAIN（写 CFG1 的 bit4:0）。增益越高越灵敏，但强光下容易饱和(ASAT)。
 * 换算关系：0 -> 0.5x，1 -> 1x，2 -> 2x …… 每加 1 翻一倍，12 -> 2048x */
#define TCS3448_AGAIN_0_5X    0
#define TCS3448_AGAIN_1X      1
#define TCS3448_AGAIN_2X      2
#define TCS3448_AGAIN_4X      3
#define TCS3448_AGAIN_8X      4
#define TCS3448_AGAIN_16X     5
#define TCS3448_AGAIN_32X     6
#define TCS3448_AGAIN_64X     7
#define TCS3448_AGAIN_128X    8
#define TCS3448_AGAIN_256X    9
#define TCS3448_AGAIN_512X    10
#define TCS3448_AGAIN_1024X   11
#define TCS3448_AGAIN_2048X   12

/* 自动 SMUX 扫描模式（写 CFG20 的 bit6:5） */
#define TCS3448_SMUX_6CH      0     /* 只扫 1 个周期，得到 ADATA0~5                   */
#define TCS3448_SMUX_12CH     2     /* 扫 2 个周期，得到 ADATA0~11                    */
#define TCS3448_SMUX_18CH     3     /* 扫 3 个周期，得到 ADATA0~17（14 通道全都有）   */

/* -----------------------------------------------------------------------------
 *  积分时间 = (ATIME + 1) × (ASTEP + 1) × 2.78us
 *  ADC 满量程 = (ATIME + 1) × (ASTEP + 1)（最大 65535，超过按 65535 算）
 *
 *  常用组合：
 *    ATIME  ASTEP   积分时间     说明
 *    0      599     1.67ms       最快，适合强光或需要高刷新率
 *    9      599     16.7ms       默认值：4 颗传感器轮询(100ms 周期)够用
 *    29     599     50.0ms       原厂推荐，信噪比好，但单颗就要 50ms
 *    255    599     427ms        极弱光
 * -------------------------------------------------------------------------- */
#define TCS3448_DEFAULT_ATIME   9       /* 积分步数：10 步                                */
#define TCS3448_DEFAULT_ASTEP   599     /* 每步 1.668ms，合计约 16.7ms 积分时间           */
#define TCS3448_DEFAULT_AGAIN   TCS3448_AGAIN_256X  /* 默认 256 倍增益，太亮导致饱和时调小 */

/* I²C 单次传输超时时间(ms)，37 字节突发读在 400kHz 下只需约 1ms */
#define TCS3448_I2C_TIMEOUT     50

/* 错误码：每次测量结束放进 tcs3448_data.err，调试时直接看这个字段就知道卡在哪
 *   0 = 正常
 *   1 = I2C 通信失败（从机不应答 / 总线错误）
 *   2 = 器件 ID 不对（读回的不是 0x81）
 *   3 = 等 AVALID 超时：器件在应答，但测量一直没完成
 *   4 = 配置写进去后读回不一致（PON/ATIME/ASTEP/CFG1/CFG20 没生效）*/
#define TCS3448_ERR_NONE        0
#define TCS3448_ERR_I2C         1
#define TCS3448_ERR_ID          2
#define TCS3448_ERR_NOT_READY   3
#define TCS3448_ERR_CONFIG      4

/*==============================================================================
 *  6. 数据结构
 *  TCS3448_GetAllData() 会把传感器的全部信息一次性填进 tcs3448_data：
 *    - 18 个自动 SMUX 原始寄存器值（含 3 个周期的透明通道）
 *    - 14 个光谱通道，已按物理含义命名，方便直接做算法
 *    - 各种状态位（数据是否有效、是否饱和、时序是否出错……）
 *    - 当前配置快照（增益 / 积分时间 / 扫描模式）
 *    - 附带算出的 RGB888（0~255，方便沿用原来 TCS34725 的显示/上报逻辑）
 *============================================================================*/
typedef struct
{
    /* ---------- 18 个自动 SMUX 数据寄存器原始值（下标见 TCS3448_CH_xxx）---------- */
    uint16_t adata[TCS3448_CH_COUNT];

    /* ---------- 14 个光谱通道（单位：ADC 计数值，0~65535）---------- */
    uint16_t f1;        /* 407nm 窄带  紫                                   */
    uint16_t f2;        /* 424nm 窄带  紫                                   */
    uint16_t f3;        /* 473nm 窄带  蓝                                   */
    uint16_t f4;        /* 516nm 窄带  青绿                                 */
    uint16_t f5;        /* 546nm 窄带  绿                                   */
    uint16_t f6;        /* 636nm 窄带  红                                   */
    uint16_t f7;        /* 687nm 窄带  深红                                 */
    uint16_t f8;        /* 748nm 窄带  近红外                               */
    uint16_t fz;        /* 450nm 宽带（FWHM 67nm）蓝                        */
    uint16_t fy;        /* 560nm 宽带（FWHM 123nm）绿                       */
    uint16_t fxl;       /* 596nm 宽带（FWHM 93nm）红                        */
    uint16_t nir;       /* 855nm 近红外                                     */
    uint16_t vis_tl;    /* 透明通道：阵列左上，代表可见光总强度（取第 1 周期）*/
    uint16_t vis_br;    /* 透明通道：阵列右侧 2 个并联合计（取第 1 周期）    */
    uint32_t clear;     /* = vis_tl + vis_br，本驱动自己算的"总光强"；
                           用 32 位是为了两个通道都接近满量程(65535)时不会溢出 */

    /* ---------- 状态信息 ---------- */
    uint8_t  astatus;       /* 0x94 原始值：bit7 饱和标志，bit3:0 本次生效的增益档 */
    uint8_t  again_status;  /* = astatus[3:0]，本次数据实际使用的增益档            */
    uint8_t  sat_astatus;   /* = astatus[7]，本次锁存的数据是否饱和                */
    uint8_t  status2;       /* 0x90 原始值                                        */
    uint8_t  als_valid;     /* 1 = AVALID，一次完整测量已完成                      */
    uint8_t  sat_analog;    /* 1 = 模拟饱和（光太强，应该调小 AGAIN 或积分时间）   */
    uint8_t  sat_digital;   /* 1 = 数字饱和（计数值到量程上限）                    */
    uint8_t  fd_sat_analog; /* 1 = 闪烁检测模拟饱和                                */
    uint8_t  fd_sat_digital;/* 1 = 闪烁检测数字饱和                                */
    uint8_t  status;        /* 0x93 原始值（中断/饱和标志，读后写回可清）          */
    uint8_t  status4;       /* 0xBC 原始值：FIFO 溢出/过温/时序错误/休眠            */
    uint8_t  status5;       /* 0xBB 原始值：闪烁检测状态变化 / SMUX 完成            */
    uint8_t  fd_status;     /* 0xE3 原始值：闪烁检测频率判定结果                    */
    uint8_t  fifo_level;    /* FIFO 中待读的条目数（本驱动默认不用 FIFO）           */

    /* ---------- 调试用：出错时能直接看出问题出在哪 ---------- */
    uint8_t  err;           /* 错误码 TCS3448_ERR_xxx（0 表示正常）                 */
    uint8_t  reg_enable;    /* 读回值：ENABLE(0x80)，测量时正常应为 0x03(PON+ALS_EN)*/
    uint8_t  reg_cfg1;      /* 读回值：CFG1 增益                                    */
    uint8_t  reg_cfg20;     /* 读回值：CFG20 扫描模式                               */

    /* ---------- 当前配置快照 ---------- */
    uint8_t  atime;         /* 当前积分步数                                        */
    uint16_t astep;         /* 当前积分步长                                        */
    uint8_t  again_cfg;     /* 当前 CFG1 里的增益档                                */
    uint8_t  smux_mode;     /* 当前自动 SMUX 模式                                  */
    float    integration_ms;/* 由 atime/astep 算出的实际积分时间(ms)               */

    /* ---------- 由可见通道换算出来的 RGB888（不是原始数据，方便沿用旧逻辑）---------- */
    uint8_t  r;
    uint8_t  g;
    uint8_t  b;
} tcs3448_data;

/* 寄存器快照：每颗传感器初始化后自动填一次，调试时把 tcs_dbg[] 加到 Keil 的
 * Watch 窗口就能看到 ID / ENABLE / 积分时间 / 状态等，快速判断是通信问题还是配置问题 */
typedef struct
{
    uint8_t id;         /* 0x5A 器件 ID，正常 0x81                    */
    uint8_t auxid;      /* 0x58 辅助 ID                              */
    uint8_t revid;      /* 0x59 版本号                                */
    uint8_t cfg0;       /* 0xBF：bit4=REG_BANK                        */
    uint8_t enable;     /* 0x80：bit0=PON，bit1=ALS_EN                */
    uint8_t cfg1;       /* 0xC6：AGAIN 增益                           */
    uint8_t atime;      /* 0x81：积分步数                             */
    uint8_t astep_l;    /* 0xD4：积分步长低字节                       */
    uint8_t astep_h;    /* 0xD5：积分步长高字节                       */
    uint8_t cfg20;      /* 0xD6：auto_smux                            */
    uint8_t status;     /* 0x93                                       */
    uint8_t status2;    /* 0x90：bit6 = AVALID                        */
    uint8_t status4;    /* 0xBC：bit0 = INT_BUSY                      */
    uint8_t init_ok;    /* 1 = 初始化成功                             */
} tcs3448_dbg_t;

extern tcs3448_dbg_t tcs3448_dbg;   /* 最近一次访问的传感器快照 */

/*==============================================================================
 *  7. 对外函数
 *============================================================================*/

/* 读取器件 ID（0x5A），正常返回 TCS3448_CHIP_ID(0x81)，通信失败返回 0xFF */
uint8_t TCS3448_ReadID(I2C_HandleTypeDef *hi2c);

/* 读取辅助 ID / 版本号（只读，调试用） */
uint8_t TCS3448_ReadAUXID(I2C_HandleTypeDef *hi2c);
uint8_t TCS3448_ReadREVID(I2C_HandleTypeDef *hi2c);

/* 初始化：上电复位 -> 校验 ID -> 配置增益/积分时间/18 通道自动扫描 -> 关中断
 * 返回 HAL_OK 表示器件在线且配置成功，HAL_ERROR 表示通信或 ID 校验失败 */
HAL_StatusTypeDef TCS3448_Init(I2C_HandleTypeDef *hi2c);

/* 软件复位（等价于重新上电，所有寄存器回到默认值，复位后需要重新 Init 配置） */
HAL_StatusTypeDef TCS3448_SoftwareReset(I2C_HandleTypeDef *hi2c);

/* 设置增益，again 取 TCS3448_AGAIN_xxx */
HAL_StatusTypeDef TCS3448_SetGain(I2C_HandleTypeDef *hi2c, uint8_t again);

/* 设置积分时间：积分时间 = (atime+1) × (astep+1) × 2.78us
 * atime 0~255，astep 0~65534（0xFFFF 保留），两者不能同时为 0 */
HAL_StatusTypeDef TCS3448_SetIntegrationTime(I2C_HandleTypeDef *hi2c, uint8_t atime, uint16_t astep);

/* 设置自动 SMUX 扫描模式，mode 取 TCS3448_SMUX_xxx（默认 18 通道） */
HAL_StatusTypeDef TCS3448_SetSmuxMode(I2C_HandleTypeDef *hi2c, uint8_t mode);

/* LED 驱动（接在 LDR 脚的补光 LED）：enable 1=开 0=关；current_code 0~127
 * 对应电流 = 4mA + 2mA × current_code，即 4~258mA；不用 LED 就传 0 */
HAL_StatusTypeDef TCS3448_SetLED(I2C_HandleTypeDef *hi2c, uint8_t enable, uint8_t current_code);

/* 清除 STATUS 里的中断/饱和标志（"写 1 清零"） */
HAL_StatusTypeDef TCS3448_ClearStatus(I2C_HandleTypeDef *hi2c);

/* ★核心函数：启动一次测量，等测量完成，然后把全部数据(18 通道原始值 + 14 个光谱
 *  通道 + 状态 + 配置快照)读进 data，最后自动停止测量省电。
 *  返回 HAL_OK / HAL_TIMEOUT(等 AVALID 超时) / 其它 HAL 错误码 */
HAL_StatusTypeDef TCS3448_GetAllData(I2C_HandleTypeDef *hi2c, tcs3448_data *data);

/* ---------- 非阻塞三件套（推荐用在 main_loop 里，不会阻塞 BLE 协议栈） ----------
 * 用法：
 *     TCS3448_StartMeasurement(&hi2c1, &tcs_data[i]);   // 选好通道后启动
 *     while (!TCS3448_MeasurementDone(&hi2c1, &tcs_data[i])) { ...等下一轮... }
 *     TCS3448_ReadResult(&hi2c1, &tcs_data[i]);         // 读数据并停止测量
 * 这三个函数每次只做一次很短的 I2C 传输（约 0.1~1ms），不会卡住主循环 */
HAL_StatusTypeDef TCS3448_StartMeasurement(I2C_HandleTypeDef *hi2c, tcs3448_data *data);
uint8_t TCS3448_MeasurementDone(I2C_HandleTypeDef *hi2c, tcs3448_data *data);
HAL_StatusTypeDef TCS3448_ReadResult(I2C_HandleTypeDef *hi2c, tcs3448_data *data);
HAL_StatusTypeDef TCS3448_StopMeasurement(I2C_HandleTypeDef *hi2c);

/* 把关键寄存器读一遍填进 tcs3448_dbg（Init 里已经自动调用过，也可手动调） */
void TCS3448_DumpRegisters(I2C_HandleTypeDef *hi2c);

/* 不重新启动测量，直接把寄存器里现有的数据读一遍（用于高频轮询：先 GetAllData
 * 启动一次，之后连续用这个函数取数据） */
HAL_StatusTypeDef TCS3448_ReadData(I2C_HandleTypeDef *hi2c, tcs3448_data *data);

/* 用 FZ/FY/FXL 三个宽频可见通道算出 RGB888(0~255)，与原来 TCS34725_GetRGB888
 * 的做法一致（先除以 clear 得到色比，再按最大值归一化）。
 * TCS3448_GetAllData() 内部已经调用过，一般不需要自己再调 */
void TCS3448_ComputeRGB888(tcs3448_data *data);

/* 4 颗传感器的数据缓存（对应原来 tcs34725.c 里的 rgbdata[4]） */
extern tcs3448_data tcs_data[4];

#endif
