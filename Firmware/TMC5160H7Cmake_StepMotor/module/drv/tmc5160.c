/*****************************************************************************
 * @文件: tmc5160.c
 * @作者: cl
 * @日期: 2026-08-25
 * @版本: v1.0
 * @说明: TMC5160 驱动层 = SPI/GPIO 原语 + 芯片功能封装（运动/编码器/状态/回零原语）
 *        2026-09-09 用户裁决：tmc5160_usr.c 并入本文件（drv 允许芯片功能完整封装）
 *        2026-09-17 用户裁决：回零拆分为 drv 基础控制（TMC5160_Home* 芯片原语）+
 *        app/homing 编排（状态机/时序/CAN 反馈/闭环目标同步）
 * @来源: 自 TMC5160_StepMotor(F407) 移植归一
 * @变更点: ① 片选/使能引脚宏改为 H7 板 U1/U2_SPI_SCN、U1/U2_DRV_ENN；
 *          ② SetMode 改空实现（H7 板 SD_MODE/SPI_MODE 引脚硬接线为 SPI 模式）；
 *          ③ SPI 速率注释 2.625Mbps → 4Mbit/s（prescaler=16 @64MHz）
 * @平台: STM32H750VBT6 (SPI3)
 * @依赖: SPI3 寄存器(CubeMX 配置), HAL_GPIO
 ****************************************************************************/
#include "drv/tmc5160.h"
#include "drv/can.h"        /* CAN_SendMotionFeedback: 回零终态反馈 (drv→drv) */
#include "drv/uart_dbg.h"   /* UART_DBG_Printf: 寻零 SG 跟踪诊断 */
#include "main.h"
#include "spi.h"
#include "tim_test.h"   /* TEST_TIM_* 探针宏: 宏版实测/生产版空宏零开销 (probe.md) */

/* ==== CS 高电平间隔: tCSH > 2*tCLK+10ns=320ns @6.45MHz (ch04 §4.3), 取 5us 裕量 ==== */
/* 依据 TMC5160A_Datasheet_Rev1.14_ch04_4_spi_interface.md: SPI 时序 */
/* 依据 .cl/memory/config.md: DWT=CPU=SYSCLK=480MHz(D1CPRE=1), 1tick=2.083ns */

#define TMC5160_CS_HIGH_US       5U

/* SPI3 属 high-end 实例，TX/RX FIFO 各 16 字节
 * 依据 stm32h750xx.h IS_SPI_HIGHEND_INSTANCE(SPI1/2/3)
 *      + stm32h7xx_hal_spi.h SPI_HIGHEND_FIFO_SIZE=16
 * 5 字节数据报 ≤ FIFO 深度 → 可一次压入 TX 后再取 RX，无需分片 */
#define TMC5160_SPI_FIFO_DEPTH   16U
/* 轮询收敛兜底：40bit@6.45MHz≈6.2us，HCLK 240MHz 下正常收敛仅数百次迭代，
 * 取 100000 作上界，仅防 SPI 异常时死循环（对应 HAL 的 Timeout 语义） */
#define TMC5160_SPI_POLL_GUARD   100000UL

/* ==== TMC5160 寄存器地址（usr 层只读常量） ==== */
/* 依据 TMC5160A_Datasheet_Rev1.14.ch06.p032.md: 寄存器映射 */
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
/* 依据 TMC5160A_Datasheet_Rev1.14.ch06.p042.md: SW_MODE/RAMP_STAT */
#define REG_SW_MODE        0x34
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

/* 加速度优化(2026-09-18, VMAX冻结): 各组AMAX/DMAX/D1=干净最大×降额, DMAX=AMAX满足ch22(DMAX≥AMAX)
 * 依据 TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md:
 *   AMAX[µsteps/ta²] ta²=2^41/fCLK²(fCLK=15MHz) → 28000≈2.87M µsteps/s²=56rev/s²,
 *   39200≈4.01M µsteps/s²=78rev/s²
 * 方法: AMAX自顶向下探(40000=已实测可写上限, 更高须回读验证未做), 每候选rel往返正反各2趟,
 *   判据=终态bit0=1且失步位0; G1~G5取40000×0.7=28000, G6取40000×0.98=39200(极限语义)
 * 实测: G1/G2/G3/G4/G5@40000各4腿全干净, G6@30000/@40000各4腿全干净(2026-09-18) */
