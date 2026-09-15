/*****************************************************************************
 * @文件: homing.c
 * @作者: cl
 * @日期: 2026-09-12
 * @版本: v1.0
 * @说明: 无编码器回零编排层（StallGuard2 堵转检测碰硬限位，不用编码器）
 *        标准流程依据 ch13 §13.4：硬限位方向速度运动 → 堵转 →
 *        反向回退防卡死 → XACTUAL 写 0 标定零点
 * @依赖: drv/tmc5160(寄存器读写), drv/can(完成反馈), app/motor_ctrl(运动)
 ****************************************************************************/
#include "app/homing.h"
#include "app/motor_ctrl.h"
#include "drv/tmc5160.h"
#include "drv/can.h"
#include "drv/uart_dbg.h"
#include "main.h"

/* ==== 寄存器地址 ==== */
/* 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p040.md: RAMPMODE/XACTUAL */
#define REG_RAMPMODE     0x20
#define REG_XACTUAL      0x21
/* 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch05.p038.md: TSTEP/TCOOLTHRS */
#define REG_TCOOLTHRS    0x14
#define REG_VMAX         0x27
#define REG_AMAX         0x26
/* 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p042.md: SW_MODE/RAMP_STAT */
#define REG_SW_MODE      0x34
#define REG_RAMP_STAT    0x35
/* 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch01.p053.md: COOLCONF */
#define REG_COOLCONF     0x6D
#define REG_DRVSTATUS    0x6F

/* ==== 回零运动参数 ==== */
/* 回零速度取 2RPS（ch13 §13.4 推荐 1~5RPS 区间内）：
 * 要求 V=2×51200=102400µsteps/s →
 * VMAX=V×2^24/fCLK=102400×16777216/15e6≈114532
 * 依据 .cl/datasheet/TMC5160A_Datasheet_Rev1.14_ch12_12_ramp_generator.md:
 *   t=2^24/fCLK + .cl/memory/config.md fCLK=15MHz/51200µsteps/rev */
#define HOME_VMAX        114532UL
/* 加速 AMAX=20000（与参数组 4 同量级）：
 * a=20000×fCLK²/2^41≈2.05M µsteps/s² → 0→2RPS 约 50ms/2560µsteps，
 * 起点须远于此距离（ch13 §13.4） */
#define HOME_AMAX        20000UL
/* TCOOLTHRS=200：TSTEP=2^24/VMAX≈146，取 200 略高于 146
 * （ch13 §13.1 + ch05.p038 TCOOLTHRS≥TSTEP），SG 在≥约 1.46RPS 生效 */
#define HOME_TCOOLTHRS   200UL
/* SGT=0：datasheet 起始值（ch13 §13.1），阈值待真机交互调定（待实测确认） */
/* sfilt=0：回零用非滤波模式（ch13 §13.3，滤波只利精度不利响应） */
/* SEMIN=0：CoolStep 关（仅用 StallGuard 堵转检测） */
#define HOME_COOLCONF    0x00000000UL

/* ==== 确认与超时 ==== */
#define HOME_SG_POLL_MS    2U      /* SG 轮询周期（fullstep 约 2.5ms@2RPS） */
#define HOME_SG_CONFIRM    8U      /* 连续零确认次数（用户 2026-09-12 定） */
#define HOME_BO_STEPS      2048L   /* 反向回退步数（用户 2026-09-12 定） */
#define HOME_RUN_TIMEOUT   30000U  /* 寻零超时（机械行程未知，待实测确认） */
#define HOME_BO_TIMEOUT    10000U  /* 回退到位超时 */

/* ==== 状态位 ==== */
/* 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p044.md:
 *   RAMP_STAT event_stop_sg=bit6(R+WC，写 1 清) */
#define RAMP_EVENT_STOP_SG  (1UL << 6)
/* 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p043.md:
 *   SW_MODE sg_stop=bit10 */
#define SW_MODE_SG_STOP     (1UL << 10)
/* 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p056.md:
 *   DRV_STATUS SG_RESULT=bits[9:0]（0=最大负载/堵转） */
#define DRV_SG_RESULT_MSK   0x3FFUL

