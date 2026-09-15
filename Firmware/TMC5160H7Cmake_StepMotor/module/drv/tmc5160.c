/*****************************************************************************
 * @文件: tmc5160.c
 * @作者: cl
 * @日期: 2026-08-25
 * @版本: v1.0
 * @说明: TMC5160 驱动层 = SPI/GPIO 原语 + 芯片功能封装（运动/编码器/状态）
 *        2026-09-09 用户裁决：tmc5160_usr.c 并入本文件（drv 允许芯片功能完整封装）
 * @来源: 自 TMC5160_StepMotor(F407) 移植归一
 * @变更点: ① 片选/使能引脚宏改为 H7 板 U1/U2_SPI_SCN、U1/U2_DRV_ENN；
 *          ② SetMode 改空实现（H7 板 SD_MODE/SPI_MODE 引脚硬接线为 SPI 模式）；
 *          ③ SPI 速率注释 2.625Mbps → 4Mbit/s（prescaler=16 @64MHz）
 * @平台: STM32H750VBT6 (SPI3)
 * @依赖: HAL_SPI, HAL_GPIO
 ****************************************************************************/
#include "drv/tmc5160.h"
#include "main.h"
#include "spi.h"
#include "tim_test.h"   /* TEST_TIM_* 探针宏: 宏版实测/生产版空宏零开销 (probe.md) */

/* ==== CS 高电平间隔: t_CHH ≥ 20ns (datasheet ch04 Fig4.3), 取 10us 裕量 ==== */
/* 依据 .cl/datasheet/TMC5160A_Datasheet_Rev1.14_ch04_4_spi_interface.md: SPI 时序 */
/* 依据 .cl/memory/config.md: stm32_hclk=240MHz, DWT CYCCNT 1tick=4.17ns */

#define TMC5160_SPI_TIMEOUT_MS   100U
#define TMC5160_CS_HIGH_US       10U

/* ==== TMC5160 寄存器地址（usr 层只读常量） ==== */
/* 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p032.md: 寄存器映射 */
#define REG_GCONF          0x00
#define REG_GSTAT          0x01
#define REG_IHOLD_IRUN     0x10
#define REG_TPOWERDOWN     0x11
#define REG_TPWMTHRS       0x13
#define REG_SHORT_CONF     0x09
#define REG_TCOOLTHRS      0x14
#define REG_COOLCONF       0x6D
#define REG_RAMPMODE       0x20
#define REG_XACTUAL        0x21
#define REG_VACTUAL        0x22
#define REG_VSTART         0x23
#define REG_A1             0x24
#define REG_V1             0x25
#define REG_AMAX           0x26
#define REG_VMAX           0x27
#define REG_DMAX           0x28
#define REG_D1             0x2A
#define REG_VSTOP          0x2B
#define REG_TZEROWAIT      0x2C
#define REG_XTARGET        0x2D
#define REG_RAMP_STAT      0x35
#define REG_ENCMODE        0x38
#define REG_X_ENC          0x39
#define REG_ENC_CONST      0x3A
#define REG_ENC_STATUS     0x3B
#define REG_ENC_DEVIATION  0x3D
#define REG_CHOPCONF       0x6C
#define REG_DRV_CONF       0x0A
#define REG_DRVSTATUS      0x6F
#define REG_PWMCONF        0x70

/* ==== 全局实例 ==== */
TMC5160_CHIP_T g_tmc5160_chip1_st;
TMC5160_CHIP_T g_tmc5160_chip2_st;

/* 编码器零位偏移：上电时 X_ENC 的初始值，后续读数减去此值归零 */
static int32_t s_enc_offset[2];

/* 急停锁存（CAN 0x0A）：1=功率级已关断，后续运动命令须先重使能 */
static uint8_t s_estop[2];

static const TMC5160_PROFILE_T s_profiles[TMC5160_PROFILE_COUNT] = {
    {0, 10, 0, 0, 1000, 5000, 1000, 1000, 10},
    {0, 10, 0, 0, 5000, 20000, 5000, 5000, 10},
    {0, 10, 0, 0, 10000, 50000, 10000, 10000, 10},
    {0, 10, 0, 0, 20000, 100000, 20000, 20000, 10},
    /* 组5 超高速: VMAX=2863311=50rev/s @fCLK=15MHz, AMAX/DMAX=40000(加速4.09M µsteps/s²)
     * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md:
     *   VMAX[µsteps/t] t=2^24/fCLK → 50×51200×2^24/15e6=2863311(上限2^23-512=8388608 OK)
     *   AMAX[µsteps/ta²] ta²=2^41/fCLK² → 40000→4.09M µsteps/s² */
    {0, 10, 0, 0, 40000, 2863311, 40000, 40000, 10},
};
/*******************************************************************************************
 *  内部函数部分
********************************************************************************************/

static void S_DelayUs(uint32_t us)
{
    uint32_t start, ticks;
    if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    else if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0)
    {
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    ticks = us * (240U);
    start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < ticks)
    {
        __NOP();
    }
}

