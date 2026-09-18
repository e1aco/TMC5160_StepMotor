/*****************************************************************************
 * @文件: cl_config.h
 * @作者: cl
 * @日期: 2026-09-09
 * @版本: v1.0
 * @说明: 工程编译期特性开关（调试通道唯一门控源 + 上电测试钩子总表）
 *        开关分两类——通道类(DBG_OPEN/UART_DBG/RTT_DBG)：控制调试输出通断；
 *        测试类(SPI_SOAK/SPI_QUIET/SPI_MANUAL)：控制上电是否自动跑对应测试
 *        关闭时接口层用空宏剥除调用（编译期零开销，实参不求值）
 * @消费: 通道类经 drv/uart_dbg.h、drv/rtt_dbg.h include 生效；
 *        测试类由 Core/Src/main.c 上电测试段(#if)消费
 * @现状: UART 开（115200 回传主通道）；RTT 关（用户 2026-09-09 决定，
 *        无 J-Link 在位；接口与实现保留，重开只需 RTT_DBG→1）；
 *        测试类全关（生产版，见各宏注记）
 ****************************************************************************/
#ifndef CL_CONFIG_H
#define CL_CONFIG_H

/* 总闸(DBG_OPEN)：1=按通道开关输出调试信息；
 * 0=无视通道配置，全部调试输出关闭（下方 #if 强制两通道为 0）
 * 消费: drv/uart_dbg.h、drv/rtt_dbg.h（编译期空宏剥除） */
#define DBG_OPEN    1

/* 上电测试开关 SPI_SOAK：SPI 位保真浸泡诊断
 * 背景(2026-09-10 电机排查轮)：验证 H7 6.45Mbps 相对 F407 2.625Mbps
 * 是否过快致位翻转（ch04 §4.3 裕量仅 1%，详见 test/comm_test.c 头注）
 * 取值: 1=上电自动跑（SOAK1 静置 2000 帧写读回显 + 运转浸泡）；
 *       0=生产版关闭，不跑
 * 消费: Core/Src/main.c 上电测试段(#if SPI_SOAK→COMM_Test_SPI_Soak())
 * 实现: test/comm_test.c COMM_Test_SPI_Soak()
 * 现状=0：2026-09-10 验收（结论=降速论证伪，见 .cl/memory/ 与 log.md） */
#define SPI_SOAK    0

/* 上电测试开关 SPI_QUIET：静音模式(StealthChop)电机运行测试
 * （2026-09-10 用户定案）
 * 取值: 1=上电直调 U2（GCONF.en_pwm_mode=1，速度模式跑 1s），
 *       判据=转+无故障（|enc|≥8940 且 s2gb=0 且 drv_err=0，用户定）；
 *       0=生产版关闭，不跑
 * 消费: Core/Src/main.c 上电测试段(#if SPI_QUIET→COMM_Test_SPI_Quiet())
 * 实现: test/comm_test.c COMM_Test_SPI_Quiet()
 * 现状=0：2026-09-10 实测 FAIL(enc=13/ds=40146000: stealth=1 s2vsb=1
 * olb=1 cs=20) → 静音下 s2gb 消失但转报 s2vsb+olb, 电机仍 100% 丢步 →
 * B 相故障与斩波模式无关（根因后定为 RS-B Kelvin 跳线错位，见 log.md） */
#define SPI_QUIET   0

/* 上电测试开关 SPI_MANUAL：手册(ch22/ch23)基线配置+一整圈定位运转
 * （2026-09-10 用户定案）
 * 取值: 1=上电按 ch23 初始化示例重写斩波/斜坡并定位 +51200µsteps
 *       （一整圈），判据=|enc|≥25600 且 s2g/s2vs 全 0 且 drv_err=0；
 *       0=生产版关闭，不跑
 * 消费: Core/Src/main.c 上电测试段(#if SPI_MANUAL→COMM_Test_ManualRun())
 * 实现: test/comm_test.c COMM_Test_ManualRun()
 * 基线=CHOPCONF 0x000100C5(TBL=36clk/HEND=1) + 驱动配置寄存器复位缺省(medium)
 * + 保留 SHORT_CONF/IHOLD_IRUN/GCONF 现值（约束3: memory 实测校准优先）
 * 现状=0：2026-09-10 实测 FAIL(reached=1 act=33321 enc=8717
 * ds=D1140067: s2gb=1 olb=1 gs=02) → 手册基线仍锁桥,
 * 但电机首次实际位移 8717µsteps(17%圈)后锁 → 软件斩波/drvs 维度
 * 全部穷尽, 收敛硬件（根因后定为 RS-B Kelvin 跳线错位，见 log.md） */
#define SPI_MANUAL  0

/* 上电测试开关 SPI_PRODRUN：生产配置 SpreadCycle 整圈运行验证
 * （2026-09-12 用户定案：非静音模式 + 运转全程无错误标志置位）
 * 取值: 1=上电以生产 init 配置(GCONF=0x00/CHOPCONF=0x000181C5/IRUN=20,
 *       不重写斩波, 仅 ApplyProfile(4)常规运动参数)定位 +51200µsteps；
 *       0=生产版关闭，不跑
 * 消费: Core/Src/main.c 上电测试段(#if SPI_PRODRUN→COMM_Test_ProdRun())
 * 实现: test/comm_test.c COMM_Test_ProdRun()
 * 判据: reached=1 且 |enc|≥25600 且 s2g/s2vs/ol 全 0 且 drv_err=0 */
#define SPI_PRODRUN 0

/* 上电测试开关 SPI_PRODRUN_U1：U1 生产配置 SpreadCycle 整圈运行验证
 * （2026-09-14 新增：U1 已焊+供电正常，init 双芯同配已就绪）
 * 取值: 1=上电以生产 init 配置定位 U1 +51200µsteps（流程/判据对标 PRODRUN，
 *       实现见 test/comm_test.c COMM_Test_ProdRun_U1()）；
 *       0=生产版关闭，不跑
 * 消费: Core/Src/main.c 上电测试段(#if SPI_PRODRUN_U1→COMM_Test_ProdRun_U1())
 * 判据: reached=1 且 |enc|≥25600 且 s2g/s2vs/ol 全 0 且 drv_err=0 */
#define SPI_PRODRUN_U1 0

#if DBG_OPEN
/* 通道开关 UART_DBG：1=USART1(PB14/PB15, 115200)调试输出开（回传主通道）；
 * 0=该通道关闭。仅 DBG_OPEN=1 时本值有效 */
#define UART_DBG    0   /* USART1 调试输出 —— 已关闭（2026-09-18 切 RTT） */
/* 通道开关 RTT_DBG：1=SEGGER RTT 输出开；0=关。
 * 2026-09-18: J-Link 就位，开启 RTT 替代 USART1 调试回传。
 * 仅 DBG_OPEN=1 时本值有效 */
#define RTT_DBG     1   /* SEGGER RTT 输出 (J-Link RTT Viewer) */
#else
/* 总闸关闭时两通道强制为 0（与上方取值无关） */
#define UART_DBG    0
#define RTT_DBG     0
#endif

#endif /* CL_CONFIG_H */
