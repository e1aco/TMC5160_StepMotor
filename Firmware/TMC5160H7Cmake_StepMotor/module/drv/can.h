/*****************************************************************************
 * @文件: can.h
 * @作者: cl
 * @日期: 2026-09-09
 * @版本: v1.0
 * @说明: CAN 驱动层统一头 = 原 can_drv.h + can_usr.h 文件合并
 * @来源: 自 TMC5160_StepMotor(F407 bxCAN) 移植归一，重写为 H7 FDCAN HAL
 * @平台: STM32H750VBT6 (FDCAN2 PB12/PB13, 经典帧 500kbit/s)
 * @依赖: HAL_FDCAN；※协议→电机/闭环 的上行调用（can.c 引用 app/motor_ctrl、
 *        app/closed_loop）为反向依赖，用户 2026-09-09 显式裁决豁免登记，
 *        /cl check 分层扫描按本注豁免
 ****************************************************************************/
#ifndef CAN_H
#define CAN_H

#include "main.h"
#include "fdcan.h"

/* ==== 协议常量 ==== */
#define CAN_RX_ID       0x1AA55F42
#define CAN_TX_ID       0x1AA55F43
#define CAN_DATA_LEN    8

/* ==== 位宏 ==== */
#define BIT(n)          (1UL << (n))

/* ==== 命令码 ==== */
#define CMD_ABS_POS     0x01
#define CMD_REL_CW      0x02
#define CMD_REL_CCW     0x03
#define CMD_VELOCITY    0x04
#define CMD_STOP        0x05
#define CMD_PID_ADJUST  0x06
#define CMD_CL_ENABLE   0x07
#define CMD_CL_DISABLE  0x08
#define CMD_HOME        0x09
#define CMD_ESTOP       0x0A

/* ==== PID 参数类型 (命令 0x06 byte[6]) ==== */
#define CAN_PID_KP          0x01
#define CAN_PID_KI          0x02
#define CAN_PID_KD          0x03
#define CAN_PID_OUT_MAX     0x04
#define CAN_PID_OUT_MIN     0x05
#define CAN_PID_INT_MAX     0x06

/* ==== 状态标志位 ==== */
#define STATUS_DONE     BIT(0)
#define STATUS_STALL    BIT(1)
#define STATUS_OTW      BIT(2)
#define STATUS_DRV_ERR  BIT(3)
#define STATUS_SPI_ERR  BIT(4)

/* ==== 运动阶段标志 ==== */
#define STAGE_ACCEL     BIT(0)
#define STAGE_CRUISE    BIT(1)
#define STAGE_DECEL     BIT(2)
#define STAGE_HOME_WAIT BIT(3)
#define STAGE_LOCKED    BIT(4)

/* ==== 类型定义 ==== */
typedef struct {
    int32_t  value;
    uint8_t  cmd;
    uint8_t  motor;
    uint8_t  param;
    uint8_t  checksum;
} CAN_CMD_T;

/* ==== 接口: 驱动原语 ==== */
void    CAN_Init(void);
uint8_t CAN_Send(uint32_t id, uint8_t *data);
uint8_t CAN_SendWait(uint32_t id, uint8_t *data);

/* ==== 接口: 协议分发与反馈组装 ==== */
void    CAN_Process(void);
uint8_t CAN_SendMotionFeedback(uint8_t motor, int32_t pos,
                                   uint8_t status, uint8_t stage);
uint8_t CAN_SendPidFeedback(uint8_t motor, uint8_t pid_type, int32_t value);

#endif /* CAN_H */
