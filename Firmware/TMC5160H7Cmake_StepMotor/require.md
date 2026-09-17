# 硬件资源池 (Hardware Pool)
芯片: STM32H750VBT6 (LQFP100, Cortex-M7)
晶振频率: 8 MHz (HSE, PH0/PH1)
关键器件:
  - 驱动芯片: TMC5160
  - 外部 MOS: AOD4126
  - 采样电阻 RS: 0.05R
  - 步进电机: 57CME13
        1. 相数 2
        2. 保持力矩（N.m） 1.3
        3. 电机机座（mm） 57
        4. 步距角 1.8
        5. 电感（mH） 1.6
        6. 建议使用电压（VDC） 36V
        7. 额定电流（A） 4
        8. 电阻（Ω） 0.42
        9. 转子惯量（Kg·cm²）  0.3
        10. 电机重量（KG） 0.8

# 引脚固定映射表 (Pin Mapping)

| 功能网标 | 引脚号 | 外设功能 | 说明 |
| :------- | :----- | :------- | :--- |
| HSE_IN | PH0 | RCC_OSC_IN | 8MHz 晶振输入 |
| HSE_OUT | PH1 | RCC_OSC_OUT | 8MHz 晶振输出 |
| LED_MCU | PA4 | GPIO_Output | 用户指示灯, 默认高 |
| U1_DRV_ENN | PA10 | GPIO_Output | TMC5160 #1 驱动使能(低有效), 默认高 |
| U1_SPI_SCN | PA11 | GPIO_Output | TMC5160 #1 SPI 片选(低有效), 默认高 |
| SWDIO | PA13 | DEBUG_JTMS | SWD 调试 |
| SWCLK | PA14 | DEBUG_JTCK | SWD 调试 |
| U2_DRV_ENN | PA15 | GPIO_Output | TMC5160 #2 驱动使能(低有效), 默认高 |
| FDCAN2_RX | PB12 | FDCAN2_RX | CAN 总线接收 |
| FDCAN2_TX | PB13 | FDCAN2_TX | CAN 总线发送 |
| USART1_TX | PB14 | USART1_TX | 调试串口发送 |
| USART1_RX | PB15 | USART1_RX | 调试串口接收 |
| SPI3_SCK | PC10 | SPI3_SCK | TMC5160 SPI 时钟 |
| SPI3_MISO | PC11 | SPI3_MISO | TMC5160 SPI 数据输入 |
| SPI3_MOSI | PC12 | SPI3_MOSI | TMC5160 SPI 数据输出 |
| U2_SPI_SCN | PD3 | GPIO_Output | TMC5160 #2 SPI 片选(低有效), 默认高 |
| TMC_CLK | PD14 | TIM4_CH3 | TMC5160 外部时钟(PWM 输出) |

# 工具链池 (Toolchain Pool)
构建体系: cmake
CMake 工程: 工程根=本目录; target=TMC5160H7_StepMotor; 工具链文件=cmake/gcc-arm-none-eabi.cmake（cmake_build.py 自动探测）; 构建目录=build_cl（与 CLion 的 build/ 隔离）
编译指令: python tools/cmake_build.py <工程根> [--define 宏]   # RESULT 行为真相判据，产物 hex/elf 落 build_cl/（唯一权威 details/build.md）
IDE/编译器: CMake + GCC (arm-none-eabi-gcc) + Ninja（STM32CubeCLT 提供）；CLion 打开同一工程用其自有 Profile
芯片型号: STM32H750VBT6
优化等级: 6 (Os — optimize for size)
FPU: 单精度 + 双精度 (Cortex-M7 DP-FPU)
CubeMX 版本: 6.17.0, FW_H7 V1.13.0

# 调试与烧录 (Debug/Flash)
调试器: ST-Link (SWD, PA13/PA14)
调试回传接口: USART1 (PB14/PB15, 速率待确认) 或 FDCAN2 (PB12/PB13, 500kbps)
烧录工具: ST-Link + OpenOCD
烧录指令: python tools/openocd_flash.py --flash <工程根>\build_cl\TMC5160H7_StepMotor.hex --target stm32h7x
复位/运行: python tools/openocd_flash.py --reset --target stm32h7x
> 残留进程兜底: python tools/openocd_flash.py --kill

