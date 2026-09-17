/*****************************************************************************
 * @文件: homing.c
 * @作者: cl
 * @日期: 2026-09-12
 * @版本: v1.0
 * @说明: 无编码器回零编排层（StallGuard2 堵转检测碰硬限位，不用编码器）
 *        标准流程依据 ch13 §13.4：硬限位方向速度运动 → 堵转 →
 *        反向回退防卡死 → XACTUAL 写 0 标定零点
 *        芯片级 SG 原语在 drv/tmc5160（TMC5160_Home*）；本层负责状态机/时序，
 *        运动经 app/motor_ctrl（保持闭环命令目标 s_cmd_target 同步）
 * @依赖: drv/tmc5160(SG 原语/位置), drv/can(完成反馈), app/motor_ctrl(运动), drv/uart_dbg
 ****************************************************************************/
#include "app/homing.h"
#include "app/motor_ctrl.h"
#include "drv/tmc5160.h"
#include "drv/can.h"
#include "drv/uart_dbg.h"
#include "main.h"

/* ==== 确认与超时 ==== */
/* SG 轮询周期: fullstep 周期@2RPS=1/(2×200)=2.5ms, 取 2ms 覆盖每 fullstep
 * 依据 .cl/memory/config.md home_sg_poll_ms=2
 * （SG_RESULT 随 fullstep 更新, ch13 §13.1） */
#define HOME_SG_POLL_MS    2U
/* 连续零确认次数（用户 2026-09-12 定） */
#define HOME_SG_CONFIRM    8U
/* 反向回退步数（用户 2026-09-12 定） */
#define HOME_BO_STEPS      2048L
/* 寻零超时：机械行程未知，2RPS 下 30s=60 转上限兜底（待实测确认）
 * 依据 .cl/memory/config.md home_run_timeout_ms=30000 */
#define HOME_RUN_TIMEOUT   30000U
/* 回退到位超时：2048µsteps 三角斜坡约 60ms(AMAX≈2.05M µsteps/s²)，取 10s 兜底堵死
 * （待实测确认）依据 .cl/memory/config.md home_bo_timeout_ms=10000 */
#define HOME_BO_TIMEOUT    10000U

/* ==== 内部状态 ==== */
static HOME_STATE_T s_state = HOME_IDLE;
static uint8_t s_motor = 0;
static int8_t s_seek_sign = 0;   /* +1 正向寻零 / -1 负向寻零 */
static uint8_t s_confirm = 0;
static uint8_t s_stable = 0;
static uint32_t s_last_poll = 0;
static uint32_t s_t_mark = 0;
static int32_t s_bo_target = 0;
static uint8_t s_result = 1;      /* 0=成功，1=失败/未完成 */
static uint32_t s_last_trace = 0; /* SG 跟踪打印节流（SGT 调定后删除） */

/* ==== 内部工具 ==== */

/**
 * @输入 ok: 1=成功(DONE)，0=失败/超时(FAIL)
 * @输出 无
 * @说明 进入终态：停车(经 MOTOR_Stop 同步闭环目标)→恢复现场→发 CAN 反馈
 * @注意 改全局状态机并发反馈帧；MOTOR_Stop 保持闭环命令目标一致
 */
static void S_Finish(uint8_t ok)
{
    TMC5160_CHIP_T *chip = MOTOR_GetChip(s_motor);

    if ((void *)0 != chip)
    {
        MOTOR_Stop(s_motor);
        TMC5160_HomeRestore(chip);
    }
    if (0U != ok)
    {
        s_state = HOME_DONE;
        s_result = 0;
        CAN_SendMotionFeedback(s_motor, 0, STATUS_DONE, 0);
    }
    else
    {
        s_state = HOME_FAIL;
        s_result = 1;
        CAN_SendMotionFeedback(s_motor, 0, STATUS_STALL, 0);
    }
}

/**
 * @输入 无
 * @输出 无
 * @说明 堵转确认后转入回退段：停车→反向回退 HOME_BO_STEPS→登记回退目标
 *        回退方向与寻零方向相反（防卡死在限位）；运动经 MOTOR_MoveBy(同步闭环目标)
 */
