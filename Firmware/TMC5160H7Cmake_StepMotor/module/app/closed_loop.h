/*****************************************************************************
 * @文件: closed_loop.h
 * @作者: cl
 * @日期: 2026-08-25
 * @版本: v1.0
 * @说明: 闭环控制模块（编码器反馈 + 补步）
 * @来源: 自 TMC5160_StepMotor(F407) 移植归一，行为等价
 * @依赖: usr/tmc5160_usr, usr/motor_ctrl
 ****************************************************************************/
#ifndef CLOSED_LOOP_H
#define CLOSED_LOOP_H

#include <stdint.h>

/* ==== 闭环模式 ==== */
#define CLOSED_LOOP_OFF     0
#define CLOSED_LOOP_ON      1

/* ==== 接口 ==== */
void    USR_CLOSEDLOOP_Init(void);
void    USR_CLOSEDLOOP_Enable(uint8_t motor);
void    USR_CLOSEDLOOP_Disable(uint8_t motor);
void    USR_CLOSEDLOOP_Tick(uint8_t motor);
uint8_t USR_CLOSEDLOOP_GetMode(uint8_t motor);
void    USR_CLOSEDLOOP_SetTarget(uint8_t motor, int32_t target);

#endif /* CLOSED_LOOP_H */