/**
 * @输入 reg_value: 读取到的寄存器值
 * @输出 0=有效, 1=无效
 * @说明 0xFFFFFFFF 视为 SPI 读失败哨兵值
 */
static uint8_t S_RegValid(uint32_t reg_value)
{
    if (0xFFFFFFFF == reg_value)
    {
        return 1;
    }
    return 0;
}

/*******************************************************************************************
 *  驱动函数部分
********************************************************************************************/

/**
 * @输入 chip: 芯片编号 TMC5160_CHIP_1/TMC5160_CHIP_2
 * @输出 无
 * @说明 拉低对应芯片 SPI 片选引脚
 */
void DRV_TMC5160_SelectChip(uint8_t chip)
{
    if (TMC5160_CHIP_1 == chip)
    {
        HAL_GPIO_WritePin(U1_SPI_SCN_GPIO_Port, U1_SPI_SCN_Pin,
                          GPIO_PIN_RESET);
    }
    else if (TMC5160_CHIP_2 == chip)
    {
        HAL_GPIO_WritePin(U2_SPI_SCN_GPIO_Port, U2_SPI_SCN_Pin,
                          GPIO_PIN_RESET);
    }
}

/**
 * @输入 chip: 芯片编号
 * @输出 无
 * @说明 拉高对应芯片 SPI 片选引脚
 */
void DRV_TMC5160_DeselectChip(uint8_t chip)
{
    if (TMC5160_CHIP_1 == chip)
    {
        HAL_GPIO_WritePin(U1_SPI_SCN_GPIO_Port, U1_SPI_SCN_Pin,
                          GPIO_PIN_SET);
    }
    else if (TMC5160_CHIP_2 == chip)
    {
        HAL_GPIO_WritePin(U2_SPI_SCN_GPIO_Port, U2_SPI_SCN_Pin,
                          GPIO_PIN_SET);
    }
}

/**
 * @输入 chip: 芯片编号; mode: 模式(1/2/3，保留兼容)
 * @输出 无
 * @说明 空实现——H7 板 TMC5160 SD_MODE/SPI_MODE 引脚硬件硬接线为 SPI 模式，
 *       无模式切换 GPIO（源 F407 板为软件可控，移植时删除）
 */
void DRV_TMC5160_SetMode(uint8_t chip, uint8_t mode)
{
    (void)chip;
    (void)mode;
}

/**
 * @输入 chip: 芯片编号
 * @输出 无
 * @说明 拉低 DRV_ENN 使能电机驱动
 */
void DRV_TMC5160_Enable(uint8_t chip)
{
    if (TMC5160_CHIP_1 == chip)
    {
        HAL_GPIO_WritePin(U1_DRV_ENN_GPIO_Port, U1_DRV_ENN_Pin,
                          GPIO_PIN_RESET);
    }
    else if (TMC5160_CHIP_2 == chip)
    {
        HAL_GPIO_WritePin(U2_DRV_ENN_GPIO_Port, U2_DRV_ENN_Pin,
                          GPIO_PIN_RESET);
    }
}

/**
 * @输入 chip: 芯片编号
 * @输出 无
 * @说明 拉高 DRV_ENN 禁用电机驱动
 */
void DRV_TMC5160_Disable(uint8_t chip)
{
    if (TMC5160_CHIP_1 == chip)
    {
        HAL_GPIO_WritePin(U1_DRV_ENN_GPIO_Port, U1_DRV_ENN_Pin,
                          GPIO_PIN_SET);
    }
    else if (TMC5160_CHIP_2 == chip)
    {
        HAL_GPIO_WritePin(U2_DRV_ENN_GPIO_Port, U2_DRV_ENN_Pin,
                          GPIO_PIN_SET);
    }
}

/**
 * @输入 ms: 毫秒数
 * @输出 无
 * @说明 毫秒延时（HAL_Delay 封装）
 */
void DRV_TMC5160_DelayMs(uint32_t ms)
{
    HAL_Delay(ms);
}

/**
 * @输入 chip: 芯片编号; reg_addr: 寄存器地址(7bit); data: 32 位数据
 * @输出 TMC5160_OK / TMC5160_ERR
 * @说明 通过 SPI 写入 TMC5160 寄存器
 *   SPI 数据报(40-bit, 5字节): Byte0=bit7(1写)+bit6-0地址, Byte1-4=数据
 * @注意 用 TransmitReceive 避免 OVR 标志 (H7 HAL 已适配 FIFO)
 * 依据 .cl/datasheet/TMC5160A_Datasheet_Rev1.14_ch04_4_spi_interface.md 第4章:
 *   SPI DATAGRAM 格式; 写访问地址加 0x80 (ch06.p031)
 */
