/*****************************************************************************
 * @文件: tmc5160.h
 * @作者: cl
 * @日期: 2026-09-09
 * @版本: v1.0
 * @说明: TMC5160 驱动层统一头 = 原 tmc5160_drv.h + tmc5160_usr.h 文件合并
 *        （用户 2026-09-09 裁决：drv 允许芯片功能完整封装，符号名不变）
 * @来源: 自 TMC5160_StepMotor(F407) 移植归一
 * @平台: STM32H750VBT6 (SPI3, 模式3, 6.45Mbit/s; fCLK=15MHz 外部时钟)
 * @依赖: HAL_SPI, HAL_GPIO
 ****************************************************************************/
#ifndef MDRV_TMC5160_H
#define MDRV_TMC5160_H

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

/* ==== 全局实例 ==== */
extern TMC5160_CHIP_T g_tmc5160_chip1_st;
extern TMC5160_CHIP_T g_tmc5160_chip2_st;

/* ==== 接口: SPI/GPIO 原语 (DRV_) ==== */
void     DRV_TMC5160_SelectChip(uint8_t chip);
void     DRV_TMC5160_DeselectChip(uint8_t chip);
void     DRV_TMC5160_SetMode(uint8_t chip, uint8_t mode);
void     DRV_TMC5160_Enable(uint8_t chip);
void     DRV_TMC5160_Disable(uint8_t chip);
void     DRV_TMC5160_DelayMs(uint32_t ms);
uint8_t  DRV_TMC5160_WriteReg(uint8_t chip, uint8_t reg_addr, uint32_t data);
uint32_t DRV_TMC5160_ReadReg(uint8_t chip, uint8_t reg_addr);
uint8_t  DRV_TMC5160_DebugTransfer(uint8_t chip, uint8_t *tx, uint8_t *rx, uint8_t len);

/* ==== 接口: 芯片功能封装 (USR_，原 tmc5160_usr.h，符号保留) ==== */
void    USR_TMC5160_Init(void);
void    USR_TMC5160_EStop(uint8_t chip);
uint8_t USR_TMC5160_WriteReg(TMC5160_CHIP_T *chip, uint8_t reg_addr, uint32_t data);
uint32_t USR_TMC5160_ReadReg(TMC5160_CHIP_T *chip, uint8_t reg_addr);

void    USR_TMC5160_ApplyProfile(TMC5160_CHIP_T *chip, uint8_t profile_id);
void    USR_TMC5160_MoveTo(TMC5160_CHIP_T *chip, int32_t target);
void    USR_TMC5160_MoveBy(TMC5160_CHIP_T *chip, int32_t offset);
void    USR_TMC5160_SetVelocity(TMC5160_CHIP_T *chip, int32_t velocity);
void    USR_TMC5160_Stop(TMC5160_CHIP_T *chip);

int32_t USR_TMC5160_GetPosition(TMC5160_CHIP_T *chip);
int32_t USR_TMC5160_GetVelocity(TMC5160_CHIP_T *chip);
uint32_t USR_TMC5160_GetRampStat(TMC5160_CHIP_T *chip);
uint32_t USR_TMC5160_GetDrvStatus(TMC5160_CHIP_T *chip);
uint32_t USR_TMC5160_GetGStat(TMC5160_CHIP_T *chip);

uint8_t USR_TMC5160_GetStatusFlags(TMC5160_CHIP_T *chip);
uint8_t USR_TMC5160_GetMotionPhase(TMC5160_CHIP_T *chip);

void    USR_TMC5160_ConfigEncoder(TMC5160_CHIP_T *chip);
int32_t USR_TMC5160_GetEncoderPosition(TMC5160_CHIP_T *chip);
uint32_t USR_TMC5160_GetEncoderStatus(TMC5160_CHIP_T *chip);
int32_t USR_TMC5160_GetEncoderDeviation(TMC5160_CHIP_T *chip);
uint8_t USR_TMC5160_CheckPosition(TMC5160_CHIP_T *chip, int32_t expected_steps);

TMC5160_MOVE_RESULT_T USR_TMC5160_WaitPosition(TMC5160_CHIP_T *chip, uint32_t timeout_ms);
TMC5160_MOVE_RESULT_T USR_TMC5160_MoveToWithVerify(TMC5160_CHIP_T *chip, int32_t target);

#endif /* MDRV_TMC5160_H */