static void S_EnterBackoff(void)
{
    TMC5160_CHIP_T *chip = MOTOR_GetChip(s_motor);
    int32_t backoff;

    MOTOR_Stop(s_motor);
    backoff = (0 < s_seek_sign) ? -HOME_BO_STEPS : HOME_BO_STEPS;
    MOTOR_MoveBy(s_motor, backoff);
    if ((void *)0 != chip)
    {
        s_bo_target = TMC5160_GetPosition(chip) + backoff;
    }
    s_stable = 0;
    s_t_mark = HAL_GetTick();
    s_state = HOME_BACKOFF;
}

/* ==== 接口实现 ==== */

/**
 * @输入 motor: 电机编号(U1/U2)；dir: 方向(TMC5160_HOME_DIR_*)
 * @输出 0=已启动，1=忙（上次未取走结果），2=参数非法
 * @说明 配置 SG 检测并以速度模式撞向硬限位；非阻塞
 * @注意 仅单电机（无 ALL）；终态(DONE/FAIL)允许直接重启动
 */
uint8_t HOME_Start(uint8_t motor, uint8_t dir)
{
    TMC5160_CHIP_T *chip;

    /* 仅 RUN/BACKOFF 算忙（终态反馈已发出，HOME_TakeResult 仅供查询） */
    if (HOME_RUN == s_state || HOME_BACKOFF == s_state)
    {
        return 1;   /* 忙：上一次回零仍在进行（RUN/BACKOFF） */
    }
    chip = MOTOR_GetChip(motor);
    if (((void *)0 == chip) ||
        (TMC5160_HOME_DIR_POSITIVE != dir && TMC5160_HOME_DIR_NEGATIVE != dir))
    {
        return 2;   /* 参数非法：电机编号无效(非 U1/U2) 或方向非法 */
    }

    s_motor = motor;
    s_seek_sign = (TMC5160_HOME_DIR_POSITIVE == dir) ? 1 : -1;
    s_confirm = 0;

    /* 配置 SG 检测（芯片级原语在 drv/tmc5160） */
    TMC5160_HomeConfig(chip);

    /* 速度模式撞向限位（sg_stop 硬件兜底，软件 N 确认主判） */
    if (0 < s_seek_sign)
    {
        MOTOR_SetVelocity(motor, (int32_t)TMC5160_HOME_VMAX);
    }
    else
    {
        MOTOR_SetVelocity(motor, -(int32_t)TMC5160_HOME_VMAX);
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
void HOME_Tick(void)
{
    TMC5160_CHIP_T *chip;
    uint32_t now;
    uint16_t sg;

    if (HOME_RUN != s_state && HOME_BACKOFF != s_state)
    {
        return;
    }
    chip = MOTOR_GetChip(s_motor);
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

        /* 硬件备份：sg_stop 已触发 event_stop_sg（读+清，drv 原语） */
        if (0U != TMC5160_HomeCheckStall(chip))
        {
            S_EnterBackoff();
            return;
        }

        /* 主判据：SG_RESULT 连续零确认（用户 2026-09-12 定） */
        sg = TMC5160_HomeGetSg(chip);
        /* SGT 调定诊断：寻零期间每 500ms 打印 SG（调定后删除） */
        if ((now - s_last_trace) >= 500U)
        {
            s_last_trace = now;
            UART_DBG_Printf("[HOME sg] sg=%u v=%ld n=%u\r\n",
                            (unsigned)sg,
                            (long)TMC5160_GetVelocity(chip),
                            (unsigned)s_confirm);
        }
        if (0U == sg)
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
        if (s_bo_target == TMC5160_GetPosition(chip))
        {
            s_stable++;
            if (s_stable >= 2U)
            {
                /* 标定零点（ch06.p040：homing 时允许改 XACTUAL） */
                TMC5160_HomeZero(chip);
                MOTOR_Stop(s_motor);
                TMC5160_HomeRestore(chip);
                s_state = HOME_DONE;
                s_result = 0;
                CAN_SendMotionFeedback(s_motor, 0, STATUS_DONE, 0);
            }
        }
        else
        {
            s_stable = 0;
        }
    }
}

/* 查询当前状态 */
HOME_STATE_T HOME_GetState(void)
{
    return s_state;
}

/**
 * @输出 0=成功，1=失败/未完成
 * @说明 取走终态结果并回 IDLE（仅终态有效）
 */
uint8_t HOME_TakeResult(void)
{
    uint8_t r = s_result;

    if (HOME_DONE == s_state || HOME_FAIL == s_state)
    {
        s_state = HOME_IDLE;
        s_result = 1;
    }
    return r;
}