uint8_t DRV_TMC5160_WriteReg(uint8_t chip, uint8_t reg_addr, uint32_t data)
{
    static uint8_t tx_buf[5];
    static uint8_t rx_buf[5];
    HAL_StatusTypeDef status;

    tx_buf[0] = reg_addr | 0x80;
    tx_buf[1] = (data >> 24) & 0xFF;
    tx_buf[2] = (data >> 16) & 0xFF;
    tx_buf[3] = (data >> 8) & 0xFF;
    tx_buf[4] = data & 0xFF;

    DRV_TMC5160_SelectChip(chip);
    TEST_TIM_Start(0);   /* tag0=写帧 HAL TxRx 窗口 */
    status = HAL_SPI_TransmitReceive(&hspi3, tx_buf, rx_buf, 5,
                                     TMC5160_SPI_TIMEOUT_MS);
    TEST_TIM_Stop(0);
    DRV_TMC5160_DeselectChip(chip);

    /* CS 拉高间隔: 保证背靠背写帧之间 tCSH 达标
     * 依据 .cl/datasheet/TMC5160A_Datasheet_Rev1.14_ch04_4_spi_interface.md §4.3 表:
     *   tCSH > 2*tCLK + 10ns; SCK=4MHz → tCLK=250ns → 2*250+10=510ns 最低要求,
     *   取 10us ≈ 20x 裕量(与 ReadReg 两报间隔同值) */
    S_DelayUs(TMC5160_CS_HIGH_US);

    return (HAL_OK == status) ? TMC5160_OK : TMC5160_ERR;
}

/**
 * @输入 chip: 芯片编号; reg_addr: 寄存器地址(7bit)
 * @输出 32 位寄存器值，失败返回 0xFFFFFFFF
 * @说明 通过 SPI 读取 TMC5160 寄存器，需两报：第一报丢弃旧数据，第二报取新数据
 * @注意 两报之间 CS 拉高需满足 tCSH > 2*tCLK+10ns (datasheet ch04 §4.3 表,
 *       SCK=4MHz → 510ns), 取 10us ≈ 20x 裕量
 * 依据 .cl/datasheet/TMC5160A_Datasheet_Rev1.14_ch04_4_spi_interface.md 第4章:
 *   SPI DATAGRAM 格式
 */
uint32_t DRV_TMC5160_ReadReg(uint8_t chip, uint8_t reg_addr)
{
    static uint8_t tx_buf[5];
    static uint8_t rx_buf[5];
    HAL_StatusTypeDef status;

    tx_buf[0] = reg_addr & 0x7F;
    tx_buf[1] = 0x00;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;
    tx_buf[4] = 0x00;

    /* 第一报：丢弃旧数据 */
    TEST_TIM_Start(1);   /* tag1=读双报总窗(含 10us 帧间隔) */
    DRV_TMC5160_SelectChip(chip);
    status = HAL_SPI_TransmitReceive(&hspi3, tx_buf, rx_buf, 5,
                                     TMC5160_SPI_TIMEOUT_MS);
    DRV_TMC5160_DeselectChip(chip);
    if (HAL_OK != status)
    {
        return 0xFFFFFFFF;
    }

    /* CS 高电平间隔: 10us (t_CHH ≥ 20ns, 取 500× 裕量) */
    S_DelayUs(TMC5160_CS_HIGH_US);

    /* 第二报：获取本次数据 */
    DRV_TMC5160_SelectChip(chip);
    status = HAL_SPI_TransmitReceive(&hspi3, tx_buf, rx_buf, 5,
                                     TMC5160_SPI_TIMEOUT_MS);
    DRV_TMC5160_DeselectChip(chip);
    TEST_TIM_Stop(1);
    if (HAL_OK != status)
    {
        return 0xFFFFFFFF;
    }

    return ((uint32_t)rx_buf[1] << 24) |
           ((uint32_t)rx_buf[2] << 16) |
           ((uint32_t)rx_buf[3] << 8) |
           (uint32_t)rx_buf[4];
}

/* ==== 调试: 原始收发字节回显 (HAL 版, 供 comm_test 打印) ==== */
uint8_t DRV_TMC5160_DebugTransfer(uint8_t chip, uint8_t *tx, uint8_t *rx, uint8_t len)
{
    HAL_StatusTypeDef status;

    if (NULL == tx || NULL == rx || 5 != len)
    {
        return 1;
    }
    DRV_TMC5160_SelectChip(chip);
    status = HAL_SPI_TransmitReceive(&hspi3, tx, rx, 5, TMC5160_SPI_TIMEOUT_MS);
    DRV_TMC5160_DeselectChip(chip);
    return (HAL_OK == status) ? 0 : 1;
}

/*******************************************************************************************
 *  用户函数部分
 ********************************************************************************************/

/* 急停恢复：锁存有效时重使能（使能后延时等校准，时序同 init 使能段） */
static void S_RecoverIfEStop(TMC5160_CHIP_T *chip)
{
    uint8_t idx;

    if ((void *)0 == chip)
    {
        return;
    }
    if (TMC5160_CHIP_1 != chip->chip_number &&
        TMC5160_CHIP_2 != chip->chip_number)
    {
        return;
    }
    idx = (uint8_t)(chip->chip_number - 1U);
    if (0U != s_estop[idx])
    {
        s_estop[idx] = 0;
        DRV_TMC5160_Enable(chip->chip_number);
        DRV_TMC5160_DelayMs(10);
    }
}

