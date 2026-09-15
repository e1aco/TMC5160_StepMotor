/*****************************************************************************
 * @文件: can.c
 * @作者: cl
 * @日期: 2026-08-25
 * @版本: v1.0
 * @说明: CAN 驱动层 = 硬件原语 + 协议解析/反馈组装（原 can_usr.c 并入，
 *        用户 2026-09-09 裁决；其中调用 app/motor_ctrl、app/closed_loop 为
 *        反向依赖，已按该裁决在 can.h 头注豁免登记）
 * @来源: 自 TMC5160_StepMotor(F407 bxCAN) 移植归一，API 重写为 H7 FDCAN HAL
 * @变更点: bxCAN FilterBank/AddTxMessage/GetTxMailboxesFreeLevel →
 *          FDCAN Filter/AddMessageToTxFifoQ/GetTxFifoFreeLevel；
 *          RX 中断回调 HAL_CAN_RxFifo0MsgPendingCallback →
 *          HAL_FDCAN_RxFifo0Callback（实现在 stm32h7xx_it.c）
 * @注意: MessageRAM 分配在 Core/Src/fdcan.c MX_FDCAN2_Init 中完成
 *        （ExtFiltersNbr=1 / RxFifo0ElmtsNbr=8 / TxFifoQueueElmtsNbr=3），
 *        CubeMX 重生成 .ioc 时需保留该配置
 * @平台: STM32H750VBT6 (FDCAN2, 经典帧 500kbit/s)
 * 依据 require.md 时钟树: FDCANCLK=8MHz(HSE 直接), Prescaler=1, Seg1=13,
 *   Seg2=2 → (1+13+2)TQ=16TQ → 8MHz/16=500kbit/s
 * @依赖: HAL_FDCAN
 ****************************************************************************/
#include "drv/can.h"
#include "algo/queue.h"
#include "app/motor_ctrl.h"
#include "app/closed_loop.h"
#include "app/homing.h"

/* TX FIFO 深度，与 fdcan.c TxFifoQueueElmtsNbr 保持一致 */
#define CAN_TX_FIFO_DEPTH  3

/* 到位终态跟踪：ACK 只表示收到，bit0 强制清零；目标到达后补发终态帧
 * 超时推导: 最慢参数组 VMAX=5000 → 整圈 51200/5000≈10.2s + 加减速，
 * 取 30s(约×3 裕量，待实测确认) */
#define COMPLETION_TIMEOUT_MS  30000U

typedef struct {
    uint8_t active;
    uint8_t motor;
    int32_t target;
    uint32_t deadline;
} CAN_PENDING_T;

static CAN_PENDING_T s_pending[2];

/*******************************************************************************************
 *  内部函数部分
********************************************************************************************/

/**
 * @输入 无
 * @输出 无
 * @说明 配置扩展帧过滤器，仅接收 ID 0x1AA55F42 到 RX FIFO0
 */
static void S_FilterConfig(void)
{
    FDCAN_FilterTypeDef filter;

    filter.IdType = FDCAN_EXTENDED_ID;
    filter.FilterIndex = 0;
    filter.FilterID1 = CAN_RX_ID;
    filter.FilterID2 = 0x1FFFFFFF;             /* 掩码: 29位全比对,精确匹配 RX_ID */
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;

    if (HAL_FDCAN_ConfigFilter(&hfdcan2, &filter) != HAL_OK)
    {
        Error_Handler();
    }

    /* 未匹配帧默认丢弃（不进 FIFO），拒绝远程帧 */
    if (HAL_FDCAN_ConfigGlobalFilter(&hfdcan2, FDCAN_REJECT, FDCAN_REJECT,
                                     FDCAN_FILTER_REMOTE,
                                     FDCAN_FILTER_REMOTE) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @输入 id: 发送帧 ID(扩展帧); data: 8字节数据指针
 * @输出 0=成功, 1=发送失败
 * @说明 封装 HAL_FDCAN_AddMessageToTxFifoQ，非阻塞发送
 */
static uint8_t S_SendMsg(uint32_t id, uint8_t *data)
{
    FDCAN_TxHeaderTypeDef tx_header;

    tx_header.Identifier = id;
    tx_header.IdType = FDCAN_EXTENDED_ID;
    tx_header.TxFrameType = FDCAN_DATA_FRAME;
    tx_header.DataLength = FDCAN_DLC_BYTES_8;
    tx_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx_header.BitRateSwitch = FDCAN_BRS_OFF;
    tx_header.FDFormat = FDCAN_CLASSIC_CAN;
    tx_header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    tx_header.MessageMarker = 0;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan2, &tx_header, data) != HAL_OK)
    {
        return 1;
    }
    return 0;
}

