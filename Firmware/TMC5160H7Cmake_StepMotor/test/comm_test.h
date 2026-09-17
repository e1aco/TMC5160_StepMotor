/*****************************************************************************
 * @文件: comm_test.h
 * @作者: cl
 * @日期: 2026-09-08
 * @版本: v1.0
 * @说明: CAN/SPI 通讯自检编排层（上电 SPI 寄存器回读 + CAN 收发计数）
 * @依据: TMC5160A_Datasheet_Rev1.14_ch04/ch06 (SPI/GSTAT/DRVSTATUS)
 *        + require.md CAN 协议 0x1AA55F42/0x1AA55F43
 * @依赖: drv/tmc5160_drv, drv/uart_dbg, drv/rtt_dbg, usr/tmc5160_usr, usr/queue
 * @分层: 测试层 (test) — 调 drv+usr 串流程，零寄存器直碰
 ****************************************************************************/
#ifndef COMM_TEST_H
#define COMM_TEST_H

#include <stdint.h>

void COMM_Test_SPI(void);
void COMM_Test_SPI_Soak(void);
void COMM_Test_SPI_Quiet(void);
void COMM_Test_ManualRun(void);
void COMM_Test_ProdRun(void);
void COMM_Test_ProdRun_U1(void);
void COMM_Test_OnCanRxISR(void);
void COMM_Test_CAN_Heartbeat(void);
uint32_t COMM_Test_GetCanRxCount(void);

#endif /* COMM_TEST_H */