/**
 * @输入 chip: 芯片编号(1/2)
 * @输出 无
 * @说明 急停：拉高 DRV_ENN 关断功率级，轴自由停车（不锁轴）；
 *        置锁存，后续运动命令自动重使能；位置已丢失须重回零
 */
void USR_TMC5160_EStop(uint8_t chip)
{
    if (TMC5160_CHIP_1 != chip && TMC5160_CHIP_2 != chip)
    {
        return;
    }
    DRV_TMC5160_Disable(chip);
    s_estop[chip - 1U] = 1;
}

/**
 * @输入 无
 * @输出 无
 * @说明 初始化两片 TMC5160：默认配置/清错/基础寄存器/编码器/电流/使能
 * @注意 模式固定 SPI 模式（H7 板模式引脚硬接线），closed_loop 默认关
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p032.md: GCONF
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p048.md: CHOPCONF
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p038.md: IHOLD_IRUN/TPOWERDOWN
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p045.md: ENCMODE/ENC_CONST
 */
void USR_TMC5160_Init(void)
{
    uint8_t i;
    uint32_t gstat;
    TMC5160_CHIP_T *chips[2] = { &g_tmc5160_chip1_st, &g_tmc5160_chip2_st };

    /* 等待 TMC5160 上电稳定后再操作 SPI */
    DRV_TMC5160_DelayMs(50);

    for (i = 0; i < 2; i++)
    {
        TMC5160_CHIP_T *chip = chips[i];
        chip->chip_number = i + 1;

        /* 固定默认配置（RAM 常驻，上电初始化写入） */
        chip->mode = TMC5160_MODE_POSITION;
        chip->closed_loop = 0;

        /* 保持 DRV_ENN 高电平(禁用), 上电默认寄存器尚未配置, 此时使能会瞬时过流
         * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p006.md 使能时序:
         *   上电→保持使能脚上拉(禁用)→写入电机配置→延时→拉低使能→延时 */

        /* 清除 Power-on 残留错误，同时验证 SPI 通信 */
        USR_TMC5160_WriteReg(chip, REG_GSTAT, 0x07);
        DRV_TMC5160_DelayMs(1);
        gstat = USR_TMC5160_ReadReg(chip, REG_GSTAT);
        if (0xFFFFFFFF == gstat)
        {
            /* SPI 通信异常，重试一次 */
            DRV_TMC5160_DelayMs(10);
            USR_TMC5160_WriteReg(chip, REG_GSTAT, 0x07);
            DRV_TMC5160_DelayMs(1);
            gstat = USR_TMC5160_ReadReg(chip, REG_GSTAT);
        }

        /* 斩波模式: GCONF=0x00 → en_pwm_mode=0, 全程 SpreadCycle
         * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p032.md: GCONF.en_pwm_mode
         * 历史: 2026-09-09 静音对照测试(0x04)已毕——SOAK r4 证明静音直调同样 100%
         *       丢步不转, 对照结论=非斩波模式独有问题; 2026-09-10 撤钩子回生产值,
         *       恢复 S2/OL 检测有效性(OL 精度 SpreadCycle 最高, ch11 §11.3) */
        USR_TMC5160_WriteReg(chip, REG_GCONF, 0x00);

        /* CHOPCONF: TOFF=5, TBL=%11(54clk 最长死区), MRES=%0000(256微步) → 0x000181C5
         * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p051.md / ch06.p052.md: CHOPCONF
         * 说明: MRES=0=256微步配合内部运动控制器; TBL=%11 为源工程实测调优值——加长比较器
         *       死区抑制大电流加速段方向相关 S2GA 短路误触发。其余斩波参数由 PWMCONF 决定。
         * 历史: 2026-09-09 曾写 0xC00181C5(bit31 diss2vs+bit30 diss2g 关短路检测)做
         *       误触发对照, 2026-09-10 撤钩子恢复生产值 → S2G/S2VS 检测重新生效 */
        USR_TMC5160_WriteReg(chip, REG_CHOPCONF, 0x000181C5);

        /* DRV_CONF: DRVSTRENGTH=00(weak), 降低栅极驱动电流
         * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch03.p017.md:
         *   表3.3 MOSFET Miller Charge VS DRVSTRENGTH
         * 推导: AOD4126 Qgd=10nC(typ) ∈ 10~20nC → DRVSTRENGTH=0(weak)
         * 复位缺省 %10(medium) 栅极驱动过强 → 开关振铃/额外发热
         * 只写寄存器无法读回, 其余按复位缺省: BBMTIME=0, BBMCLKS=4,
         *   OTSELECT=0, FILT_ISENSE=0 */
        USR_TMC5160_WriteReg(chip, REG_DRV_CONF, 0x00000400);

        /* SHORT_CONF: 短路检测灵敏度最低
         * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p035.md:
         *   SHORT_CONF
         * 目的: 大步偶发 S2GA/S2GB 短路误检测(关断放开电机)
         * OTP 默认灵敏度在大电流+高 dv/dt 下误触发,降至最低消除
         * 值: 0x7030F = shortdelay(bit18)=1 | SHORTFILTER=3
         *   | S2G_LEVEL=15 | S2VS_LEVEL=15 */
        USR_TMC5160_WriteReg(chip, REG_SHORT_CONF, 0x0007030F);

        /* PWMCONF: StealthChop 斩波频率 = %01 → fPWM=2/683×15MHz≈43.9kHz
         * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch18_18_sine_wave_look.md: PWM 频率选择
         * 推导: fCLK=15MHz(TIM4_CH3@PD14, TIM4CLK=240MHz/(PSC=0)/(ARR=15+1)), 复位 %00=29.3kHz 偏低,
         *       %01=43.9kHz 落 36~48kHz 推荐区间(源工程 14MHz 时为 41kHz, 同档位)。其余位:
         *       pwm_autoscale=1/pwm_autograd=1 自动调校, PWM_OFS=30, PWM_GRAD=12, PWM_LIM=12。 */
        USR_TMC5160_WriteReg(chip, REG_PWMCONF, 0xC40D001E);

        /* 编码器配置 */
        USR_TMC5160_ConfigEncoder(chip);

        /* 电机电流: bit 域 IHOLD[3:0] | IRUN[11:8] | IHOLDDELAY[19:16]
         * (2026-09-10 修: 参考代码/本工程旧注释"IHOLD=8,DELAY=6"为位域写反误读,
         *  实际代码值一直是 DELAY=8/IRUN=20/IHOLD=6)
         * IRUN 推导: 采用 F407 参考代码实测验证值 CS=20:
         *   IRMS = (20+1)/32 × IFS/√2 = 21/32 × (0.325/0.05)/1.414 = 3.02A RMS
         *   历史: CS=27(≈4A 额定) 双机满流经 OTPW(120°C) → 热致 S2 误触发;
         *         CS=20 后 150 轮浸泡 0 次 (源工程 tmc5160_usr.c 2026-08-14 定案)
         *   (2026-09-10 曾推 CS=13→2.01A 降热方案, 按用户裁决回退参考值 20;
         *    若发热仍大再切 13: 14/32×6.5/1.414=2.01A, 铜损 3.4W vs 现 7.7W)
         * IHOLD 推导: 3(0.576A) 实测锁不住轴(用户 2026-09-10) → 回调 6:
         *   (6+1)/32 × 6.5/1.414 = 1.01A RMS, 保持力矩 ≈ 1.3×1.01/4 ≈ 0.33N·m,
         *   静止铜损 = 2×1.01²×0.42 = 0.85W
         * 依据 .cl/memory/config.md tmc5160_ifs=6.5A / tmc5160_vfs=0.325V +
         *      电机规格书 R=0.42Ω 额定4A 保持力矩1.3N·m +
         *      ..\TMC5160_StepMotor tmc5160_usr.c:267 实测校准史 */
        USR_TMC5160_WriteReg(chip, REG_IHOLD_IRUN, (8 << 16) | (20 << 8) | 6);

        /* 静止降流延迟: 2^18 tCLK 单位, 40 → 40×262144/15e6 ≈ 699ms
         * 需 >=2 保证 StealthChop PWM 自动调校正常 */
        USR_TMC5160_WriteReg(chip, REG_TPOWERDOWN, 40);

        /* CoolStep 关闭 */
        USR_TMC5160_WriteReg(chip, REG_COOLCONF, 0x0000);

        /* CoolStep 速度窗口: 0 = 关闭 */
        USR_TMC5160_WriteReg(chip, REG_TCOOLTHRS, 0);

        /* TPWMTHRS=0: en_pwm_mode=0(GCONF=0x00) 下本寄存器不参与斩波切换, 写 0 保
         * 源工程行为等价 (2026-09-10 撤静音对照后注释修正)
         * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch05.p038.md:
         *   0x10-0x1F 速度相关控制寄存器组 */
        USR_TMC5160_WriteReg(chip, REG_TPWMTHRS, 0);

        /* 使能时序: 配置写完后延时稳定, 再拉低 DRV_ENN 使能, 随后延时等待校准
         * 避免上电瞬时过流(当前/斩波参数已就绪时才导通功率级) */
        DRV_TMC5160_DelayMs(10);
        DRV_TMC5160_Enable(chip->chip_number);
        DRV_TMC5160_DelayMs(10);
    }

    /* 等待斩波校准稳定 */
    DRV_TMC5160_DelayMs(100);

    /* 记录编码器上电初始值，后续读数减去此偏移归零 */
    s_enc_offset[0] = (int32_t)USR_TMC5160_ReadReg(&g_tmc5160_chip1_st, REG_X_ENC);
    s_enc_offset[1] = (int32_t)USR_TMC5160_ReadReg(&g_tmc5160_chip2_st, REG_X_ENC);
}

