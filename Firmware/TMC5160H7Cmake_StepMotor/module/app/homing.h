/*****************************************************************************
 * @文件: homing.h
 * @作者: cl
 * @日期: 2026-09-12
 * @版本: v1.0
 * @说明: 无编码器回零编排层（StallGuard2 堵转碰硬限位）——状态机/时序/CAN 反馈；
 *        芯片级 SG 原语在 drv/tmc5160（TMC5160_Home*）
 * @依赖: drv/tmc5160, drv/can(完成反馈), app/motor_ctrl(运动+闭环目标同步)
 ****************************************************************************/
#ifndef HOMING_H
#define HOMING_H

#include <stdint.h>
#include "drv/tmc5160.h"   /* 方向宏 TMC5160_HOME_DIR_x 与速度 TMC5160_HOME_VMAX */

/* ==== 回零状态 ==== */
typedef enum {
    HOME_IDLE = 0,   /* 空闲，可启动 */
    HOME_RUN,        /* 寻零中（速度模式撞限位） */
    HOME_BACKOFF,    /* 反向回退中 */
    HOME_DONE,       /* 回零成功 */
    HOME_FAIL        /* 回零失败（超时/参数非法） */
} HOME_STATE_T;

/* ==== 接口 ==== */

/* 启动回零（非阻塞，推理由 HOME_Tick 完成）；dir=TMC5160_HOME_DIR_* */
uint8_t HOME_Start(uint8_t motor, uint8_t dir);

/* 主循环每圈调用，推进状态机；终态发 CAN 反馈 */
void HOME_Tick(void);

/* 查询状态 / 取走终态（取后回 IDLE，允许下一次启动） */
HOME_STATE_T HOME_GetState(void);
uint8_t HOME_TakeResult(void);

#endif /* HOMING_H */