static const TMC5160_PROFILE_T s_profiles[TMC5160_PROFILE_COUNT] = {
    {0, 10, 0, 0, 28000, 5000, 28000, 28000, 10},
    {0, 10, 0, 0, 28000, 20000, 28000, 28000, 10},
    {0, 10, 0, 0, 28000, 50000, 28000, 28000, 10},
    {0, 10, 0, 0, 28000, 100000, 28000, 28000, 10},
    /* 组5 生产高速档: VMAX=572303=10rev/s=600rpm(需求锁上限)@fCLK=15MHz,
     * AMAX/DMAX=28000(加速2.87M µsteps/s²=56rev/s², 0.18s达速; 40000试探干净×0.7)
     * 依据 TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md:
     *   VMAX[µsteps/t] t=2^24/fCLK → 10×51200×2^24/15e6=572303(上限2^23-512=8388608 OK)
     *   AMAX[µsteps/ta²] ta²=2^41/fCLK² → 20000×15e6²/2^41≈2.05M µsteps/s²
     * 依据 .cl/memory/config.md: 24V+传送带实测失速边界1200<stall≤1500rpm(U2先丢),
     *   600rpm双电机15s零滑动PASS(2026-09-18), 裕量2倍; 原50rps档已删(超需求锁×5且必失速)
     * 依据 .cl/memory/config.md stm32_dwt_cyccnt_clk: fCLK=TIM4 15MHz(TIM4CLK=240MHz/16) */
    {0, 10, 0, 0, 28000, 572303, 28000, 28000, 10},
    /* 组6 极限运行档: VMAX=1375452=1442rpm @fCLK=15MHz, AMAX/DMAX=39200(40000试探干净×0.98)
     * 依据 TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md:
     *   VMAX[µsteps/t] t=2^24/fCLK → 1442/60×51200×2^24/15e6=1375452(上限OK)
     * 依据 .cl/memory/config.md: 弱电机U2失速边界(1471,1481]rpm, 本档=1471×0.98取整,
     *   U1边界(1536,1572]; 24V+传送带+AMAX=20000下二分实测(2026-09-18);
     *   极限运行专用·生产禁用·有人值守·每次使用后检查失步位, 丢步须断电重建零点 */
    {0, 10, 0, 0, 39200, 1375452, 39200, 39200, 10},
};
/* ==== 内部函数 ==== */

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
    ticks = us * (480U);   /* DWT=CPU=480MHz */
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

/* ==== SPI/GPIO 原语 ==== */

/**
 * @输入 tx: 发送缓冲(5 字节); rx: 接收缓冲(5 字节); len: 字节数(数据报=5)
 * @输出 TMC5160_OK / TMC5160_ERR
 * @说明 寄存器级全双工收发，替代 HAL_SPI_TransmitReceive 以去除 HAL 固定开销
 * @注意 SPI 模式/分频/CPOL/CPHA 仍由 CubeMX HAL 配置，本函数只替换收发路径
 * 依据 TMC5160A_Datasheet_Rev1.14_ch04_4_spi_interface.md 第4章:
 *   40-bit 数据报(8bit 地址 + 32bit 数据)，需两报读一寄存器
 * 依据 RM0433(STM32H7) SPI 传输序列(HAL stm32h7xx_hal_spi.c:1410-1647):
 *   SPI_CR2.TSIZE 设数据数 → SPI_CR1.SPE 使能 → SPI_CR1.CSTART 启动
 *   → 轮询 SPI_SR.TXP 写 SPI_TXDR / SPI_SR.RXP 读 SPI_RXDR
 *   → 等 SPI_SR.EOT → 写 SPI_IFCR 清 EOTC/TXTFC → 关 SPI
 */
static uint8_t S_SpiTransfer(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    uint16_t tx_cnt = len;
    uint16_t rx_cnt = len;
    uint16_t tx_idx = 0U;
    uint16_t rx_idx = 0U;
    uint32_t sr;
    uint32_t guard;

    /* 清残留标志(EOT/TXTF/OVR/UDR/MODF)，防上一帧异常污染本帧 */
    SPI3->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_OVRC |
                 SPI_IFCR_UDRC | SPI_IFCR_MODFC;

    MODIFY_REG(SPI3->CR2, SPI_CR2_TSIZE, (uint32_t)len);
    SET_BIT(SPI3->CR1, SPI_CR1_SPE);
    SET_BIT(SPI3->CR1, SPI_CR1_CSTART);

    guard = 0U;
    while ((tx_cnt > 0U) || (rx_cnt > 0U))
    {
        if ((tx_cnt > 0U) && (0U != (SPI3->SR & SPI_SR_TXP)) &&
            (rx_cnt < (tx_cnt + TMC5160_SPI_FIFO_DEPTH)))
        {
            *(__IO uint8_t *)&SPI3->TXDR = tx[tx_idx];
            tx_idx++;
            tx_cnt--;
        }
        if ((rx_cnt > 0U) && (0U != (SPI3->SR & SPI_SR_RXP)))
        {
            rx[rx_idx] = *(__IO uint8_t *)&SPI3->RXDR;
            rx_idx++;
            rx_cnt--;
        }
        if (++guard > TMC5160_SPI_POLL_GUARD)
        {
            CLEAR_BIT(SPI3->CR1, SPI_CR1_SPE);
            return TMC5160_ERR;
        }
    }

    /* 等发送/RX 全完成后 EOT */
    guard = 0U;
    while (0U == (SPI3->SR & SPI_SR_EOT))
    {
        if (++guard > TMC5160_SPI_POLL_GUARD)
        {
            CLEAR_BIT(SPI3->CR1, SPI_CR1_SPE);
            return TMC5160_ERR;
        }
    }
    sr = SPI3->SR;
    SPI3->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_OVRC |
                 SPI_IFCR_UDRC | SPI_IFCR_MODFC;
    CLEAR_BIT(SPI3->CR1, SPI_CR1_SPE);

    /* 过速/欠速错误等同 HAL 的 ErrorCode 判定 */
    if (0U != (sr & (SPI_SR_OVR | SPI_SR_UDR)))
    {
        return TMC5160_ERR;
    }
    return TMC5160_OK;
}