/* ==== 寄存器读写 ==== */

/**
 * @输入 chip: 芯片指针; reg_addr: 寄存器地址; data: 数据
 * @输出 TMC5160_SUCCESS / TMC5160_FAIL
 * @说明 写寄存器（转发 drv 层）
 */
uint8_t USR_TMC5160_WriteReg(TMC5160_CHIP_T *chip, uint8_t reg_addr, uint32_t data)
{
    return DRV_TMC5160_WriteReg(chip->chip_number, reg_addr, data);
}

/**
 * @输入 chip: 芯片指针; reg_addr: 寄存器地址
 * @输出 寄存器值，失败 0xFFFFFFFF
 * @说明 读寄存器（转发 drv 层）
 */
uint32_t USR_TMC5160_ReadReg(TMC5160_CHIP_T *chip, uint8_t reg_addr)
{
    return DRV_TMC5160_ReadReg(chip->chip_number, reg_addr);
}

/**
 * @输入 chip: 芯片指针; profile_id: 运动参数组 ID(1~5)
 * @输出 无
 * @说明 按预配置运动参数组设置斜坡寄存器
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md:
 *   VSTART/A1/V1/AMAX/VMAX/DMAX/D1/VSTOP/TZEROWAIT
 */
void USR_TMC5160_ApplyProfile(TMC5160_CHIP_T *chip, uint8_t profile_id)
{
    const TMC5160_PROFILE_T *p;

    if (0 == profile_id || TMC5160_PROFILE_COUNT < profile_id)
    {
        profile_id = 1;
    }
    p = &s_profiles[profile_id - 1];

    USR_TMC5160_WriteReg(chip, REG_VSTART, p->vstart);
    USR_TMC5160_WriteReg(chip, REG_VSTOP, p->vstop);
    USR_TMC5160_WriteReg(chip, REG_V1, p->v1);
    USR_TMC5160_WriteReg(chip, REG_A1, p->a1);
    USR_TMC5160_WriteReg(chip, REG_AMAX, p->amax);
    USR_TMC5160_WriteReg(chip, REG_VMAX, p->vmax);
    USR_TMC5160_WriteReg(chip, REG_DMAX, p->dmax);
    USR_TMC5160_WriteReg(chip, REG_D1, p->d1);
    USR_TMC5160_WriteReg(chip, REG_TZEROWAIT, p->tzerowait);
}