# 时钟树摘要 (Clock Tree, 从 .ioc 提取)
- PLL1: HSE 8MHz → DIVM1=1 → VCO1=960MHz → DIVP1=2 → SYSCLK=480MHz
- HCLK: 240MHz (HPRE=DIV2)
- APB1/APB2/APB3/APB4: 120MHz (均 DIV2)
- TIM4CLK: 240MHz (APB1×2, APB1=120MHz)
- SPI3CLK: 103.2MHz (PLL3Q: HSE8/5×129/2, 2026-09-09 CubeMX 改; 原 CLKP/HSI 64MHz), 实际波特率 6.45Mbit/s prescaler=16
- USART1CLK: 64MHz (HSI 源)
- FDCANCLK: 8MHz (HSE 直接)
- ADCCLK: 16.125MHz

# SPI3 配置 (TMC5160 通信)
模式: Master Full Duplex
波特率: 6.45 Mbit/s (SPI3 kernel=PLL3Q 103.2MHz, prescaler=16)（2026-09-09 CubeMX 改配置+实测通过；≤fCLK/2=7.5MHz 规格内）
极性: CPOL=HIGH (Idle HIGH)
相位: CPHA=2EDGE (Sample on 2nd edge)
数据宽度: 8-bit

# FDCAN2 配置
波特率: 500 kbit/s (Prescaler=1, Seg1=13, Seg2=2, 16TQ @8MHz HSE)
自动重发: ENABLE
MessageRAM 分配 (移植适配, CubeMX 重生成需保留): ExtFiltersNbr=1 / RxFifo0ElmtsNbr=8 / TxFifoQueueElmtsNbr=3

# CAN 命令协议 (自 TMC5160_StepMotor 移植提取) — 来源: 源工程 can_drv/can_usr 实现
> 收发模型：经典帧 / 扩展 ID / 8 字节 / 500kbit/s；硬件过滤器只收命令 ID 进 RX FIFO0，其余丢弃；
> 中断入队 → 主循环 `USR_CAN_Process()` 逐条分发（实现见 module/drv/can.c）。
> 校验和：byte7 = (byte0+…+byte6) & 0xFF（收发同算法，不符的帧丢弃）。

## 命令帧 0x1AA55F42 (上位机 → MCU)

| byte | 字段 | 说明 |
| :--- | :--- | :--- |
| 0-3 | value | int32 小端；含义随 cmd 变（见下表） |
| 4 | cmd | 命令码 0x01~0x0A（见下表） |
| 5 | motor | 0x01=U1 / 0x02=U2 / 0x06=全部（0x09 回零仅接受 U1/U2） |
| 6 | param | 运动参数组 ID 1~5（cmd 0x06 时为 PID 参数类型；0x09 时预留忽略） |
| 7 | checksum | 见上 |

| cmd | 名称 | value 含义 | 执行动作 | 反馈时机 |
| :--- | :--- | :--- | :--- | :--- |
| 0x01 | 绝对定位 | 目标绝对位置 µsteps | 按 param 组运动到 value | ACK（bit0=0）+ 到位后补终态帧 |
| 0x02 | 相对顺时针 | 偏移量 µsteps（正数） | 从当前位置 +value | 同上 |
| 0x03 | 相对逆时针 | 偏移量 µsteps（正数） | 从当前位置 −value | 同上 |
| 0x04 | 速度模式 | 目标速度（符号=方向，+正/−反） | 持续旋转 | 收到即回一帧（无目标，无终态） |
| 0x05 | 停止 | 忽略 | 切定位模式锁轴 | ACK（bit0=0）+ 静止后补终态帧 |
| 0x06 | PID 调参 | — | 未实现（收到丢弃，无反馈） | 无 |
| 0x07 | 闭环使能 | 忽略 | 使能该电机闭环 | 收到即回 ACK（全 0） |
| 0x08 | 闭环禁用 | 忽略 | 禁用该电机闭环 | 收到即回 ACK（全 0） |
| 0x09 | 无编码器回零 | bit0=方向（0=负向，1=正向），其余位保留 | StallGuard2 碰限位回零（异步，见下） | 启动成功回 ACK，**完成时再发终态帧** |
| 0x0A | 急停 | 忽略 | 拉高 ENN 关功率级，自由停车（不锁轴） | 收到即回 ACK |