/**
 * @输入 chip: 芯片编号 TMC5160_CHIP_1/TMC5160_CHIP_2
 * @输出 无
 * @说明 拉低对应芯片 SPI 片选引脚（寄存器级 BSRR 原子写，替代 HAL_GPIO_WritePin）
 * @注意 BSRR 高 16 位写 1 = 复位对应引脚(拉低)，无读-改-写、单次存储即原子
 * 依据 RM0433(STM32H7) GPIO: GPIOx_BSRR bits[15:0] 置位 / bits[31:16] 复位
 */
void TMC5160_SelectChip(uint8_t chip)
{
    if (TMC5160_CHIP_1 == chip)
    {
        U1_SPI_SCN_GPIO_Port->BSRR = (uint32_t)U1_SPI_SCN_Pin << 16U;
    }
    else if (TMC5160_CHIP_2 == chip)
    {
        U2_SPI_SCN_GPIO_Port->BSRR = (uint32_t)U2_SPI_SCN_Pin << 16U;
    }
}

/**
 * @输入 chip: 芯片编号
 * @输出 无
 * @说明 拉高对应芯片 SPI 片选引脚（寄存器级 BSRR 原子写）
 * @注意 BSRR 低 16 位写 1 = 置位对应引脚(拉高)
 * 依据 RM0433(STM32H7) GPIO: GPIOx_BSRR bits[15:0] 置位
 */
void TMC5160_DeselectChip(uint8_t chip)
{
    if (TMC5160_CHIP_1 == chip)
    {
        U1_SPI_SCN_GPIO_Port->BSRR = (uint32_t)U1_SPI_SCN_Pin;
    }
    else if (TMC5160_CHIP_2 == chip)
    {
        U2_SPI_SCN_GPIO_Port->BSRR = (uint32_t)U2_SPI_SCN_Pin;
    }
}

/**
 * @输入 chip: 芯片编号; mode: 模式(1/2/3，保留兼容)
 * @输出 无
 * @说明 空实现——H7 板 TMC5160 SD_MODE/SPI_MODE 引脚硬件硬接线为 SPI 模式，
 *       无模式切换 GPIO（源 F407 板为软件可控，移植时删除）
 */
void TMC5160_SetMode(uint8_t chip, uint8_t mode)
{
    (void)chip;
    (void)mode;
}

/**
 * @输入 chip: 芯片编号
 * @输出 无
 * @说明 拉低 ENN 引脚使能电机驱动
 */
void TMC5160_Enable(uint8_t chip)
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
 * @说明 拉高 ENN 引脚禁用电机驱动
 */
void TMC5160_Disable(uint8_t chip)
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
void TMC5160_DelayMs(uint32_t ms)
{
    HAL_Delay(ms);
}

/**
 * @输入 chip: 芯片编号; reg_addr: 寄存器地址(7bit); data: 32 位数据
 * @输出 TMC5160_OK / TMC5160_ERR
 * @说明 通过 SPI 写入 TMC5160 寄存器
 *   SPI 数据报(40-bit, 5字节): Byte0=bit7(1写)+bit6-0地址, Byte1-4=数据
 * @注意 寄存器级收发(S_SpiTransfer)，配置仍由 CubeMX HAL 提供
 * 依据 TMC5160A_Datasheet_Rev1.14_ch04_4_spi_interface.md 第4章:
 *   SPI DATAGRAM 格式; 写访问地址加 0x80 (ch06.p031)
 */
uint8_t TMC5160_SpiWrite(uint8_t chip, uint8_t reg_addr, uint32_t data)
{
    static uint8_t tx_buf[5];
    static uint8_t rx_buf[5];
    uint8_t ret;

    tx_buf[0] = reg_addr | 0x80;
    tx_buf[1] = (data >> 24) & 0xFF;
    tx_buf[2] = (data >> 16) & 0xFF;
    tx_buf[3] = (data >> 8) & 0xFF;
    tx_buf[4] = data & 0xFF;

    TEST_TIM_Start(0);   /* tag0=写事务全窗(片选+收发) */
    TMC5160_SelectChip(chip);
    ret = S_SpiTransfer(tx_buf, rx_buf, 5U);
    TMC5160_DeselectChip(chip);
    TEST_TIM_Stop(0);

    /* CS 拉高间隔: 保证背靠背写帧之间 tCSH 达标
     * 依据 TMC5160A_Datasheet_Rev1.14_ch04_4_spi_interface.md §4.3 表:
     *   tCSH > 2*tCLK + 10ns; SCK=6.45MHz → tCLK=155ns → 2*155+10=320ns 最低要求,
     *   取 5us ≈ 15x 裕量(与 ReadReg 两报间隔同值) */
    S_DelayUs(TMC5160_CS_HIGH_US);

    return ret;
}

/**
 * @输入 chip: 芯片编号; reg_addr: 寄存器地址(7bit)
 * @输出 32 位寄存器值，失败返回 0xFFFFFFFF
 * @说明 通过 SPI 读取 TMC5160 寄存器，需两报：第一报丢弃旧数据，第二报取新数据
 * @注意 两报之间 CS 拉高需满足 tCSH > 2*tCLK+10ns (datasheet ch04 §4.3 表,
 *       SCK=4MHz → 510ns), 取 10us ≈ 20x 裕量
 * 依据 TMC5160A_Datasheet_Rev1.14_ch04_4_spi_interface.md 第4章:
 *   SPI DATAGRAM 格式
 */