/* ==== 运动控制 ==== */

/**
 * @输入 chip: 芯片指针; target: 目标绝对位置
 * @输出 无
 * @说明 绝对定位，RAMPMODE=0 位置模式
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md:
 *   RAMPMODE/XTARGET
 */
void USR_TMC5160_MoveTo(TMC5160_CHIP_T *chip, int32_t target)
{
    S_RecoverIfEStop(chip);
    USR_TMC5160_WriteReg(chip, REG_RAMPMODE, 0);
    USR_TMC5160_WriteReg(chip, REG_XTARGET, (uint32_t)target);
}

/**
 * @输入 chip: 芯片指针; offset: 相对偏移量(+正转, -反转)
 * @输出 无
 * @说明 从当前位置运动指定偏移量
 */
void USR_TMC5160_MoveBy(TMC5160_CHIP_T *chip, int32_t offset)
{
    int32_t current = USR_TMC5160_GetPosition(chip);

    USR_TMC5160_MoveTo(chip, current + offset);
}

/**
 * @输入 chip: 芯片指针; velocity: 目标速度(+正转, -反转)
 * @输出 无
 * @说明 速度模式持续旋转
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md:
 *   RAMPMODE 速度模式
 */
void USR_TMC5160_SetVelocity(TMC5160_CHIP_T *chip, int32_t velocity)
{
    S_RecoverIfEStop(chip);
    if (0 <= velocity)
    {
        USR_TMC5160_WriteReg(chip, REG_RAMPMODE, 1);
        USR_TMC5160_WriteReg(chip, REG_VMAX, (uint32_t)velocity);
    }
    else
    {
        USR_TMC5160_WriteReg(chip, REG_RAMPMODE, 2);
        USR_TMC5160_WriteReg(chip, REG_VMAX, (uint32_t)(-velocity));
    }
}

/**
 * @输入 chip: 芯片指针
 * @输出 无
 * @说明 立即停止，切回定位模式保持锁轴
 */
void USR_TMC5160_Stop(TMC5160_CHIP_T *chip)
{
    USR_TMC5160_WriteReg(chip, REG_VMAX, 0);
    USR_TMC5160_WriteReg(chip, REG_RAMPMODE, 0);
}

/* ==== 位置读取 ==== */

/**
 * @输入 chip: 芯片指针
 * @输出 int32_t: 当前位置(XACTUAL)
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md: XACTUAL
 */
int32_t USR_TMC5160_GetPosition(TMC5160_CHIP_T *chip)
{
    return (int32_t)USR_TMC5160_ReadReg(chip, REG_XACTUAL);
}

/**
 * @输入 chip: 芯片指针
 * @输出 int32_t: VACTUAL 当前实际速度（有符号，负=反转）
 * @依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch05.p038.md:
 *   R 0x22 20 VACTUAL（斜坡发生器实时速度）
 */
int32_t USR_TMC5160_GetVelocity(TMC5160_CHIP_T *chip)
{
    return (int32_t)USR_TMC5160_ReadReg(chip, 0x22);
}