/* ==== 内部状态 ==== */
static HOME_STATE_T s_state = HOME_IDLE;
static uint8_t s_motor = 0;
static int8_t s_seek_sign = 0;   /* +1 正向寻零 / -1 负向寻零 */
static uint8_t s_confirm = 0;
static uint8_t s_stable = 0;
static uint32_t s_last_poll = 0;
static uint32_t s_t_mark = 0;
static int32_t s_bo_target = 0;
static uint32_t s_sw_mode_saved = 0;
static uint32_t s_tcoolthrs_saved = 0;
static uint8_t s_result = 1;     /* 0=成功，1=失败/未完成 */
static uint32_t s_last_trace = 0; /* SG 跟踪打印节流（SGT 调定后删除） */

/* ==== 内部工具 ==== */

/* 恢复寻零前寄存器现场（SW_MODE/TCOOLTHRS） */
static void S_Restore(TMC5160_CHIP_T *chip)
{
    USR_TMC5160_WriteReg(chip, REG_SW_MODE, s_sw_mode_saved);
    USR_TMC5160_WriteReg(chip, REG_TCOOLTHRS, s_tcoolthrs_saved);
}

/* 进入终态：停车→恢复现场→发 CAN 反馈 */
static void S_Finish(uint8_t ok)
{
    TMC5160_CHIP_T *chip = USR_MOTOR_GetChip(s_motor);

    if ((void *)0 != chip)
    {
        USR_MOTOR_Stop(s_motor);
        S_Restore(chip);
    }
    if (0U != ok)
    {
        s_state = HOME_DONE;
        s_result = 0;
        USR_CAN_SendMotionFeedback(s_motor, 0, STATUS_DONE, 0);
    }
    else
    {
        s_state = HOME_FAIL;
        s_result = 1;
        USR_CAN_SendMotionFeedback(s_motor, 0, STATUS_STALL, 0);
    }
}

/* 堵转确认后转入回退段 */
static void S_EnterBackoff(void)
{
    TMC5160_CHIP_T *chip = USR_MOTOR_GetChip(s_motor);
    int32_t backoff;

    USR_MOTOR_Stop(s_motor);
    /* 反向回退：寻零为负向则回退 +2048，反之 -2048 */
    backoff = (0 < s_seek_sign) ? -HOME_BO_STEPS : HOME_BO_STEPS;
    USR_MOTOR_MoveBy(s_motor, backoff);
    if ((void *)0 != chip)
    {
        s_bo_target = USR_TMC5160_GetPosition(chip) + backoff;
    }
    s_stable = 0;
    s_t_mark = HAL_GetTick();
    s_state = HOME_BACKOFF;
}

/* ==== 接口实现 ==== */

/**
 * @输入 motor: 电机编号(U1/U2)；dir: 方向(HOME_DIR_*)
 * @输出 0=已启动，1=忙（上次未取走结果），2=参数非法
 * @说明 配置 SG 检测并以速度模式撞向硬限位；非阻塞
 */
uint8_t USR_HOME_Start(uint8_t motor, uint8_t dir)
{
    TMC5160_CHIP_T *chip;
    uint32_t sw;

    /* 终态(DONE/FAIL)允许直接重启动；仅 RUN/BACKOFF 算忙
     * （终态反馈已发出，TakeResult 仅供查询） */
    if (HOME_RUN == s_state || HOME_BACKOFF == s_state)
    {
        return 1;
    }
    chip = USR_MOTOR_GetChip(motor);
    if (((void *)0 == chip) || (HOME_DIR_POSITIVE != dir && HOME_DIR_NEGATIVE != dir))
    {
        return 2;
    }

    s_motor = motor;
    s_seek_sign = (HOME_DIR_POSITIVE == dir) ? 1 : -1;
    s_confirm = 0;

    /* 保存现场，配置 SG 检测（SGT/sfilt/SEMIN 见 HOME_COOLCONF 注） */
    s_sw_mode_saved = USR_TMC5160_ReadReg(chip, REG_SW_MODE);
    s_tcoolthrs_saved = USR_TMC5160_ReadReg(chip, REG_TCOOLTHRS);
    USR_TMC5160_WriteReg(chip, REG_COOLCONF, HOME_COOLCONF);
    USR_TMC5160_WriteReg(chip, REG_TCOOLTHRS, HOME_TCOOLTHRS);
    USR_TMC5160_WriteReg(chip, REG_AMAX, HOME_AMAX);
    sw = s_sw_mode_saved | SW_MODE_SG_STOP;
    USR_TMC5160_WriteReg(chip, REG_SW_MODE, sw);

    /* 速度模式撞向限位（sg_stop 硬件兜底，软件 N 确认主判） */
    if (0 < s_seek_sign)
    {
        USR_MOTOR_SetVelocity(motor, (int32_t)HOME_VMAX);
    }
    else
    {
        USR_MOTOR_SetVelocity(motor, -(int32_t)HOME_VMAX);
    }
    s_last_poll = HAL_GetTick();
    s_t_mark = s_last_poll;
    s_state = HOME_RUN;
    return 0;
}