uint32_t TMC5160_SpiRead(uint8_t chip, uint8_t reg_addr)
{
    static uint8_t tx_buf[5];
    static uint8_t rx_buf[5];
    uint8_t ret;

    tx_buf[0] = reg_addr & 0x7F;
    tx_buf[1] = 0x00;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;
    tx_buf[4] = 0x00;

    /* 第一报：丢弃旧数据 */
    TEST_TIM_Start(1);   /* tag1=读双报总窗(含 10us 帧间隔) */
    TMC5160_SelectChip(chip);
    ret = S_SpiTransfer(tx_buf, rx_buf, 5U);
    TMC5160_DeselectChip(chip);
    if (TMC5160_OK != ret)
    {
        return 0xFFFFFFFF;
    }

    /* CS 高电平间隔: 5us (tCSH > 2*tCLK+10ns=320ns @6.45MHz, 取 ~15× 裕量) */
    S_DelayUs(TMC5160_CS_HIGH_US);

    /* 第二报：获取本次数据 */
    TMC5160_SelectChip(chip);
    ret = S_SpiTransfer(tx_buf, rx_buf, 5U);
    TMC5160_DeselectChip(chip);
    TEST_TIM_Stop(1);
    if (TMC5160_OK != ret)
    {
        return 0xFFFFFFFF;
    }

    return ((uint32_t)rx_buf[1] << 24) |
           ((uint32_t)rx_buf[2] << 16) |
           ((uint32_t)rx_buf[3] << 8) |
           (uint32_t)rx_buf[4];
}

/* ==== 调试: 原始收发字节回显 (HAL 版, 供 comm_test 打印) ==== */
uint8_t TMC5160_DebugTransfer(uint8_t chip, uint8_t *tx, uint8_t *rx, uint8_t len)
{
    uint8_t ret;

    if (NULL == tx || NULL == rx || 5 != len)
    {
        return 1;
    }
    TMC5160_SelectChip(chip);
    ret = S_SpiTransfer(tx, rx, 5U);
    TMC5160_DeselectChip(chip);
    return ret;
}

/* ==== 芯片功能封装 ==== */

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
        TMC5160_Enable(chip->chip_number);
        TMC5160_DelayMs(10);
    }
}

/**
 * @输入 chip: 芯片编号(1/2)
 * @输出 无
 * @说明 急停：拉高 ENN 引脚关断功率级，轴自由停车（不锁轴）；
 *        置锁存，后续运动命令自动重使能；位置已丢失须重回零
 */
void TMC5160_EStop(uint8_t chip)
{
    if (TMC5160_CHIP_1 != chip && TMC5160_CHIP_2 != chip)
    {
        return;
    }
    TMC5160_Disable(chip);
    s_estop[chip - 1U] = 1;
}

/**
 * @输入 无
 * @输出 无
 * @说明 初始化两片 TMC5160：默认配置/清错/基础寄存器/编码器/电流/使能
 * @注意 模式固定 SPI 模式（H7 板模式引脚硬接线），closed_loop 默认关
 * 依据 TMC5160A_Datasheet_Rev1.14.ch06.p032.md: GCONF/COOLCONF/TPWMTHRS  2026-08-24
 * 依据 TMC5160A_Datasheet_Rev1.14.ch06.p048.md: CHOPCONF  2026-08-24
 * 依据 TMC5160A_Datasheet_Rev1.14.ch03.p017.md: DRV_CONF.DRVSTRENGTH  2026-08-24
 * 依据 TMC5160A_Datasheet_Rev1.14.ch06.p035.md: SHORT_CONF  2026-09-10
 * 依据 TMC5160A_Datasheet_Rev1.14.ch18_18_sine_wave_look.md: PWMCONF  2026-08-24
 * 依据 TMC5160A_Datasheet_Rev1.14.ch06.p038.md: IHOLD_IRUN/TPOWERDOWN/TCOOLTHRS  2026-09-10
 * 依据 TMC5160A_Datasheet_Rev1.14.ch06.p045.md: ENCMODE/ENC_CONST  2026-09-01
 * 依据 TMC5160A_Datasheet_Rev1.14.ch06.p006.md: 使能时序(ENN)  2026-08-24
 */