> cmd 0x09 回零语义：motor 仅 U1/U2（0x06 拒绝）；忙中/非法参数不回 ACK，上位机可重发；
> 终态反馈 status bit0=到位（成功）/ bit1=失步（失败）；执行见 module/app/homing.c。
>
> cmd 0x0A 急停语义：motor 可为 U1/U2/0x06（全部）；value/param 忽略；
> 急停后位置已丢失——后续运动命令（0x01~0x04/0x09）会自动重使能，但必须先发 0x09 重回零，否则运动基准不可信。

## 反馈帧 0x1AA55F43 (MCU → 上位机)

| byte | 字段 | 说明 |
| :--- | :--- | :--- |
| 0-3 | pos | 编码器位姿 X_ENC，int32 小端（ACK 类反馈填 0） |
| 4 | status | bit0=到位（RAMP_STAT bit9，读后清）/ bit1=失步（ENC deviation_warn）/ bit2=过温（OTPW/OT）/ bit3=驱动错（GSTAT drv_err，读后清）/ bit4=SPI 异常（读回 0xFFFFFFFF/0 判活失败） |
| 5 | motor | 回显命令中的 motor |
| 6 | protect | bit0=OTPW / bit1=OT / bit2=drv_err / bit3=S2GA / bit4=S2GB / bit5=S2VSA / bit6=S2VSB / bit7=失步（编码器偏差） |
| 7 | checksum | 见上 |

> 反馈时机：定位类命令（0x01/0x02/0x03/0x05）先回 ACK（bit0 强制 0，旧到位残留不外露），
> 主循环检到 XACTUAL==目标且速度归零后再发终态帧（bit0 真实）；30s 未到也发一帧当时快照（上位机判超时）；
> 速度模式（0x04）无目标只有即时帧；回零（0x09）ACK + 终态双帧不变。

## 调试通道
- 调试遥测已迁移 J-Link RTT（1Hz 心跳，rtt_dbg 封装），不再占用 CAN 总线

# TIM4 配置
CH3: PWM Generation (TMC_CLK — 为 TMC5160 提供外部时钟)
PWM频率: 15Mhz

# 项目全局目标 (Global Goal) — /cl init 访谈填写；需求锁锚点，禁静默降级
> 由 /cl init「全局目标访谈」填写，AI 生成/调参/验收一律对照本节。每行 = 量化目标（值+单位）+ 验收判据（仪器/工况/数值）+ 来源（你 / AI 提议 / 待实测确定）。

- 一句话定位: 双 TMC5160 独立电机控制，CAN 指令驱动，支持绝对位置/相对位置/速度三种运动模式
- 精度: 0.09°/count（光编 1000PPR ×4=4000cpr；闭环定位精度由编码器+控制算法决定，待调定后实测）
- 速度: 最高 600rpm, 加减速由 TMC5160 内部速度规划（VDCOL/TCOOLTHRS 等），暂不外部干预 | 来源: 你
- 控制周期: 待定（CAN 协议后续定义，位置环周期由 TMC5160 内部扫描）| 来源: 待定
- 负载: 待实测确定（电机 57CME13, 保持力矩 1.3N·m, 转子惯量 0.3kg·cm²）| 来源: 待实测确定
- 接口/通信: CAN 500kbps, 协议后续定义 | 来源: 待定
- 电源: 24VDC（电机建议 36V, 降额使用）| 来源: 你
- 编码器: 光编 1000PPR, ×4正交解码=4000cpr (ENC_CONST=12.8 对齐 51200微步/圈), 用于闭环反馈 | 来源: 你, 实测验证
- 验收判据: 待定（由你确定）| 来源: 你
- 优先级: 待定（由你确定）| 来源: 待定
- 首版范围 (MVP): 待定 | 来源: 待定
- 待实测确定项: 精度指标、负载工况、控制周期、验收判据、优先级、MVP 范围