/**
 * @输入 chip: 芯片指针
 * @输出 uint32_t: RAMP_STAT 寄存器值
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md: RAMP_STAT
 */
uint32_t USR_TMC5160_GetRampStat(TMC5160_CHIP_T *chip)
{
    return USR_TMC5160_ReadReg(chip, REG_RAMP_STAT);
}

/**
 * @输入 chip: 芯片指针
 * @输出 uint32_t: DRV_STATUS 寄存器值
 * 依据 .cl/datasheet/TMC5160A_Datasheet_Rev1.14_ch11_11_diagnostics_and_protection.md
 */
uint32_t USR_TMC5160_GetDrvStatus(TMC5160_CHIP_T *chip)
{
    return USR_TMC5160_ReadReg(chip, REG_DRVSTATUS);
}

/**
 * @输入 chip: 芯片指针
 * @输出 uint32_t: GSTAT 寄存器值（reset/drv_err/uv_cp）
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p033.md: GSTAT
 */
uint32_t USR_TMC5160_GetGStat(TMC5160_CHIP_T *chip)
{
    return USR_TMC5160_ReadReg(chip, REG_GSTAT);
}

/* ==== 状态标志 ==== */

/**
 * @输入 chip: 芯片指针
 * @输出 uint8_t 状态标志位
 *   bit0=到位, bit1=失步, bit2=过温, bit3=驱动错误, bit4=SPI通讯异常
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md: RAMP_STAT
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p045.md: ENC_STATUS
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p033.md: GSTAT/DRV_STATUS
 */
uint8_t USR_TMC5160_GetStatusFlags(TMC5160_CHIP_T *chip)
{
    uint32_t ramp_stat, drv_status, gstat, enc_stat;
    uint8_t flags = 0;

    ramp_stat = USR_TMC5160_ReadReg(chip, REG_RAMP_STAT);
    drv_status = USR_TMC5160_ReadReg(chip, REG_DRVSTATUS);
    gstat = USR_TMC5160_ReadReg(chip, REG_GSTAT);
    enc_stat = USR_TMC5160_ReadReg(chip, REG_ENC_STATUS);

    /* SPI 通讯异常检测 */
    if (S_RegValid(ramp_stat) ||
        S_RegValid(drv_status) ||
        S_RegValid(gstat))
    {
        flags |= 0x10;
        return flags;
    }

    /* bit0: 到位 - RAMP_STAT.bit9 */
    if (ramp_stat & (1UL << 9))
    {
        flags |= 0x01;
        USR_TMC5160_WriteReg(chip, REG_RAMP_STAT, (1UL << 9));
    }
    /* bit1: 失步 - ENC_STATUS.bit1 (deviation_warn) */
    if (enc_stat & (1UL << 1))
    {
        flags |= 0x02;
        USR_TMC5160_WriteReg(chip, REG_ENC_STATUS, (1UL << 1));
    }
    /* bit2: 过温 - DRV_STATUS.bit26/25 */
    if (drv_status & (3UL << 25))
    {
        flags |= 0x04;
    }
    /* bit3: 驱动错误 - GSTAT.bit1 (drv_err) */
    if (gstat & 0x02)
    {
        flags |= 0x08;
        USR_TMC5160_WriteReg(chip, REG_GSTAT, 0x02);
    }

    return flags;
}

/**
 * @输入 chip: 芯片指针
 * @输出 uint8_t: 运动阶段标志
 *   bit0=加速, bit1=匀速, bit2=减速, bit3=归零等待, bit4=静止锁轴
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md: RAMP_STAT
 */
uint8_t USR_TMC5160_GetMotionPhase(TMC5160_CHIP_T *chip)
{
    uint32_t ramp_stat;
    uint8_t phase = 0;

    ramp_stat = USR_TMC5160_ReadReg(chip, REG_RAMP_STAT);

    if (ramp_stat & (1UL << 5))
    {
        phase |= 0x01;    /* 加速 */
    }
    if (ramp_stat & (1UL << 6))
    {
        phase |= 0x02;    /* 匀速 */
    }
    if (ramp_stat & (1UL << 7))
    {
        phase |= 0x04;    /* 减速 */
    }
    if (ramp_stat & (1UL << 10))
    {
        phase |= 0x08;    /* 归零等待 */
    }
    if (ramp_stat & (1UL << 0))
    {
        phase |= 0x10;    /* 静止锁轴 */
    }

    return phase;
}

/* ==== 编码器 ==== */

/**
 * @输入 chip: 芯片指针
 * @输出 无
 * @说明 配置编码器接口
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch20_20_abn_incremental_encoder.md:
 *   ENCMODE/ENC_CONST/ENC_DEVIATION
 */
void USR_TMC5160_ConfigEncoder(TMC5160_CHIP_T *chip)
{
    USR_TMC5160_WriteReg(chip, REG_ENCMODE, 0x00);
    USR_TMC5160_WriteReg(chip, REG_ENC_CONST, 0xFFF33333);
    USR_TMC5160_WriteReg(chip, REG_ENC_DEVIATION, TMC5160_ENC_TOLERANCE);
}