void TMC5160_Init(void)
{
    uint8_t i;
    uint32_t gstat;
    TMC5160_CHIP_T *chips[2] = { &g_tmc5160_chip1_st, &g_tmc5160_chip2_st };

    /* 等待 TMC5160 上电稳定后再操作 SPI */
    TMC5160_DelayMs(50);

    for (i = 0; i < 2; i++)
    {
        TMC5160_CHIP_T *chip = chips[i];
        chip->chip_number = i + 1;

        /* 固定默认配置（RAM 常驻，上电初始化写入） */
        chip->mode = TMC5160_MODE_POSITION;
        chip->closed_loop = 0;

        /* 上电保持 ENN 高(禁用)，写配置后再使能，避免瞬时过流 */

        /* 清除 Power-on 残留错误，同时验证 SPI 通信 */
        TMC5160_WriteReg(chip, REG_GSTAT, 0x07);
        TMC5160_DelayMs(1);
        gstat = TMC5160_ReadReg(chip, REG_GSTAT);
        if (0xFFFFFFFF == gstat)
        {
            /* SPI 通信异常，重试一次 */
            TMC5160_DelayMs(10);
            TMC5160_WriteReg(chip, REG_GSTAT, 0x07);
            TMC5160_DelayMs(1);
            gstat = TMC5160_ReadReg(chip, REG_GSTAT);
        }

        /* GCONF: en_pwm_mode=0, 全程 SpreadCycle */
        TMC5160_WriteReg(chip, REG_GCONF, 0x00);

        /* CHOPCONF: TOFF=5, TBL=54clk, MRES=256微步 */
        TMC5160_WriteReg(chip, REG_CHOPCONF, 0x000181C5);

        /* 驱动配置: DRVSTRENGTH=weak(00)（AOD4126 Qgd=10nC） */
        TMC5160_WriteReg(chip, REG_DRV_CONF, 0x00000400);

        /* SHORT_CONF: 短路检测灵敏度最低 */
        TMC5160_WriteReg(chip, REG_SHORT_CONF, 0x0007030F);

        /* PWMCONF: 斩波频率 43.9kHz + PWM 自动调校 */
        TMC5160_WriteReg(chip, REG_PWMCONF, 0xC40D001E);

        /* 编码器配置 */
        TMC5160_ConfigEncoder(chip);

        /* 电机电流: IHOLDDELAY=8 / IRUN=20(3.02A RMS) / IHOLD=6(1.01A) */
        TMC5160_WriteReg(chip, REG_IHOLD_IRUN, (8 << 16) | (20 << 8) | 6);

        /* 静止降流延迟: 40 → 约 699ms */
        TMC5160_WriteReg(chip, REG_TPOWERDOWN, 40);

        /* CoolStep 关闭 */
        TMC5160_WriteReg(chip, REG_COOLCONF, 0x0000);

        /* CoolStep 速度窗口: 0 = 关闭 */
        TMC5160_WriteReg(chip, REG_TCOOLTHRS, 0);

        /* TPWMTHRS=0（本模式不参与斩波切换） */
        TMC5160_WriteReg(chip, REG_TPWMTHRS, 0);

        /* 使能时序: 配置写完后延时稳定, 再拉低 ENN 引脚使能, 随后延时等待校准
         * 避免上电瞬时过流(当前/斩波参数已就绪时才导通功率级) */
        TMC5160_DelayMs(10);
        TMC5160_Enable(chip->chip_number);
        TMC5160_DelayMs(10);
    }

    /* 等待斩波校准稳定 */
    TMC5160_DelayMs(100);

    /* 记录编码器上电初始值，后续读数减去此偏移归零 */
    s_enc_offset[0] = (int32_t)TMC5160_ReadReg(&g_tmc5160_chip1_st, REG_X_ENC);
    s_enc_offset[1] = (int32_t)TMC5160_ReadReg(&g_tmc5160_chip2_st, REG_X_ENC);
}

/* ==== 寄存器读写 ==== */

/**
 * @输入 chip: 芯片指针; reg_addr: 寄存器地址; data: 数据
 * @输出 TMC5160_SUCCESS / TMC5160_FAIL
 * @说明 写寄存器（转发 drv 层）
 */
uint8_t TMC5160_WriteReg(TMC5160_CHIP_T *chip, uint8_t reg_addr, uint32_t data)
{
    return TMC5160_SpiWrite(chip->chip_number, reg_addr, data);
}

/**
 * @输入 chip: 芯片指针; reg_addr: 寄存器地址
 * @输出 寄存器值，失败 0xFFFFFFFF
 * @说明 读寄存器（转发 drv 层）
 */
uint32_t TMC5160_ReadReg(TMC5160_CHIP_T *chip, uint8_t reg_addr)
{
    return TMC5160_SpiRead(chip->chip_number, reg_addr);
}

/**
 * @输入 chip: 芯片指针; profile_id: 运动参数组 ID(1~5)
 * @输出 无
 * @说明 按预配置运动参数组设置斜坡寄存器
 * 依据 TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md:
 *   VSTART/A1/V1/AMAX/VMAX/DMAX/D1/VSTOP/TZEROWAIT
 */
void TMC5160_ApplyProfile(TMC5160_CHIP_T *chip, uint8_t profile_id)
{
    const TMC5160_PROFILE_T *p;

    if (0 == profile_id || TMC5160_PROFILE_COUNT < profile_id)
    {
        profile_id = 1;
    }
    p = &s_profiles[profile_id - 1];

    TMC5160_WriteReg(chip, REG_VSTART, p->vstart);
    TMC5160_WriteReg(chip, REG_VSTOP, p->vstop);
    TMC5160_WriteReg(chip, REG_V1, p->v1);
    TMC5160_WriteReg(chip, REG_A1, p->a1);
    TMC5160_WriteReg(chip, REG_AMAX, p->amax);
    TMC5160_WriteReg(chip, REG_VMAX, p->vmax);
    TMC5160_WriteReg(chip, REG_DMAX, p->dmax);
    TMC5160_WriteReg(chip, REG_D1, p->d1);
    TMC5160_WriteReg(chip, REG_TZEROWAIT, p->tzerowait);
}

/* ==== 运动控制 ==== */

