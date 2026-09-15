/*****************************************************************************
 * @文件: motor_ctrl.h
 * @作者: cl
 * @日期: 2026-08-25
 * @版本: v1.0
 * @说明: 电机控制中间层（封装电机选择 + 运动执行）
 * @来源: 自 TMC5160_StepMotor(F407) 移植归一，行为等价
 * @依赖: drv/tmc5160（原 tmc5160_usr，2026-09-09 文件合并）
 ****************************************************************************/
#ifndef MOTOR_CTRL_H
#define MOTOR_CTRL_H

#include <stdint.h>
#include "drv/tmc5160.h"

/* ==== 电机编号 ==== */
#define MOTOR_CTRL_U1   0x01
#define MOTOR_CTRL_U2   0x02
#define MOTOR_CTRL_ALL  0x06

/* ==== 运动参数组 ID ==== */
#define MOTION_GROUP_1  0x01
#define MOTION_GROUP_2  0x02
#define MOTION_GROUP_3  0x03
#define MOTION_GROUP_4  0x04

/* ==== 接口 ==== */
void           USR_MOTOR_Init(void);
TMC5160_CHIP_T *USR_MOTOR_GetChip(uint8_t motor);
void           USR_MOTOR_MoveTo(uint8_t motor, int32_t target);
void           USR_MOTOR_MoveBy(uint8_t motor, int32_t offset);
void           USR_MOTOR_SetVelocity(uint8_t motor, int32_t velocity);
void           USR_MOTOR_Stop(uint8_t motor);
void           USR_MOTOR_ApplyProfile(uint8_t motor, uint8_t group);
int32_t        USR_MOTOR_GetPosition(uint8_t motor);
int32_t        USR_MOTOR_GetEncoderPosition(uint8_t motor);
int32_t        USR_MOTOR_GetEncoderDeviation(uint8_t motor);
uint8_t        USR_MOTOR_GetStatus(uint8_t motor);
uint8_t        USR_MOTOR_GetStage(uint8_t motor);

#endif /* MOTOR_CTRL_H */

