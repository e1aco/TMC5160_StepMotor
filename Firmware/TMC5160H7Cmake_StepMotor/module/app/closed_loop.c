/*****************************************************************************
 * @文件: closed_loop.c
 * @作者: cl
 * @日期: 2026-08-25
 * @版本: v1.0
 * @说明: 闭环控制模块（编码器反馈 + 补步；PID 预留未接入控制回路）
 * @来源: 自 TMC5160_StepMotor(F407) 移植归一
 * @变更点: 移除 Init 中源工程的闭环节拍定时器启动调用——目标板无 TIM7，
 *          闭环节拍由外部提供，当前保持源工程的注释态（闭环未自动运行）
 * @依赖: drv/tmc5160, app/motor_ctrl, algo/pid
 ****************************************************************************/
#include "app/closed_loop.h"
#include "algo/pid.h"
#include "app/motor_ctrl.h"
#include "drv/tmc5160.h"

/* ==== 内部常量 ==== */
/* 默认 PID 参数（保守值，首次测试用） */
#define CL_PID_KP_DEFAULT      5
#define CL_PID_KI_DEFAULT      0
#define CL_PID_KD_DEFAULT      0

/* 输出限幅（每周期最大修正量） */
#define CL_PID_OUT_MIN         (-200)
#define CL_PID_OUT_MAX         200

/* ==== 内部变量 ==== */
/* 命令目标位置（用户要求的位置，闭环对比基准） */
static int32_t s_cmd_target[2];

static PID_T s_pid_st;

/* ==== 接口实现 ==== */

/**
 * @输入 无
 * @输出 无
 * @说明 初始化闭环 PID 参数与命令目标
 * @注意 闭环节拍定时器暂未配置（目标板无 TIM7），Tick 由主循环按需调用
 */
void CLOSEDLOOP_Init(void)
{
    PID_Init(&s_pid_st,
                 CL_PID_KP_DEFAULT, CL_PID_KI_DEFAULT, CL_PID_KD_DEFAULT,
                 CL_PID_OUT_MIN, CL_PID_OUT_MAX);
    s_cmd_target[0] = 0;
    s_cmd_target[1] = 0;
}

/**
 * @输入 motor: 电机编号; target: 命令目标位置
 * @输出 无
 * @说明 设置闭环命令基准目标，由电机控制层在发出运动指令时调用
 */
void CLOSEDLOOP_SetTarget(uint8_t motor, int32_t target)
{
    if (MOTOR_CTRL_U1 == motor)
    {
        s_cmd_target[0] = target;
    }
    else if (MOTOR_CTRL_U2 == motor)
    {
        s_cmd_target[1] = target;
    }
    PID_Reset(&s_pid_st);
}

/**
 * @输入 motor: 电机编号
 * @输出 无
 * @说明 使能闭环控制，已使能时跳过
 * @注意 状态即时生效（RAM 常驻）
 */
void CLOSEDLOOP_Enable(uint8_t motor)
{
    TMC5160_CHIP_T *chip = MOTOR_GetChip(motor);
    if ((void *)0 != chip)
    {
        if (CLOSED_LOOP_ON == chip->closed_loop)
        {
            return;
        }
        chip->closed_loop = CLOSED_LOOP_ON;
    }
    PID_Reset(&s_pid_st);
}

/**
 * @输入 motor: 电机编号
 * @输出 无
 * @说明 禁用闭环控制，已禁用时跳过
 * @注意 状态即时生效（RAM 常驻）
 */
void CLOSEDLOOP_Disable(uint8_t motor)
{
    TMC5160_CHIP_T *chip = MOTOR_GetChip(motor);
    if ((void *)0 != chip)
    {
        if (CLOSED_LOOP_OFF == chip->closed_loop)
        {
            return;
        }
        chip->closed_loop = CLOSED_LOOP_OFF;
    }
}

/**
 * @输入 motor: 电机编号
 * @输出 uint8_t: 闭环模式状态
 */
uint8_t CLOSEDLOOP_GetMode(uint8_t motor)
{
    TMC5160_CHIP_T *chip = MOTOR_GetChip(motor);
    if ((void *)0 != chip)
    {
        return chip->closed_loop;
    }
    return CLOSED_LOOP_OFF;
}

/**
 * @输入 motor: 电机编号
 * @输出 无
 * @说明 闭环控制周期（外部节拍或主循环调用）
 *   1. 等待斜坡完成（XACTUAL == 命令目标）后才介入
 *   2. 读取编码器(X_ENC)，对比命令目标
 *   3. 偏差超出容差 → 补步 + 更新命令目标
 * @注意 不干涉正在运行的斜坡，防止目标跑飞
 */
void CLOSEDLOOP_Tick(uint8_t motor)
{
    TMC5160_CHIP_T *chip;
    int32_t x_actual, x_enc, cmd_target, deviation, new_target;
    int32_t idx;

    chip = MOTOR_GetChip(motor);
    if ((void *)0 == chip)
    {
        return;
    }

    if (CLOSED_LOOP_OFF == chip->closed_loop)
    {
        return;
    }

    /* 取命令目标（固定值） */
    idx = (MOTOR_CTRL_U1 == motor) ? 0 : 1;
    cmd_target = s_cmd_target[idx];

    /* 斜坡未完成 → 不干涉，让 TMC5160 自行到位 */
    x_actual = MOTOR_GetPosition(motor);
    if (x_actual != cmd_target)
    {
        return;
    }

    /* 读编码器，算偏差 */
    x_enc = MOTOR_GetEncoderPosition(motor);
    deviation = cmd_target - x_enc;

    /* 偏差在容差内 → 不动 */
    if (deviation > -TMC5160_ENC_TOLERANCE &&
        deviation <  TMC5160_ENC_TOLERANCE)
    {
        return;
    }

    /* 偏差超出容差 → 补步，同时更新命令目标 */
    new_target = cmd_target + deviation;
    CLOSEDLOOP_SetTarget(motor, new_target);
    TMC5160_MoveTo(chip, new_target);
}