/**
 * @输入 chip: 芯片指针
 * @输出 int32_t: 编码器当前位姿(X_ENC 归零后)
 */
int32_t USR_TMC5160_GetEncoderPosition(TMC5160_CHIP_T *chip)
{
    int32_t raw = (int32_t)USR_TMC5160_ReadReg(chip, REG_X_ENC);
    int32_t idx = (TMC5160_CHIP_1 == chip->chip_number) ? 0 : 1;
    return raw - s_enc_offset[idx];
}

/**
 * @输入 chip: 芯片指针
 * @输出 uint32_t: ENC_STATUS 寄存器值
 */
uint32_t USR_TMC5160_GetEncoderStatus(TMC5160_CHIP_T *chip)
{
    return USR_TMC5160_ReadReg(chip, REG_ENC_STATUS);
}

/**
 * @输入 chip: 芯片指针
 * @输出 int32_t: 编码器偏差绝对值 (X_ENC - XACTUAL)
 */
int32_t USR_TMC5160_GetEncoderDeviation(TMC5160_CHIP_T *chip)
{
    int32_t enc_pos = USR_TMC5160_GetEncoderPosition(chip);
    int32_t x_actual = USR_TMC5160_GetPosition(chip);
    int32_t diff = enc_pos - x_actual;

    if (diff < 0)
    {
        diff = -diff;
    }
    return diff;
}

/**
 * @输入 chip: 芯片指针; expected_steps: 期望运动步数
 * @输出 0=在容差内, 1=超出容差
 * @说明 验证编码器实际运动量是否符合预期
 */
uint8_t USR_TMC5160_CheckPosition(TMC5160_CHIP_T *chip, int32_t expected_steps)
{
    int32_t enc_before, enc_after;
    int32_t actual_delta, error;

    enc_before = USR_TMC5160_GetEncoderPosition(chip);
    DRV_TMC5160_DelayMs(10);
    enc_after = USR_TMC5160_GetEncoderPosition(chip);

    actual_delta = enc_after - enc_before;
    error = actual_delta - expected_steps;
    if (error < 0)
    {
        error = -error;
    }

    return (error <= TMC5160_ENC_TOLERANCE) ? 0 : 1;
}

/* ==== 带验证的运动 ==== */

/**
 * @输入 chip: 芯片指针; timeout_ms: 超时(ms)
 * @输出 TMC5160_MOVE_RESULT_T
 * @说明 等待芯片完成定位，检查 RAMP_STAT.bit9(position_reached)
 */
TMC5160_MOVE_RESULT_T USR_TMC5160_WaitPosition(TMC5160_CHIP_T *chip, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    uint32_t ramp_stat;

    while (elapsed < timeout_ms)
    {
        ramp_stat = USR_TMC5160_GetRampStat(chip);

        if (0xFFFFFFFF == ramp_stat || 0 == ramp_stat)
        {
            return TMC5160_MOVE_SPI_ERROR;
        }

        if (ramp_stat & (1UL << 9))
        {
            return TMC5160_MOVE_OK;
        }

        DRV_TMC5160_DelayMs(5);
        elapsed += 5;
    }

    return TMC5160_MOVE_TIMEOUT;
}

/**
 * @输入 chip: 芯片指针; target: 目标绝对位置
 * @输出 TMC5160_MOVE_RESULT_T
 * @说明 执行位置运动并验证编码器精度，偏差超限自动重试
 */
TMC5160_MOVE_RESULT_T USR_TMC5160_MoveToWithVerify(TMC5160_CHIP_T *chip, int32_t target)
{
    TMC5160_MOVE_RESULT_T result;
    int32_t current_pos, move_delta;
    int32_t enc_before, enc_after;
    int32_t deviation;
    uint8_t retry;

    current_pos = USR_TMC5160_GetPosition(chip);
    move_delta = target - current_pos;

    for (retry = 0; retry < TMC5160_MAX_RETRY; retry++)
    {
        /* 清除残留错误 */
        USR_TMC5160_WriteReg(chip, REG_GSTAT, 0x07);

        enc_before = USR_TMC5160_GetEncoderPosition(chip);

        USR_TMC5160_MoveTo(chip, target);

        result = USR_TMC5160_WaitPosition(chip, TMC5160_MOVE_TIMEOUT_MS);
        if (TMC5160_MOVE_OK != result)
        {
            return result;
        }

        enc_after = USR_TMC5160_GetEncoderPosition(chip);

        deviation = (enc_after - enc_before) - move_delta;
        if (deviation < 0)
        {
            deviation = -deviation;
        }

        if (deviation <= TMC5160_ENC_TOLERANCE)
        {
            return TMC5160_MOVE_OK;
        }

        /* 偏差超限，以编码器为基准修正 */
        current_pos = USR_TMC5160_GetEncoderPosition(chip);
        target = current_pos + move_delta;
    }

    return TMC5160_MOVE_DEVIATION;
}

