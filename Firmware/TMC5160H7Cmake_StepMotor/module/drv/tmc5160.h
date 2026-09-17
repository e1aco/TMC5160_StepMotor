/*****************************************************************************
 * @文件: tmc5160.h
 * @作者: cl
 * @日期: 2026-09-09
 * @版本: v1.0
 * @说明: TMC5160 驱动层统一头 = 原 tmc5160_drv.h + tmc5160_usr.h 文件合并
 *        （用户 2026-09-09 裁决：drv 允许芯片功能完整封装）
 * @来源: 自 TMC5160_StepMotor(F407) 移植归一
 * @平台: STM32H750VBT6 (SPI3, 模式3, 6.45Mbit/s; fCLK=15MHz 外部时钟)
 * @依赖: HAL_SPI, HAL_GPIO
 ****************************************************************************/
#ifndef TMC5160_H
#define TMC5160_H

#include <stdint.h>

/* ==== 芯片编号 ==== */
#define TMC5160_CHIP_1   1
#define TMC5160_CHIP_2   2

/* ==== SPI 读写返回码 ==== */
#define TMC5160_OK       0
#define TMC5160_ERR      1

/* ==== 返回码 ==== */
#define TMC5160_SUCCESS  0
#define TMC5160_FAIL     1

/* ==== 运行模式（保留枚举兼容，H7 板模式引脚硬接线为 SPI 模式） ==== */
#define TMC5160_MODE_POSITION  1   /* 位置式步进(SPI 完全控制) */
#define TMC5160_MODE_RAMP      2   /* S 斜坡步进+方向(standalone) */
#define TMC5160_MODE_STEP      3   /* 简单步进+方向(standalone) */

/* ==== 运动结果 ==== */
typedef enum {
    TMC5160_MOVE_OK = 0,
    TMC5160_MOVE_TIMEOUT,
    TMC5160_MOVE_DEVIATION,
    TMC5160_MOVE_SPI_ERROR
} TMC5160_MOVE_RESULT_T;

/* ==== 芯片状态 ==== */
typedef struct {
    uint8_t chip_number;
    uint8_t mode;
    uint8_t closed_loop;
} TMC5160_CHIP_T;

/* ==== 运动参数组 ==== */
typedef struct {
    uint32_t vstart;
    uint32_t vstop;
    uint32_t v1;
    uint32_t a1;
    uint32_t amax;
    uint32_t vmax;
    uint32_t d1;
    uint32_t dmax;
    uint32_t tzerowait;
} TMC5160_PROFILE_T;

#define TMC5160_PROFILE_COUNT  5

/* ==== 常量 ==== */
#define TMC5160_ENC_TOLERANCE    256
#define TMC5160_MOVE_TIMEOUT_MS  5000
#define TMC5160_MAX_RETRY        3

/* ==== 回零(StallGuard2 堵转碰硬限位，无编码器) ==== */
#define TMC5160_HOME_DIR_NEGATIVE  0   /* 向负方向（XACTUAL 减小） */
#define TMC5160_HOME_DIR_POSITIVE  1   /* 向正方向（XACTUAL 增大） */
/* 回零速度 2RPS（ch13 §13.4 推荐 1~5RPS）：VMAX=V×2^24/fCLK=102400×2^24/15e6≈114532
 * 依据 .cl/memory/config.md home_vmax=114532 */
#define TMC5160_HOME_VMAX          114532UL

/* ==== 全局实例 ==== */
extern TMC5160_CHIP_T g_tmc5160_chip1_st;
extern TMC5160_CHIP_T g_tmc5160_chip2_st;

/* ==== 接口: SPI/GPIO 原语 ==== */
void     TMC5160_SelectChip(uint8_t chip);
void     TMC5160_DeselectChip(uint8_t chip);
void     TMC5160_SetMode(uint8_t chip, uint8_t mode);
void     TMC5160_Enable(uint8_t chip);
void     TMC5160_Disable(uint8_t chip);
void     TMC5160_DelayMs(uint32_t ms);
uint8_t  TMC5160_SpiWrite(uint8_t chip, uint8_t reg_addr, uint32_t data);
uint32_t TMC5160_SpiRead(uint8_t chip, uint8_t reg_addr);
uint8_t  TMC5160_DebugTransfer(uint8_t chip, uint8_t *tx, uint8_t *rx, uint8_t len);

/* ==== 接口: 芯片功能封装 ==== */
void    TMC5160_Init(void);
void    TMC5160_EStop(uint8_t chip);
uint8_t TMC5160_WriteReg(TMC5160_CHIP_T *chip, uint8_t reg_addr, uint32_t data);
uint32_t TMC5160_ReadReg(TMC5160_CHIP_T *chip, uint8_t reg_addr);

void    TMC5160_ApplyProfile(TMC5160_CHIP_T *chip, uint8_t profile_id);
void    TMC5160_MoveTo(TMC5160_CHIP_T *chip, int32_t target);
void    TMC5160_MoveBy(TMC5160_CHIP_T *chip, int32_t offset);
void    TMC5160_SetVelocity(TMC5160_CHIP_T *chip, int32_t velocity);
void    TMC5160_Stop(TMC5160_CHIP_T *chip);

int32_t TMC5160_GetPosition(TMC5160_CHIP_T *chip);
int32_t TMC5160_GetVelocity(TMC5160_CHIP_T *chip);
uint32_t TMC5160_GetRampStat(TMC5160_CHIP_T *chip);
uint32_t TMC5160_GetDrvStatus(TMC5160_CHIP_T *chip);
uint32_t TMC5160_GetGStat(TMC5160_CHIP_T *chip);

uint8_t TMC5160_GetStatusFlags(TMC5160_CHIP_T *chip);
uint8_t TMC5160_GetMotionPhase(TMC5160_CHIP_T *chip);

void    TMC5160_ConfigEncoder(TMC5160_CHIP_T *chip);
int32_t TMC5160_GetEncoderPosition(TMC5160_CHIP_T *chip);
uint32_t TMC5160_GetEncoderStatus(TMC5160_CHIP_T *chip);
int32_t TMC5160_GetEncoderDeviation(TMC5160_CHIP_T *chip);
uint8_t TMC5160_CheckPosition(TMC5160_CHIP_T *chip, int32_t expected_steps);

TMC5160_MOVE_RESULT_T TMC5160_WaitPosition(TMC5160_CHIP_T *chip, uint32_t timeout_ms);
TMC5160_MOVE_RESULT_T TMC5160_MoveToWithVerify(TMC5160_CHIP_T *chip, int32_t target);

/* ==== 接口: 回零基础控制（StallGuard2 芯片级原语；状态机/时序在 app/homing） ==== */
void     TMC5160_HomeConfig(TMC5160_CHIP_T *chip);
void     TMC5160_HomeRestore(TMC5160_CHIP_T *chip);
uint8_t  TMC5160_HomeCheckStall(TMC5160_CHIP_T *chip);
uint16_t TMC5160_HomeGetSg(TMC5160_CHIP_T *chip);
void     TMC5160_HomeZero(TMC5160_CHIP_T *chip);

#endif /* TMC5160_H */