## 工作流程
两路电机，等待发送指令回零（回零使用无编码器的那种回零模式 使用SPI轮询判断回零完成），两路电机分别回零，一路回零到位后第二路开始回零
（使用CAN来控制回零即可， 检测到一路回零完成后CAN控制第二路回零。）回零完成后上报。 完成回零等待控制运行指令，进行相对或绝对运动，不允许电机的所有模式超过
某个运行位置(0-最大目标值)，会导致堵转和不可控行为，这时上报超出界限。在范围内可以自由运行。此外如何控制两个电机的位置不互相干涉需要如何控制（上位机还是我们
？）暂时待定。
轮子:
1. 上电回零程序 （CAN指令回零（速度+方向）-> 以这个速度和方向进行回零 -> 堵转检测确认零点 -> 向反方向运动几微步防止卡死标定为零点 -> 停止运行）
2. 相对运动，绝对运动，速度模式3个程序 （已有）
3. 堵转，过流保护程序 （在运行中途堵转，则立刻停止并且反向运动几步，（既要保证不易触发，也要保证安全性））
4. 运行中的梯形规划（或线性规划） （已有，但我还不知道逻辑是什么 ， 如何改为S型规划？）
5. 闭环位置控制 （已有，需要调试）
6. CAN通讯 （已有，协议目前感觉没有写的很清楚）
7. SPI读写TMC5160 （已有，待实测读取和写入实际时间）

# 时序测量表 (Timing Budget) — AI 维护，外部实测需人工回填
> 架构级时序预算控制：每个关键时序操作一行，理论值由 AI 按时钟树/datasheet 推导（附依据），实测值按测试方法获取。

## 默认指标（前置配置，人工可修改，AI 生成/重推导条目时按此默认）
- 预算比例: 中断/采样型 = 周期 × 30%（硬上限 50%，可改）
- 绝对型裕量: datasheet 最坏情况 × 2
- 测试周期默认: 软件条目 = 每次变更都测 | 硬件条目 = 仅首次测
- 测试方法词表: 软件(SysTick) / 硬件(Saleae) / 外部(示波器/逻辑分析仪)
- 状态词表: [ ] 待测 / [x] 已测 / [!] 需外部仪器 / [expired] 过期（保留+注明原因）
- 时序测试点 IO: PD14 (TMC_CLK, TIM4_CH3) — 可翻转供 Saleae 捕获
- 探针默认位置: 函数级（ISR 入口/出口），语句级由条目位置列自定义覆盖

## 条目表（AI 按上述默认生成；理论值/实测值记录于此）
| 操作 | 位置/探针位置 | 理论值(推导) | 依据 | 预算/判据 | 测试周期 | 测试方法 | 实测值 | 实测日期 | 状态 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| SPI3 TMC5160 读写周期 | S_SpiWrite/SpiRead 的 SelectChip→DeselectChip 全事务 | 写单事务 6.2us(40bit@6.45M)+寄存器轮询/片选开销；读双报+5us 帧间隔 | SPI3 kernel=PLL3Q 103.2MHz ÷16 = 6.45Mbit/s；TMC5160 约束 fSCK≤fCLK/2=7.5MHz（15MHz 外部时钟, ch04 §4.3）；DWT 基准=CPU=480MHz(OpenOCD 实测 481MHz) | 6.45≤7.5MHz 规格内; 写/读事务达标 | 每次变更都测 | 软件(DWT, 480MHz 基准) | 写 max=4437cyc≈9.2us(含片选); 读 max=11311cyc≈23.6us(含片选) | 2026-09-17 | [x] 宏版 tag0/tag1 实测(寄存器级收发+寄存器级片选)；HAL 旧值(/240 虚高 2×)=写20/读54 → 实~10/~27, 寄存器级约 -13% |
| TIM4 PWM 输出 (TMC_CLK) | PD14 示波器 | (待配置) | TIM4CLK=240MHz, 分频/ARR 待定 | — | 仅首次测 | 硬件(Saleae) | 软件比值: XACTUAL 17881/s=20000×fCLK/2^24 → fCLK≈15.00MHz±0.01% (SOAK r3, 非仪器, 示波器终验仍欠) | 2026-09-10 | [!] |
| USART1 串口回传 | PB14 TX | (待配置) | USART1CLK=64MHz, 波特率待定 | — | 每次变更都测 | 软件(SysTick) | | | [ ] |
| 主循环单圈 | main while(1) | (待测) | 推算 | — | 仅首次测 | 硬件(Saleae) | | | [!] |
| SpreadCycle 斩波频率 | Rsense 两端（示波器） | ~20-25kHz（TOFF=5, TBL=3, fCLK=15MHz 推算） | ch08: f≈1/(4×tOFF), tOFF=(24+32×TOFF)/fCLK | 16-30kHz | 仅首次测 | 硬件(Saleae/示波器) | | | [!] |

