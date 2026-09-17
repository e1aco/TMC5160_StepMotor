/*****************************************************************************
 * @文件: motor_ctrl.c
 * @作者: cl
 * @日期: 2026-08-25
 * @版本: v1.0
 * @说明: 电机控制中间层（封装电机选择 + 运动执行）
 * @来源: 自 TMC5160_StepMotor(F407) 移植归一，行为等价（纯逻辑零适配）
 * @依赖: drv/tmc5160, app/closed_loop
 ****************************************************************************/
#include "app/motor_ctrl.h"
#include "drv/tmc5160.h"
#include "app/closed_loop.h"

/* ==== 内部工具 ==== */

/**
 * @输入 motor: 电机编号
 * @输出 TMC5160_CHIP_T*: 芯片指针，无效编号返回 NULL
 * @说明 根据电机编号获取 TMC5160 芯片指针
 */
TMC5160_CHIP_T *MOTOR_GetChip(uint8_t motor)
{
    if (MOTOR_CTRL_U1 == motor)
    {
        return &g_tmc5160_chip1_st;
    }
    else if (MOTOR_CTRL_U2 == motor)
    {
        return &g_tmc5160_chip2_st;
    }
    return (void *)0;
}

/* ==== 接口实现 ==== */

/**
 * @输入 无
 * @输出 无
 * @说明 电机控制模块初始化（实际由 TMC5160_Init 完成）
 */
void MOTOR_Init(void)
{
    /* 初始化由 TMC5160_Init() 完成，此处无需额外操作 */
}

/**
 * @输入 motor: 电机编号; target: 目标绝对位置
 * @输出 无
 * @说明 运动到目标位置，支持单电机或全部电机
 */
void MOTOR_MoveTo(uint8_t motor, int32_t target)
{
    if (MOTOR_CTRL_ALL == motor)
    {
        TMC5160_MoveTo(&g_tmc5160_chip1_st, target);
        CLOSEDLOOP_SetTarget(MOTOR_CTRL_U1, target);
        TMC5160_MoveTo(&g_tmc5160_chip2_st, target);
        CLOSEDLOOP_SetTarget(MOTOR_CTRL_U2, target);
    }
    else
    {
        TMC5160_CHIP_T *chip = MOTOR_GetChip(motor);
        if ((void *)0 != chip)
        {
            TMC5160_MoveTo(chip, target);
            CLOSEDLOOP_SetTarget(motor, target);
        }
    }
}

/**
 * @输入 motor: 电机编号; offset: 相对偏移量(+正转, -反转)
 * @输出 无
 * @说明 运动指定偏移量
 */
void MOTOR_MoveBy(uint8_t motor, int32_t offset)
{
    if (MOTOR_CTRL_ALL == motor)
    {
        int32_t t1 = TMC5160_GetPosition(&g_tmc5160_chip1_st) + offset;
        int32_t t2 = TMC5160_GetPosition(&g_tmc5160_chip2_st) + offset;
        TMC5160_MoveBy(&g_tmc5160_chip1_st, offset);
        CLOSEDLOOP_SetTarget(MOTOR_CTRL_U1, t1);
        TMC5160_MoveBy(&g_tmc5160_chip2_st, offset);
        CLOSEDLOOP_SetTarget(MOTOR_CTRL_U2, t2);
    }
    else
    {
        TMC5160_CHIP_T *chip = MOTOR_GetChip(motor);
        if ((void *)0 != chip)
        {
            int32_t final = TMC5160_GetPosition(chip) + offset;
            TMC5160_MoveBy(chip, offset);
            CLOSEDLOOP_SetTarget(motor, final);
        }
    }
}

/**
 * @输入 motor: 电机编号; velocity: 目标速度(+正转, -反转)
 * @输出 无
 * @说明 切换速度模式持续旋转
 */
void MOTOR_SetVelocity(uint8_t motor, int32_t velocity)
{
    if (MOTOR_CTRL_ALL == motor)
    {
        TMC5160_SetVelocity(&g_tmc5160_chip1_st, velocity);
        TMC5160_SetVelocity(&g_tmc5160_chip2_st, velocity);
    }
    else
    {
        TMC5160_CHIP_T *chip = MOTOR_GetChip(motor);
        if ((void *)0 != chip)
        {
            TMC5160_SetVelocity(chip, velocity);
        }
    }
}