/**
 * @输入 chip: 芯片指针; target: 目标绝对位置
 * @输出 无
 * @说明 绝对定位，RAMPMODE=0 位置模式
 * 依据 TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md:
 *   RAMPMODE/XTARGET
 */
void TMC5160_MoveTo(TMC5160_CHIP_T *chip, int32_t target)
{
    S_RecoverIfEStop(chip);
    TMC5160_WriteReg(chip, REG_RAMPMODE, 0);
    TMC5160_WriteReg(chip, REG_XTARGET, (uint32_t)target);
}

/**
 * @输入 chip: 芯片指针; offset: 相对偏移量(+正转, -反转)
 * @输出 无
 * @说明 从当前位置运动指定偏移量
 */
void TMC5160_MoveBy(TMC5160_CHIP_T *chip, int32_t offset)
{
    int32_t current = TMC5160_GetPosition(chip);

    TMC5160_MoveTo(chip, current + offset);
}

/**
 * @输入 chip: 芯片指针; velocity: 目标速度(+正转, -反转)
 * @输出 无
 * @说明 速度模式持续旋转
 * 依据 TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md:
 *   RAMPMODE 速度模式
 */
void TMC5160_SetVelocity(TMC5160_CHIP_T *chip, int32_t velocity)
{
    S_RecoverIfEStop(chip);
    if (0 <= velocity)
    {
        TMC5160_WriteReg(chip, REG_RAMPMODE, 1);
        TMC5160_WriteReg(chip, REG_VMAX, (uint32_t)velocity);
    }
    else
    {
        TMC5160_WriteReg(chip, REG_RAMPMODE, 2);
        TMC5160_WriteReg(chip, REG_VMAX, (uint32_t)(-velocity));
    }
}

/**
 * @输入 chip: 芯片指针
 * @输出 无
 * @说明 立即停止，切回定位模式保持锁轴
 */
void TMC5160_Stop(TMC5160_CHIP_T *chip)
{
    TMC5160_WriteReg(chip, REG_VMAX, 0);
    TMC5160_WriteReg(chip, REG_RAMPMODE, 0);
}

/* ==== 位置读取 ==== */

/**
 * @输入 chip: 芯片指针
 * @输出 int32_t: 当前位置(XACTUAL)
 * 依据 TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md: XACTUAL
 */
int32_t TMC5160_GetPosition(TMC5160_CHIP_T *chip)
{
    return (int32_t)TMC5160_ReadReg(chip, REG_XACTUAL);
}

/**
 * @输入 chip: 芯片指针
 * @输出 int32_t: VACTUAL 当前实际速度（有符号，负=反转）
 * @依据 TMC5160A_Datasheet_Rev1.14.ch05.p038.md:
 *   R 0x22 20 VACTUAL（斜坡发生器实时速度）
 */
int32_t TMC5160_GetVelocity(TMC5160_CHIP_T *chip)
{
    return (int32_t)TMC5160_ReadReg(chip, 0x22);
}

/**
 * @输入 chip: 芯片指针
 * @输出 uint32_t: RAMP_STAT 寄存器值
 * 依据 TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md: RAMP_STAT
 */
uint32_t TMC5160_GetRampStat(TMC5160_CHIP_T *chip)
{
    return TMC5160_ReadReg(chip, REG_RAMP_STAT);
}

/**
 * @输入 chip: 芯片指针
 * @输出 uint32_t: DRVSTATUS 寄存器值
 * 依据 TMC5160A_Datasheet_Rev1.14_ch11_11_diagnostics_and_protection.md
 */
uint32_t TMC5160_GetDrvStatus(TMC5160_CHIP_T *chip)
{
    return TMC5160_ReadReg(chip, REG_DRVSTATUS);
}

/**
 * @输入 chip: 芯片指针
 * @输出 uint32_t: GSTAT 寄存器值（reset/drv_err/uv_cp）
 * 依据 TMC5160A_Datasheet_Rev1.14.ch06.p033.md: GSTAT
 */
uint32_t TMC5160_GetGStat(TMC5160_CHIP_T *chip)
{
    return TMC5160_ReadReg(chip, REG_GSTAT);
}

/* ==== 状态标志 ==== */

/**
 * @输入 chip: 芯片指针
 * @输出 uint8_t 状态标志位
 *   bit0=到位, bit1=失步, bit2=过温, bit3=驱动错误, bit4=SPI通讯异常
 * 依据 TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md: RAMP_STAT
 * 依据 TMC5160A_Datasheet_Rev1.14.ch06.p045.md: ENC_STATUS
 * 依据 TMC5160A_Datasheet_Rev1.14.ch06.p033.md: GSTAT/DRVSTATUS
 */
uint8_t TMC5160_GetStatusFlags(TMC5160_CHIP_T *chip)
{
    uint32_t ramp_stat, drv_status, gstat, enc_stat;
    uint8_t flags = 0;

    ramp_stat = TMC5160_ReadReg(chip, REG_RAMP_STAT);
    drv_status = TMC5160_ReadReg(chip, REG_DRVSTATUS);
    gstat = TMC5160_ReadReg(chip, REG_GSTAT);
    enc_stat = TMC5160_ReadReg(chip, REG_ENC_STATUS);

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
        TMC5160_WriteReg(chip, REG_RAMP_STAT, (1UL << 9));
    }
    /* bit1: 失步 - ENC_STATUS.bit1 (deviation_warn) */
    if (enc_stat & (1UL << 1))
    {
        flags |= 0x02;
        TMC5160_WriteReg(chip, REG_ENC_STATUS, (1UL << 1));
    }
    /* bit2: 过温 - DRVSTATUS.bit26/25 */
    if (drv_status & (3UL << 25))
    {
        flags |= 0x04;
    }
    /* bit3: 驱动错误 - GSTAT.bit1 (drv_err) */
    if (gstat & 0x02)
    {
        flags |= 0x08;
        TMC5160_WriteReg(chip, REG_GSTAT, 0x02);
    }

    return flags;
}