# 任务队列 (Task Queue) — AI 自动维护（允许为空）

- [x] 降 TMC_CLK 15→12MHz + SPI 6.45M→3.225M 烧录测试（用户假设"B 下管时间太短自举充不满"；12MHz 在 ch26 推荐带 10-16M 内；SPI /32 ≤ fCLK/2=6M）（起 2026-09-11 | 止 2026-09-11 | 验收 假设否决: 12MHz 轮 FAIL(enc 3072 比 8717 更差)→**用户放弃**→tim.c/spi.c 回退 15MHz/6.45M, 宏版 [TM] 复验写20us/读54us 与 09-09 一致, 时序表已回填[x]）
- [x] 按手册步骤(ch22/ch23)配置驱动器并控制电机旋转（手册基线 init + 定位一整圈 51200µsteps；判据=|X_ENC|≥50% 且 S2/drv_err 全 0；电流按 memory CS=20 实测校准值不取手册通用示例；供电实况=VS 24V + VSA 12V 用户确认）（起 2026-09-10 | 止 2026-09-10 | 验收 **FAIL**: reached=1 act=33321 enc=8717 ds=0xD1140067(s2gb=1 olb=1 stst=1 SG=1 CS=20) gs=02 → 手册基线 CHOPCONF=0x000100C5(TBL=36/HEND=1)+DRV_CONF 复位缺省(medium) 仍锁桥, 但**电机首次实际位移 8717µsteps(17%圈) 后锁**; 至此软件维度穷尽(斩波双模式+TBL/HEND 基线+drvs 0/2/5/7+SHORT_CONF 最低敏) → 收敛硬件: VSA=12V 轨/自举电容/焊桥/B低边管; 生产宏 SPI_MANUAL 已还原 0 重编译 0E/0W）
- [x] 静音模式(StealthChop)测试电机运行（U2 直调 GCONF=0x04 单点 21rpm 1s；判据=enc 位移≥理论 50%(8940) 且 s2gb=0 且 drv_err=0；S2 保持开启；用户定"先测完再定"不切生产默认）（起 2026-09-10 | 止 2026-09-10 | 验收 **FAIL**: act=17801 enc=13 ds=0x40146000(stealth=1 s2vsb=1 olb=1 cs=20, s2gb=0) gs=02(drv_err) ferr=0 → StealthChop 真实生效但电机仍 100% 丢步不转; s2gb 消失转 s2vsb+olb → **B 相桥臂故障与斩波模式无关, 两种模式仅检测标志不同**; 生产宏 SPI_QUIET 已还原 0 重编译 0E/0W）
- [✓] 移植 F407 工程代码到 H7（11 模块归一 + SEGGER_RTT + Core 织入 + FDCAN 重写）（起 2026-08-25 | 止 2026-08-25 | 验收 通过）
- [ ] [ ] StallGuard2 断电回零功能（上电自动回零 + 手动推位后重定位，无 Z 相光编）（起 | 止 | 验收 待定）
- [✓] 调试CAN和SPI，保证通讯实现（起 2026-09-08 | 止 2026-09-16 | 验收 通过）
- [x] 测试验证SPI（U2 读写双通路+5V硬件修复后；3轮闭环；修 WriteReg tCSH 帧间隔、判据移除只写寄存器 IHOLD_IRUN、修 U%u 标签）（起 2026-09-09 11:05 | 止 2026-09-09 11:30 | 验收 待用户确认）
- [x] SPI 提速复测 6.45Mbps（CubeMX 改 PLL3Q 内核+还原 NSSP/引脚速度/FDCAN MsgRAM 回退项；宏版实测写20us/读54us 回填时序表[x]；生产版 [SPI OK] 终验通过；修 cmake_build.py 生产→宏版注入盲区）（起 2026-09-09 11:45 | 止 2026-09-09 12:06 | 验收 待用户确认）
- [x] 文件合并重构：can_drv+can_usr→drv/can.c，tmc5160_drv+tmc5160_usr→drv/tmc5160.c（去 _drv 后缀；符号保留 DRV_/USR_ 前缀不改名；can→app 反向依赖为用户 2026-09-09 显式裁决，can.h/can.c 头注豁免登记；烧录回归 [SPI OK] 行为等价）（起 2026-09-09 12:20 | 止 2026-09-09 14:22 | 验收 待用户确认）
- [✓] S2/SpreadCycle 判定实验（撤 DIAG 钩子复跑）：r5 首跑遇 TMC 无供电([SOAK DEAD] 全零读回)→r6 活性门+非零对照组(已修 CTL 盲区)→**14:32 生产配置判定成立：运转中 ds=0x11140000(s2gb=1,SG=1,CS=20) + gs=02(drv_err 每秒) → B 相驱动异常实锤；且 S2 防误触发配置已是最低敏度档(S2G_LEVEL=15/FILTER=3/delay=1500ns/TBL=54clk)→误报概率低；5160 无 GCONF.swstack(ch06.p032-033 位表核实)→软件 AB 交换不可行, 转物理判别**（起 2026-09-10 14:40 | 止 2026-09-10 14:34 | 验收 2026-09-10 通过）
- [x] DRVSTRENGTH×CS 阶梯扫描 r7/r7b：{0,3,6} 与 {0,5,7 最强} 两轮 × CS{20,8} 共 12 格 → **s2gb 全格恒报、drv_err 恒有、轴零净转(enc 167~5427 抖动)**；drvs 完全无效 → 排除"驱动电流档位"类；olb 多格同报 → B 电流不达标+桥中点被拽低组合=非烧穿型；候选收敛=① VSA 12V→11.5V 栅压稳压器(12VOUT)压差仅 0.5V 轨塌陷(与 drvs 无效自洽: 档位救不了电压幅度) ② 自举电容 CB1/CB2/CPO 损坏 ③ 37-40/BM-LA 脚区焊桥 ④ B 低边管击穿。鉴别: 24V 重跑全清=①；表量 12VOUT<10.5V=①；断电机重跑仍 s2gb=板侧②③④（起 2026-09-10 15:10 | 止 2026-09-10 15:34 | 验收 2026-09-10 通过；※用户 15:4x 补测供电 12V 静态稳定——不能排除①瞬态塌陷，24V 对照仍待做）
- [x] 电机运行排查（CAN 命令后 SpreadCycle 抖动/不转→B相s2gb+olb；SPI 降速论 09-10 证伪；**r6 生产配置(S2 开)复判: SRB 修复无效, s2gb+drv_err 运转中复燃=芯片实锤 B 相短路, 桥臂被保护关断→单相 A 驱动→100%丢步原地抖(act=178876/enc≈-1400±700 振荡)**；短路位置待判: 板侧 B 腿 vs 线缆/绕组对地(用户曾测两相绕组阻值一致, 但 S2G 查的是输出腿对地)；下一步=r7 候选鉴别（24V 重跑表 / 断电机重跑 / 表量 B 端子与 12VOUT）详见遗留任务行）**※09-11 排查收敛: 换片✗换电容✗MOS✗ 桥阻对称✗ 降频✗ 空载全导通(板驱动链好)✗ 电源无跌落(候选①亡)✗ 线圈对调故障不离桥(负载侧亡) → 锁定 B 桥大电流通路电阻: 首选 FET 内伤(表测不出)/虚焊微裂, 处置=A/B FET 互换或换 B 桥四只**（起 2026-09-09 15:40 | 止 2026-09-11 | 验收 **PASS×2**: 开尔文跳线修正后, 15MHz 宏版 ManualRun 连续 `[MNLRUN PASS] reached=1 act=51200 enc=51200(一整圈零丢步) ds=81140061(s2gb/s2vsb/olb 全 0) gs=00`; 生产版(SPI_MANUAL=0)已烧, 心跳 act=102400 enc=0 锁轴 gs=00 | 根因=板铺铜破坏 RS-B Kelvin 走线+跳线错位, 详见 memory `u2_root_cause_kelvin_jumper_misplaced`）
- [x] 保持电流降档 IHOLD 6→3(≈0.58A) + 修 IHOLD_IRUN 注释位域写反 bug + SOAK2 加 X_ENC 丢步量化；运行电流 IRUN：曾按计算推 CS=13(2.01A)，按用户裁决回退参考代码实测值 CS=20(3.02A, F407 OTPW 史) ※量化结果：act=178818 vs enc=13（±282µsteps 振荡）= **100% 丢步**；※修正：OL 检测独立于 diss2g(ch11§11.3)，r4 运转中 ola/olb=0 为有效反证，仅 S2 组被屏蔽；※后续：用户实测 IHOLD=3 锁不住 → 当日改回 6（1.01A/~0.33N·m，已编译烧录）（起 2026-09-10 12:30 | 止 2026-09-10 15:30 | 验收 2026-09-10 通过）
- [✓] SPI 时序核查（用户假设 H7 6.45Mbps 过快）：手册 ch04§4.3 对照（帧间隔/模式/边沿裕量 1%）→ SOAK 三轮迭代（r1 误选只写寄存器 ENC_CONST 教训入 memory；r2 发现无 profile 电机未转；r3 真实运转 22.4万帧写读回显 0 翻转 + GCONF 对照 OK）→ **降速论证伪**；副产品：XACTUAL 速率精确匹配推导 → fCLK=15.00MHz±0.01% 软件钉死；SPI_SOAK 已置 0 生产版编译 0E/0W（起 2026-09-10 11:40 | 止 2026-09-10 15:50 | 验收 2026-09-10 通过）
- [ ] 生产化与硬件排查遗留（起 2026-09-10 | 止 ）：① 生产版(SPI_SOAK=0)已编译**未烧录**（板断电）——上电后烧+回归 ② IHOLD=6 手转锁轴验证 ③ 24V 供电重跑 r7 表（判 12VOUT 轨塌陷 vs 板侧损伤，12V 静态稳定不排除瞬态）④ 若 24V 仍报 s2gb：拔电机线重跑分板内外+表量 B 端子对 GND+放大镜查 QFN 37-40/9-10 脚区 ⑤ CHOPCONF/SR 修复相关 DIAG 注释历史已清，CAN 命令路径复现留待「调试CAN和SPI」任务
- [✓] 代码结构与命名按cl skill规范整顿（drv/algo/app分层 + S_ 命名 + 行宽≤100）（起 2026-09-08 | 止 2026-09-16 | 验收 通过）
- [✓] cl_config 宏定义注释澄清（6 宏含义/取值/消费点/现状补齐，纯注释零功能改动）（起 2026-09-12 | 止 2026-09-12 | 验收 编译通过 FLASH 44424B 不变，行宽全≤100）
- [✓] 非静音(SpreadCycle)模式运行验证：生产配置下运转全程无错误标志置位（起 2026-09-12 | 止 2026-09-12 | 验收 第1轮PASS：`[PRODRUN PASS] reached=1 act=51200 enc=51200(一整圈零丢步) ds=8114007E(s2g/s2vs/ol全0) gs=00`；生产版(SPI_PRODRUN=0)已重烧，boots=2 心跳健康 | 前提确认：生产init本就GCONF=0x00非静音，零生产代码改动，仅新增宏门控test钩子）
- [c] 无编码器回零逻辑(StallGuard2)：CAN 0x09 触发 + SG连续8次零确认 + 回退2048 + XACTUAL置零，非阻塞Tick（起 2026-09-12 | 止 2026-09-12 | 验收 代码完成编译0E/0W；暂无机械条件测试（双向空转60转无硬限位迹象，SG基线≈250，SGT初估≈-5）；有条件时按ch13§13.1交互调定后复测）
- [x] 急停协议 0x0A：ENN关断自由停车不锁轴 + 锁存 + 运动命令自动重使能（起 2026-09-12 | 止 2026-09-12 | 验收 T2协议PASS（ACK+恢复后运动完成）；烧录实测由你接管）
- [x] 反馈bit0去残留：ACK强制清bit0 + 到位补终态帧 + 30s超时帧（起 2026-09-12 | 止 2026-09-12 | 验收 T1双帧PASS；烧录实测由你接管）
- [✓] CAN到位终态/急停实测：0x02终态帧 + 0x0A急停恢复（起 2026-09-12 | 止 2026-09-12 | 验收 部分通过：T1反馈双帧教科书级PASS（ACK bit0=0→终态bit0=1，pos+5120）/ T2急停ACK+恢复后运动完成PASS（附带：急停期轴漂移约985µsteps，印证位置丢失须重回零）/ T3回零3轮耗尽回零不在本次范围（缺机械条件）；监听已杀，marker已清）
- [!] 失步标志常置位原因确认（起 2026-09-12 | 止 2026-09-12 | 验收 26轮零复现：早抢占/正常抢占/速度切换干净；精确复现帧20连测20/20全干净；打断路径软件侧排除，停稳常1只剩真偏差冻结一种解释；等你给出现时目标vs停稳pos值、速度/负载工况）
- [✓] 新增电机1(U1)的SPI通讯测试以及CAN控制电机运行（U1已焊+供电正常你确认；SPI对标U2活性门CHOPCONF非零签名+GCONF回显；CAN测U1定位一圈对标PRODRUN判据；init双芯已就绪，CAN U1路径已存在只需验证）（起 2026-09-14 | 止 2026-09-16 | 验收 通过）
- [c] drv+app+algo 文件夹按 cl code_style 重构（全项目符号去禁用前缀：DRV_TMC5160_*/USR_TMC5160_*/DRV_CAN_*/USR_CAN_*/USR_MOTOR_*/USR_HOME_*/USR_CLOSEDLOOP_*/USR_PID_* → S_+表意裸名，共 491 处/14 文件；与规格同名区分：SpiWrite/SpiRead=原始帧 vs WriteReg/ReadReg=芯片对象；drv 段标记/尾注/`*`框体清理 + 注释寄存器名改写；删空实现 USR_CAN_Init；can/tmc5160 头 guard 归一为 CAN_H/TMC5160_H；app/algo 格式整理：closed_loop.c 守卫补 Allman 花括号、homing.c 内部函数头补三要素；取代 2026-09-09「符号保留 DRV_/USR_ 前缀」裁决）（起 2026-09-17 | 止 2026-09-17 | 验收 代码完成：`format_gate.py` 全项目 0 FAIL + `cmake_build.py` 0E/0W（FLASH 46604B, DTCMRAM 3192B）；纯改名/格式整理不改行为（FLASH 前后一致），烧录+功能复测由你接管）风险:无
- [c] 回零分层重构：drv 留 StallGuard2 芯片原语（TMC5160_HomeConfig/HomeRestore/HomeCheckStall/HomeGetSg/HomeZero + TMC5160_HOME_DIR_*/VMAX），app 重建 homing.c 写编排（状态机/时序/CAN 完成反馈；运动经 MOTOR_* 以保留闭环命令目标 s_cmd_target 同步）；顺带消除 homing 的 REG_* 重复（复用 tmc5160.c 寄存器表，补 REG_SW_MODE 0x34）；补 2：新增 .cl/memory/config.md 3 条（home_sg_poll_ms=2 推导 / home_run_timeout_ms=30000 / home_bo_timeout_ms=10000 待实测确认）+ 代码依据行（起 2026-09-17 | 止 2026-09-17 | 验收 format_gate 0 FAIL + cmake_build 0E/0W（FLASH 46692B, +88B 为 drv 原语未内联）；行为等价于原 app/homing 整体；烧录+功能复测由你接管）风险:无
- [✓] 使用寄存器方式读写 SPI + 片选（S_SpiTransfer 寄存器级收发替换 HAL_SPI_TransmitReceive；片选 SelectChip/DeselectChip 由 HAL_GPIO_WritePin 改 GPIOx->BSRR 原子写；SPI 配置仍由 CubeMX 提供；顺带修正计量/延时基准：DWT 实测 481MHz → tim_test.c 除数 240→480、S_DelayUs(tmc5160.c) 同改，tCSH 标称 10→5µs 保持实际时序不变）（起 2026-09-17 | 止 2026-09-17 | 验收 **PASS**：①功能 `[SPI OK] tested U1+U2 link`（双芯 CHOPCONF=0x000181C5 回读 + GCONF 写-读回显）；②时序门禁宏版 tag0/tag1：写单事务 max=4437cyc≈9.2µs(含片选) / 读双报事务 max=11311cyc≈23.6µs(含片选)，对比 HAL 旧值(/240 虚高 2×) 写~10/读~27µs 约 -13%；③生产版(宏关 + COMM_Test_SPI 还原)最终已烧 boots=8 心跳健康无 [TM]；④附加 SPE 常开 A/B 实测无收益(tag0 4437→4373cyc≈-0.13µs / tag1 11311→11305cyc≈0)已回退；format_gate 0 FAIL / build 0E/0W FLASH 44928B）风险:无