/**
 * @输入 无
 * @输出 无
 * @说明 启动 CAN 并使能 RX FIFO0 新报文中断（IT0 中断线）
 */
static void S_StartRx(void)
{
    if (HAL_FDCAN_Start(&hfdcan2) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_FDCAN_ActivateNotification(&hfdcan2,
                                       FDCAN_IT_RX_FIFO0_NEW_MESSAGE |
                                       FDCAN_IT_RX_FIFO0_MESSAGE_LOST,
                                       0U) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @输入 data: 4字节小端数据
 * @输出 int32_t: 解析后的有符号值
 */
static int32_t S_BytesToInt32(uint8_t *data)
{
    int32_t val;

    val  = (int32_t)data[0];
    val |= (int32_t)data[1] << 8;
    val |= (int32_t)data[2] << 16;
    val |= (int32_t)data[3] << 24;
    return val;
}

/**
 * @输入 val: 32位值; data: 输出缓冲区(4字节)
 * @输出 无
 * @说明 int32 转 4 字节小端
 */
static void S_Int32ToBytes(int32_t val, uint8_t *data)
{
    data[0] = (uint8_t)(val & 0xFF);
    data[1] = (uint8_t)((val >> 8) & 0xFF);
    data[2] = (uint8_t)((val >> 16) & 0xFF);
    data[3] = (uint8_t)((val >> 24) & 0xFF);
}

/**
 * @输入 data: 8字节原始数据
 * @输出 uint8_t: 校验和
 * @说明 前 7 字节累加取低 8 位
 */
static uint8_t S_CalcChecksum(uint8_t *data)
{
    uint8_t sum = 0;
    uint8_t i;

    for (i = 0; i < 7; i++)
    {
        sum += data[i];
    }
    return sum;
}

/*******************************************************************************************
 *  驱动函数部分
********************************************************************************************/

/**
 * @输入 无
 * @输出 无
 * @说明 CAN 驱动初始化：过滤器 + 启动中断接收
 */
void DRV_CAN_Init(void)
{
    S_FilterConfig();
    S_StartRx();
}

/**
 * @输入 id: 发送帧 ID(扩展帧); data: 8字节数据指针
 * @输出 0=成功, 1=发送失败
 * @说明 对外发送接口
 */
uint8_t DRV_CAN_Send(uint32_t id, uint8_t *data)
{
    return S_SendMsg(id, data);
}

/**
 * @输入 id: 发送帧 ID(扩展帧); data: 8字节数据指针
 * @输出 0=成功, 1=发送失败
 * @说明 串行化发送：等全部 TX FIFO 深度空闲（上一帧已完整发出）再发下一帧。
 *        仅等 1 个空闲时，同一 ID 连续多帧会被 CAN 核跨槽乱序仲裁，
 *        监控端按行还原时会错位（源工程实测遥测串扰根因）
 */
uint8_t DRV_CAN_SendWait(uint32_t id, uint8_t *data)
{
    uint32_t timeout = 100000;

    while ((CAN_TX_FIFO_DEPTH != HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan2)) &&
           (0 != timeout))
    {
        timeout--;
    }
    return S_SendMsg(id, data);
}

/*******************************************************************************************
 *  用户函数部分
********************************************************************************************/

/**
 * @输入 无
 * @输出 无
 * @说明 CAN 协议模块初始化
 */
void USR_CAN_Init(void)
{
}

/**
 * @输入 motor: 电机编号(U1/U2/ALL)
 * @输出 无
 * @说明 取消该电机到位跟踪（回零/急停接管运动后旧目标作废）
 */
static void S_Disarm(uint8_t motor)
{
    uint8_t i;

    for (i = 0; i < 2U; i++)
    {
        if (0U != s_pending[i].active &&
            (s_pending[i].motor == motor || MOTOR_CTRL_ALL == motor))
        {
            s_pending[i].active = 0;
        }
    }
}

/**
 * @输入 motor: 电机编号(U1/U2/ALL); target: 本次指令目标绝对位置
 * @输出 无
 * @说明 登记到位跟踪（ALL 登记两路）；ACK 由调用方发（bit0 已清）
 */
static void S_ArmCompletion(uint8_t motor, int32_t t1, int32_t t2)
{
    uint32_t now = HAL_GetTick();

    if (MOTOR_CTRL_U1 == motor || MOTOR_CTRL_ALL == motor)
    {
        s_pending[0].active = 1;
        s_pending[0].motor = MOTOR_CTRL_U1;
        s_pending[0].target = t1;
        s_pending[0].deadline = now + COMPLETION_TIMEOUT_MS;
    }
    if (MOTOR_CTRL_U2 == motor || MOTOR_CTRL_ALL == motor)
    {
        s_pending[1].active = 1;
        s_pending[1].motor = MOTOR_CTRL_U2;
        s_pending[1].target = t2;
        s_pending[1].deadline = now + COMPLETION_TIMEOUT_MS;
    }
}

/**
 * @输入 无
 * @输出 无
 * @说明 主循环每圈调用：XACTUAL==目标且速度归零 → 发终态实测帧；
 *        超时也发一帧当时快照（bit0 多半为 0），上位机据此判超时
 */
static void S_CheckCompletion(void)
{
    uint8_t i;

    for (i = 0; i < 2U; i++)
    {
        if (0U == s_pending[i].active)
        {
            continue;
        }
        {
            TMC5160_CHIP_T *chip = USR_MOTOR_GetChip(s_pending[i].motor);
            int32_t pos;
            int32_t vel;
            uint32_t now = HAL_GetTick();
            uint8_t arrived;
            uint8_t expired;

            if ((void *)0 == chip)
            {
                s_pending[i].active = 0;
                continue;
            }
            pos = USR_TMC5160_GetPosition(chip);
            vel = USR_TMC5160_GetVelocity(chip);
            /* 到达：位置吻合且速度归零；超时：回绕安全的差值判 */
            arrived = (pos == s_pending[i].target && 0 == vel);
            expired = (0 <= (int32_t)(now - s_pending[i].deadline));
            if (0U != arrived || 0U != expired)
            {
                uint8_t status = USR_MOTOR_GetStatus(s_pending[i].motor);
                uint8_t stage = USR_MOTOR_GetStage(s_pending[i].motor);
                int32_t dev = USR_MOTOR_GetEncoderPosition(s_pending[i].motor);

                s_pending[i].active = 0;
                USR_CAN_SendMotionFeedback(s_pending[i].motor, dev,
                                           status, stage);
            }
        }
    }
}

/**
 * @输入 无
 * @输出 无
 * @说明 主循环调用，从队列取命令并解析执行
 */
void USR_CAN_Process(void)
{
    uint8_t *cmd_data;
    CAN_CMD_T cmd;

    /* 到位终态跟踪（每圈必跑，与队列空否无关） */
    S_CheckCompletion();

    if (QUEUE_IsEmpty(&g_queue_st))
    {
        return;
    }

    cmd_data = (uint8_t *)QUEUE_First(&g_queue_st);
    if (NULL == cmd_data)
    {
        return;
    }

    /* 解析命令（中断中已完成校验） */
    cmd.value    = S_BytesToInt32(&cmd_data[0]);
    cmd.cmd      = cmd_data[4];
    cmd.motor    = cmd_data[5];
    cmd.param    = cmd_data[6];
    cmd.checksum = cmd_data[7];

    /* 出队 */
    QUEUE_Delete(&g_queue_st);

    /* 命令分发 */
    switch (cmd.cmd)
    {
    case CMD_ABS_POS:
    {
        int32_t dev = USR_MOTOR_GetEncoderPosition(cmd.motor);
        uint8_t status = USR_MOTOR_GetStatus(cmd.motor);
        uint8_t stage = USR_MOTOR_GetStage(cmd.motor);

        USR_MOTOR_ApplyProfile(cmd.motor, cmd.param);
        USR_MOTOR_MoveTo(cmd.motor, cmd.value);
        /* ACK 清 bit0（旧到位残留），到位后补终态帧 */
        S_ArmCompletion(cmd.motor, cmd.value, cmd.value);
        USR_CAN_SendMotionFeedback(cmd.motor, dev,
                                   (uint8_t)(status & (~STATUS_DONE)), stage);
        break;
    }

    case CMD_REL_CW:
    {
        int32_t dev = USR_MOTOR_GetEncoderPosition(cmd.motor);
        uint8_t status = USR_MOTOR_GetStatus(cmd.motor);
        uint8_t stage = USR_MOTOR_GetStage(cmd.motor);
        int32_t t1 = USR_MOTOR_GetPosition(MOTOR_CTRL_U1) + cmd.value;
        int32_t t2 = USR_MOTOR_GetPosition(MOTOR_CTRL_U2) + cmd.value;

        USR_MOTOR_ApplyProfile(cmd.motor, cmd.param);
        USR_MOTOR_MoveBy(cmd.motor, cmd.value);
        S_ArmCompletion(cmd.motor, t1, t2);
        USR_CAN_SendMotionFeedback(cmd.motor, dev,
                                   (uint8_t)(status & (~STATUS_DONE)), stage);
        break;
    }

    case CMD_REL_CCW:
    {
        int32_t dev = USR_MOTOR_GetEncoderPosition(cmd.motor);
        uint8_t status = USR_MOTOR_GetStatus(cmd.motor);
        uint8_t stage = USR_MOTOR_GetStage(cmd.motor);
        int32_t t1 = USR_MOTOR_GetPosition(MOTOR_CTRL_U1) - cmd.value;
        int32_t t2 = USR_MOTOR_GetPosition(MOTOR_CTRL_U2) - cmd.value;

        USR_MOTOR_ApplyProfile(cmd.motor, cmd.param);
        USR_MOTOR_MoveBy(cmd.motor, -cmd.value);
        S_ArmCompletion(cmd.motor, t1, t2);
        USR_CAN_SendMotionFeedback(cmd.motor, dev,
                                   (uint8_t)(status & (~STATUS_DONE)), stage);
        break;
    }

    case CMD_VELOCITY:
    {
        int32_t dev = USR_MOTOR_GetEncoderPosition(cmd.motor);
        uint8_t status = USR_MOTOR_GetStatus(cmd.motor);
        uint8_t stage = USR_MOTOR_GetStage(cmd.motor);

        USR_MOTOR_ApplyProfile(cmd.motor, cmd.param);
        USR_MOTOR_SetVelocity(cmd.motor, cmd.value);
        USR_CAN_SendMotionFeedback(cmd.motor, dev,
                                   (uint8_t)(status & (~STATUS_DONE)), stage);
        break;
    }

    case CMD_STOP:
    {
        int32_t dev = USR_MOTOR_GetEncoderPosition(cmd.motor);
        uint8_t status = USR_MOTOR_GetStatus(cmd.motor);
        int32_t t1 = USR_MOTOR_GetPosition(MOTOR_CTRL_U1);
        int32_t t2 = USR_MOTOR_GetPosition(MOTOR_CTRL_U2);

        USR_MOTOR_Stop(cmd.motor);
        /* 停止即以当前位置为目标，静止后补终态帧（bit0 真实） */
        S_ArmCompletion(cmd.motor, t1, t2);
        USR_CAN_SendMotionFeedback(cmd.motor, dev,
                                   (uint8_t)(status & (~STATUS_DONE)), 0);
        break;
    }

    case CMD_PID_ADJUST:
        /* PID 调参（暂未实现） */
        break;

    case CMD_CL_ENABLE:
        USR_CLOSEDLOOP_Enable(cmd.motor);
        USR_CAN_SendMotionFeedback(cmd.motor, 0, 0, 0);
        break;

    case CMD_CL_DISABLE:
        USR_CLOSEDLOOP_Disable(cmd.motor);
        USR_CAN_SendMotionFeedback(cmd.motor, 0, 0, 0);
        break;

    case CMD_HOME:
    {
        /* 无编码器回零：value bit0=方向(0负/1正)，param 预留；
         * 仅 U1/U2 单电机（ALL/非法拒绝）；异步执行，完成由 Tick 发反馈 */
        uint8_t dir = (0UL != ((uint32_t)cmd.value & 0x01UL)) ?
                      HOME_DIR_POSITIVE : HOME_DIR_NEGATIVE;

        S_Disarm(cmd.motor);
        if (0U == USR_HOME_Start(cmd.motor, dir))
        {
            USR_CAN_SendMotionFeedback(cmd.motor, 0, 0, 0);
        }
        break;
    }

    case CMD_ESTOP:
    {
        /* 急停：关断功率级自由停车（不锁轴）；value/param 忽略；
         * 位置已丢失，再运动前须 0x09 重回零（运动命令会自动重使能） */
        S_Disarm(cmd.motor);
        if (MOTOR_CTRL_ALL == cmd.motor)
        {
            USR_TMC5160_EStop(TMC5160_CHIP_1);
            USR_TMC5160_EStop(TMC5160_CHIP_2);
            USR_CAN_SendMotionFeedback(cmd.motor, 0, 0, 0);
        }
        else if (MOTOR_CTRL_U1 == cmd.motor || MOTOR_CTRL_U2 == cmd.motor)
        {
            USR_TMC5160_EStop(cmd.motor);
            USR_CAN_SendMotionFeedback(cmd.motor, 0, 0, 0);
        }
        break;
    }

    default:
        break;
    }
}

/**
 * @输入 motor: 电机选择; pos: 编码器实际位姿(X_ENC); status: 状态标志; stage: 运动阶段(已废弃,恒0)
 * @输出 0=成功, 1=发送失败
 * @说明 发送运动反馈帧 (ID: 0x1AA55F43)；byte[6] 保护状态:
 *        bit0=OTPW, bit1=OT, bit2=drv_err(GSTAT), bit3=S2GA, bit4=S2GB,
 *        bit5=S2VSA, bit6=S2VSB, bit7=失步(ENC_STATUS.deviation_warn)
 * 依据 .cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch06.p033.md: DRV_STATUS/GSTAT 位定义
 */
uint8_t USR_CAN_SendMotionFeedback(uint8_t motor, int32_t pos,
                                   uint8_t status, uint8_t stage)
{
    uint8_t tx_data[8];
    uint8_t protect_flags = 0;
    TMC5160_CHIP_T *chip = USR_MOTOR_GetChip(motor);

    (void)stage;
    if (chip != NULL)
    {
        uint32_t drv_status = USR_TMC5160_GetDrvStatus(chip);
        uint32_t gstat = USR_TMC5160_GetGStat(chip);
        if (drv_status & (1u << 26)) protect_flags |= 0x01; /* OTPW  预过温 */
        if (drv_status & (1u << 25)) protect_flags |= 0x02; /* OT    过温关断 */
        if (gstat & (1u << 1))       protect_flags |= 0x04; /* drv_err 驱动错误 */
        if (drv_status & (1u << 27)) protect_flags |= 0x08; /* S2GA  对地短路 A 相 */
        if (drv_status & (1u << 28)) protect_flags |= 0x10; /* S2GB  对地短路 B 相 */
        if (drv_status & (1u << 12)) protect_flags |= 0x20; /* S2VSA 对电源短路 A 相 */
        if (drv_status & (1u << 13)) protect_flags |= 0x40; /* S2VSB 对电源短路 B 相 */
        if (status & 0x02)           protect_flags |= 0x80; /* 失步 (编码器偏差) */
    }

    S_Int32ToBytes(pos, &tx_data[0]);
    tx_data[4] = status;
    tx_data[5] = motor;
    tx_data[6] = protect_flags;
    tx_data[7] = S_CalcChecksum(tx_data);

    return DRV_CAN_Send(CAN_TX_ID, tx_data);
}

/**
 * @输入 motor: 电机选择; pid_type: PID参数类型; value: 参数值
 * @输出 0=成功, 1=发送失败
 * @说明 发送调参反馈帧 (ID: 0x1AA55F43)
 */
uint8_t USR_CAN_SendPidFeedback(uint8_t motor, uint8_t pid_type, int32_t value)
{
    uint8_t tx_data[8];

    S_Int32ToBytes(value, &tx_data[0]);
    tx_data[4] = pid_type;
    tx_data[5] = motor;
    tx_data[6] = CMD_PID_ADJUST;
    tx_data[7] = S_CalcChecksum(tx_data);

    return DRV_CAN_Send(CAN_TX_ID, tx_data);
}