/**
 * @输入 motor: 电机编号
 * @输出 无
 * @说明 停止电机，切回定位模式保持锁轴
 */
void MOTOR_Stop(uint8_t motor)
{
    if (MOTOR_CTRL_ALL == motor)
    {
        int32_t p1 = TMC5160_GetPosition(&g_tmc5160_chip1_st);
        int32_t p2 = TMC5160_GetPosition(&g_tmc5160_chip2_st);
        TMC5160_Stop(&g_tmc5160_chip1_st);
        CLOSEDLOOP_SetTarget(MOTOR_CTRL_U1, p1);
        TMC5160_Stop(&g_tmc5160_chip2_st);
        CLOSEDLOOP_SetTarget(MOTOR_CTRL_U2, p2);
    }
    else
    {
        TMC5160_CHIP_T *chip = MOTOR_GetChip(motor);
        if ((void *)0 != chip)
        {
            int32_t pos = TMC5160_GetPosition(chip);
            TMC5160_Stop(chip);
            CLOSEDLOOP_SetTarget(motor, pos);
        }
    }
}

/**
 * @输入 motor: 电机编号; group: 运动参数组 ID(1~5)
 * @输出 无
 * @说明 应用预定义运动参数组
 */
void MOTOR_ApplyProfile(uint8_t motor, uint8_t group)
{
    if (MOTOR_CTRL_ALL == motor)
    {
        TMC5160_ApplyProfile(&g_tmc5160_chip1_st, group);
        TMC5160_ApplyProfile(&g_tmc5160_chip2_st, group);
    }
    else
    {
        TMC5160_CHIP_T *chip = MOTOR_GetChip(motor);
        if ((void *)0 != chip)
        {
            TMC5160_ApplyProfile(chip, group);
        }
    }
}

/**
 * @输入 motor: 电机编号
 * @输出 int32_t: 当前位置，无效电机返回 0
 */
int32_t MOTOR_GetPosition(uint8_t motor)
{
    TMC5160_CHIP_T *chip = MOTOR_GetChip(motor);

    if ((void *)0 != chip)
    {
        return TMC5160_GetPosition(chip);
    }
    return 0;
}

/**
 * @输入 motor: 电机编号
 * @输出 int32_t: 编码器当前位姿(X_ENC 归零后)
 */
int32_t MOTOR_GetEncoderPosition(uint8_t motor)
{
    TMC5160_CHIP_T *chip = MOTOR_GetChip(motor);

    if ((void *)0 != chip)
    {
        return TMC5160_GetEncoderPosition(chip);
    }
    return 0;
}

/**
 * @输入 motor: 电机编号
 * @输出 int32_t: X_ENC - XACTUAL 偏差（绝对值）
 */
int32_t MOTOR_GetEncoderDeviation(uint8_t motor)
{
    TMC5160_CHIP_T *chip = MOTOR_GetChip(motor);

    if ((void *)0 != chip)
    {
        int32_t x_act = TMC5160_GetPosition(chip);
        int32_t x_enc = TMC5160_GetEncoderPosition(chip);
        return x_enc - x_act;
    }
    return 0;
}

/**
 * @输入 motor: 电机编号
 * @输出 uint8_t: 状态标志位
 */
uint8_t MOTOR_GetStatus(uint8_t motor)
{
    TMC5160_CHIP_T *chip = MOTOR_GetChip(motor);

    if ((void *)0 != chip)
    {
        return TMC5160_GetStatusFlags(chip);
    }
    return 0;
}

/**
 * @输入 motor: 电机编号
 * @输出 uint8_t: 运动阶段标志
 */
uint8_t MOTOR_GetStage(uint8_t motor)
{
    TMC5160_CHIP_T *chip = MOTOR_GetChip(motor);

    if ((void *)0 != chip)
    {
        return TMC5160_GetMotionPhase(chip);
    }
    return 0;
}

