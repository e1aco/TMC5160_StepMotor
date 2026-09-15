/*****************************************************************************
 * @文件: homing.h
 * @作者: cl
 * @日期: 2026-09-12
 * @版本: v1.0
 * @说明: 无编码器回零编排层（StallGuard2 堵转检测碰硬限位）
 *        流程: 速度模式撞限位 → SG_RESULT 连续 N 次零确认 →
 *        反向回退防卡死 → XACTUAL 写 0 标定零点
 * @依赖: drv/tmc5160, drv/can(完成反馈), app/motor_ctrl
 ****************************************************************************/
#ifndef HOMING_H
#define HOMING_H

#include <stdint.h>

/* ==== 回零方向 ==== */
#define HOME_DIR_NEGATIVE  0   /* 向负方向（XACTUAL 减小） */
#define HOME_DIR_POSITIVE  1   /* 向正方向（XACTUAL 增大） */

/* ==== 回零状态 ==== */
typedef enum {
    HOME_IDLE = 0,   /* 空闲，可启动 */
    HOME_RUN,        /* 寻零中（速度模式撞限位） */
    HOME_BACKOFF,    /* 反向回退中 */
    HOME_DONE,       /* 回零成功 */
    HOME_FAIL        /* 回零失败（超时/参数非法） */
} HOME_STATE_T;

/* ==== 接口 ==== */

/* 启动回零（非阻塞，实际推进由 USR_HOME_Tick 完成） */
uint8_t USR_HOME_Start(uint8_t motor, uint8_t dir);

/* 主循环每圈调用，推进状态机；终态时发 CAN 完成反馈 */
void USR_HOME_Tick(void);

/* 查询状态 / 取走终态（取后回 IDLE，允许下一次启动） */
HOME_STATE_T USR_HOME_GetState(void);
uint8_t USR_HOME_TakeResult(void);

#endif /* HOMING_H */