/**
 * @输入 无
 * @输出 无
 * @说明 主循环每圈调用，推进寻零/回退状态机；终态发 CAN 反馈
 */
void USR_HOME_Tick(void)
{
    TMC5160_CHIP_T *chip;
    uint32_t now;
    uint32_t rs;
    uint32_t ds;

    if (HOME_RUN != s_state && HOME_BACKOFF != s_state)
    {
        return;
    }
    chip = USR_MOTOR_GetChip(s_motor);
    if ((void *)0 == chip)
    {
        S_Finish(0);
        return;
    }
    now = HAL_GetTick();

    if (HOME_RUN == s_state)
    {
        /* 超时：未碰到限位（电机脱开/行程超限） */
        if ((now - s_t_mark) >= HOME_RUN_TIMEOUT)
        {
            S_Finish(0);
            return;
        }
        if ((now - s_last_poll) < HOME_SG_POLL_MS)
        {
            return;
        }
        s_last_poll = now;

        /* 硬件备份：sg_stop 已触发 event_stop_sg（写 1 清） */
        rs = USR_TMC5160_ReadReg(chip, REG_RAMP_STAT);
        if (0UL != (rs & RAMP_EVENT_STOP_SG))
        {
            USR_TMC5160_WriteReg(chip, REG_RAMP_STAT, RAMP_EVENT_STOP_SG);
            S_EnterBackoff();
            return;
        }

        /* 主判据：SG_RESULT 连续零确认（用户 2026-09-12 定） */
        ds = USR_TMC5160_GetDrvStatus(chip);
        /* SGT 调定诊断：寻零期间每 500ms 打印 SG（调定后删除） */
        if ((now - s_last_trace) >= 500U)
        {
            s_last_trace = now;
            UART_DBG_Printf("[HOME sg] sg=%lu rs=%08lX v=%ld n=%u\r\n",
                            (unsigned long)(ds & DRV_SG_RESULT_MSK),
                            (unsigned long)rs,
                            (long)USR_TMC5160_GetVelocity(chip),
                            (unsigned)s_confirm);
        }
        if (0UL == (ds & DRV_SG_RESULT_MSK))
        {
            s_confirm++;
            if (s_confirm >= HOME_SG_CONFIRM)
            {
                S_EnterBackoff();
                return;
            }
        }
        else
        {
            s_confirm = 0;
        }
    }
    else /* HOME_BACKOFF */
    {
        /* 超时：回退未到位 */
        if ((now - s_t_mark) >= HOME_BO_TIMEOUT)
        {
            S_Finish(0);
            return;
        }
        if ((now - s_last_poll) < 10U)
        {
            return;
        }
        s_last_poll = now;

        /* 到位轮询：XACTUAL 连续 2 次 == 目标 */
        if (s_bo_target == USR_TMC5160_GetPosition(chip))
        {
            s_stable++;
            if (s_stable >= 2U)
            {
                /* 标定零点（ch06.p040：homing 时允许改 XACTUAL） */
                USR_TMC5160_WriteReg(chip, REG_XACTUAL, 0UL);
                USR_MOTOR_Stop(s_motor);
                S_Restore(chip);
                s_state = HOME_DONE;
                s_result = 0;
                USR_CAN_SendMotionFeedback(s_motor, 0, STATUS_DONE, 0);
            }
        }
        else
        {
            s_stable = 0;
        }
    }
}

/* 查询当前状态 */
HOME_STATE_T USR_HOME_GetState(void)
{
    return s_state;
}

/**
 * @输出 0=成功，1=失败/未完成
 * @说明 取走终态结果并回 IDLE（仅终态有效）
 */
uint8_t USR_HOME_TakeResult(void)
{
    uint8_t r = s_result;

    if (HOME_DONE == s_state || HOME_FAIL == s_state)
    {
        s_state = HOME_IDLE;
        s_result = 1;
    }
    return r;
}