/**
 * @输入 chip: 芯片指针
 * @输出 uint8_t: 运动阶段标志
 *   bit0=加速, bit1=匀速, bit2=减速, bit3=归零等待, bit4=静止锁轴
 * 依据 TMC5160A_Datasheet_Rev1.14.ch12_12_ramp_generator.md: RAMP_STAT
 */
uint8_t TMC5160_GetMotionPhase(TMC5160_CHIP_T *chip)
{
    uint32_t ramp_stat;
    uint8_t phase = 0;

    ramp_stat = TMC5160_ReadReg(chip, REG_RAMP_STAT);

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
 * 依据 TMC5160A_Datasheet_Rev1.14.ch20_20_abn_incremental_encoder.md:
 *   ENCMODE/ENC_CONST/ENC_DEVIATION
 */
void TMC5160_ConfigEncoder(TMC5160_CHIP_T *chip)
{
    TMC5160_WriteReg(chip, REG_ENCMODE, 0x00);
    TMC5160_WriteReg(chip, REG_ENC_CONST, 0xFFF33333);
    TMC5160_WriteReg(chip, REG_ENC_DEVIATION, TMC5160_ENC_TOLERANCE);
}

/**
 * @输入 chip: 芯片指针
 * @输出 int32_t: 编码器当前位姿(X_ENC 归零后)
 */
int32_t TMC5160_GetEncoderPosition(TMC5160_CHIP_T *chip)
{
    int32_t raw = (int32_t)TMC5160_ReadReg(chip, REG_X_ENC);
    int32_t idx = (TMC5160_CHIP_1 == chip->chip_number) ? 0 : 1;
    return raw - s_enc_offset[idx];
}

/**
 * @输入 chip: 芯片指针
 * @输出 uint32_t: ENC_STATUS 寄存器值
 */
uint32_t TMC5160_GetEncoderStatus(TMC5160_CHIP_T *chip)
{
    return TMC5160_ReadReg(chip, REG_ENC_STATUS);
}

/**
 * @输入 chip: 芯片指针
 * @输出 int32_t: 编码器偏差绝对值 (X_ENC - XACTUAL)
 */
int32_t TMC5160_GetEncoderDeviation(TMC5160_CHIP_T *chip)
{
    int32_t enc_pos = TMC5160_GetEncoderPosition(chip);
    int32_t x_actual = TMC5160_GetPosition(chip);
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
uint8_t TMC5160_CheckPosition(TMC5160_CHIP_T *chip, int32_t expected_steps)
{
    int32_t enc_before, enc_after;
    int32_t actual_delta, error;

    enc_before = TMC5160_GetEncoderPosition(chip);
    TMC5160_DelayMs(10);
    enc_after = TMC5160_GetEncoderPosition(chip);

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
TMC5160_MOVE_RESULT_T TMC5160_WaitPosition(TMC5160_CHIP_T *chip, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    uint32_t ramp_stat;

    while (elapsed < timeout_ms)
    {
        ramp_stat = TMC5160_GetRampStat(chip);

        if (0xFFFFFFFF == ramp_stat || 0 == ramp_stat)
        {
            return TMC5160_MOVE_SPI_ERROR;
        }

        if (ramp_stat & (1UL << 9))
        {
            return TMC5160_MOVE_OK;
        }

        TMC5160_DelayMs(5);
        elapsed += 5;
    }

    return TMC5160_MOVE_TIMEOUT;
}

/**
 * @输入 chip: 芯片指针; target: 目标绝对位置
 * @输出 TMC5160_MOVE_RESULT_T
 * @说明 执行位置运动并验证编码器精度，偏差超限自动重试
 */
TMC5160_MOVE_RESULT_T TMC5160_MoveToWithVerify(TMC5160_CHIP_T *chip, int32_t target)
{
    TMC5160_MOVE_RESULT_T result;
    int32_t current_pos, move_delta;
    int32_t enc_before, enc_after;
    int32_t deviation;
    uint8_t retry;

    current_pos = TMC5160_GetPosition(chip);
    move_delta = target - current_pos;

    for (retry = 0; retry < TMC5160_MAX_RETRY; retry++)
    {
        /* 清除残留错误 */
        TMC5160_WriteReg(chip, REG_GSTAT, 0x07);

        enc_before = TMC5160_GetEncoderPosition(chip);

        TMC5160_MoveTo(chip, target);

        result = TMC5160_WaitPosition(chip, TMC5160_MOVE_TIMEOUT_MS);
        if (TMC5160_MOVE_OK != result)
        {
            return result;
        }

        enc_after = TMC5160_GetEncoderPosition(chip);

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
        current_pos = TMC5160_GetEncoderPosition(chip);
        target = current_pos + move_delta;
    }

    return TMC5160_MOVE_DEVIATION;
}

/* ==== 回零基础控制（StallGuard2 芯片级原语；状态机/时序在 app/homing） ==== */

/* SGT=0 起始值(ch13 §13.1, 阈值待真机交互调定, 待实测确认) / sfilt=0 非滤波
 * (ch13 §13.3) / SEMIN=0 CoolStep 关（仅用 StallGuard 堵转检测） */
#define TMC5160_HOME_COOLCONF      0x00000000UL
/* TCOOLTHRS=200：TSTEP=2^24/VMAX≈146 → 取 200 略高于
 * （ch13 §13.1 + ch05.p038 TCOOLTHRS≥TSTEP），SG 在≥约 1.46RPS 生效
 * 依据 .cl/memory/config.md home_tcoolthrs=200 */
#define TMC5160_HOME_TCOOLTHRS     200UL
/* AMAX=20000（与参数组 4 同量级）：a=20000×fCLK²/2^41≈2.05M µsteps/s²
 * 依据 .cl/memory/config.md home_amax=20000 */
#define TMC5160_HOME_AMAX          20000UL

/* 依据 TMC5160A_Datasheet_Rev1.14.ch06.p044.md:
 *   RAMP_STAT event_stop_sg=bit6(R+WC，写 1 清) */
#define TMC5160_RAMP_EVENT_STOP_SG (1UL << 6)
/* 依据 TMC5160A_Datasheet_Rev1.14.ch06.p043.md:
 *   SW_MODE sg_stop=bit10 */
#define TMC5160_SW_MODE_SG_STOP    (1UL << 10)
/* 依据 TMC5160A_Datasheet_Rev1.14.ch06.p056.md:
 *   DRVSTATUS SG_RESULT=bits[9:0]（0=最大负载/堵转） */
#define TMC5160_SG_RESULT_MASK     0x3FFUL

/* 回零单实例（CAN 0x09 仅单电机）→ 现场保存单组即可 */
static uint32_t s_home_sw_mode_saved = 0;
static uint32_t s_home_tcoolthrs_saved = 0;

/**
 * @输入 chip: 芯片指针
 * @输出 无
 * @说明 配置 StallGuard2 回零检测：保存现场→写 COOLCONF/TCOOLTHRS/AMAX
 *        →置 SW_MODE.sg_stop；须在速度模式撞限位前调用
 * @注意 修改全局寄存器配置，配对 TMC5160_HomeRestore 恢复现场
 */
void TMC5160_HomeConfig(TMC5160_CHIP_T *chip)
{
    uint32_t sw;

    s_home_sw_mode_saved = TMC5160_ReadReg(chip, REG_SW_MODE);
    s_home_tcoolthrs_saved = TMC5160_ReadReg(chip, REG_TCOOLTHRS);
    TMC5160_WriteReg(chip, REG_COOLCONF, TMC5160_HOME_COOLCONF);
    TMC5160_WriteReg(chip, REG_TCOOLTHRS, TMC5160_HOME_TCOOLTHRS);
    TMC5160_WriteReg(chip, REG_AMAX, TMC5160_HOME_AMAX);
    sw = s_home_sw_mode_saved | TMC5160_SW_MODE_SG_STOP;
    TMC5160_WriteReg(chip, REG_SW_MODE, sw);
}

/**
 * @输入 chip: 芯片指针
 * @输出 无
 * @说明 恢复回零前寄存器现场（SW_MODE/TCOOLTHRS）
 */
void TMC5160_HomeRestore(TMC5160_CHIP_T *chip)
{
    TMC5160_WriteReg(chip, REG_SW_MODE, s_home_sw_mode_saved);
    TMC5160_WriteReg(chip, REG_TCOOLTHRS, s_home_tcoolthrs_saved);
}

/**
 * @输入 chip: 芯片指针
 * @输出 1=硬件 stall 停已触发（读后清），0=未触发
 * @说明 读并清 RAMP_STAT.event_stop_sg（sg_stop 硬件兜底）
 */
uint8_t TMC5160_HomeCheckStall(TMC5160_CHIP_T *chip)
{
    uint32_t rs = TMC5160_ReadReg(chip, REG_RAMP_STAT);

    if (0UL != (rs & TMC5160_RAMP_EVENT_STOP_SG))
    {
        TMC5160_WriteReg(chip, REG_RAMP_STAT, TMC5160_RAMP_EVENT_STOP_SG);
        return 1;
    }
    return 0;
}

/**
 * @输入 chip: 芯片指针
 * @输出 uint16_t: SG_RESULT[9:0]（0=最大负载/堵转）
 * @说明 读 StallGuard2 负载测量值（软件主判据）
 */
uint16_t TMC5160_HomeGetSg(TMC5160_CHIP_T *chip)
{
    return (uint16_t)(TMC5160_GetDrvStatus(chip) & TMC5160_SG_RESULT_MASK);
}

/**
 * @输入 chip: 芯片指针
 * @输出 无
 * @说明 标定零点：XACTUAL 写 0（ch06.p040：homing 时允许改 XACTUAL）
 */
void TMC5160_HomeZero(TMC5160_CHIP_T *chip)
{
    TMC5160_WriteReg(chip, REG_XACTUAL, 0UL);
}
