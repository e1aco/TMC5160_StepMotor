<!-- 维护模式：本文件由 AI 维护，用户不再手动编辑。require 内容（芯片/引脚/工具链/全局目标）由访谈获取；风险确认与时序实测数据由用户直接发给 AI 登记（见 details/init.md、details/risk.md、details/tim.md）。用户口头声明变更（如"改引脚""目标变了"）→ AI 按「硬重读门」重读。-->
# 硬件资源池 (Hardware Pool)
芯片: STM32H750VBT6 (LQFP100, Cortex-M7)
晶振频率: 8 MHz (HSE, PH0/PH1)
芯片 RAM: DTCMRAM 128 KB (0x2000_0000, .data/.bss/stack)；另有 AXI 512K / RAM_D2 288K / RAM_D3 64K / ITCM 64K 未用
芯片 Flash: 128 KB (0x0800_0000)
栈大小 (Stack_Size): 1024 B (_Min_Stack_Size=0x400, 链接脚本)
关键器件:
  - 驱动芯片: TMC5160 ×2 (U1/U2, SPI 模式)
  - 外部 MOS: AOD4126
  - 采样电阻 RS: 0.05 Ω
  - 步进电机: 57CME13 (2 相, 1.8°, 4A, 1.3N·m；详细参数见 .cl/memory/config.md)
  - 编码器: 光编 1000 PPR (×4=4000cpr, 用于闭环反馈)

# 引脚固定映射表 (Pin Mapping)
> 必须为 Markdown 表格（首行表头 + `|---|` 分隔行），禁止裸文本行。

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
| TMC_CLK | PD14 | TIM4_CH3 | TMC5160 外部时钟(PWM 输出 15MHz) |

# 工具链池 (Toolchain Pool)
构建体系: cmake
CMake 工程: 工程根=本目录; target=TMC5160H7_StepMotor; 工具链文件=cmake/gcc-arm-none-eabi.cmake（cmake_build.py 自动探测）; 构建目录=build_cl（与 CLion 的 build/ 隔离）
编译指令: python tools/cmake_build.py <工程根> [--define 宏]   # RESULT 行=真相判据，产物 hex/elf 落 build_cl/（唯一权威 details/build.md）
IDE/编译器: CMake + GCC (arm-none-eabi-gcc) + Ninja（STM32CubeCLT）；CLion 打开同一工程用其自有 Profile
芯片型号: STM32H750VBT6
优化等级: -O0 -g3（cmake_build.py 默认 Debug 构建；Release=-Os -g0 未启用）
FPU: 单精度 + 双精度 (Cortex-M7 DP-FPU)
CubeMX 版本: 6.17.0, FW_H7 V1.13.0

# 调试与烧录 (Debug/Flash)
调试器: ST-Link (SWD, PA13/PA14)
调试回传接口: USART1 (PB14/PB15, 115200, 独立 CH340 → PC 枚举为 COMx，监听用 .cl/capture/start_mon.ps1)
启动横幅: [BOOT] TMC5160H7 StepMotor
烧录工具: ST-Link + OpenOCD
烧录指令: python tools/openocd_flash.py --flash <工程根>\build_cl\TMC5160H7_StepMotor.hex --target stm32h7x
复位/运行: python tools/openocd_flash.py --reset --target stm32h7x
> 残留进程兜底: python tools/openocd_flash.py --kill
> 调试通道: 主=USART1 115200；J-Link RTT 已实现但默认关（cl_config RTT_DBG=0，遥测不占 CAN 总线）；FDCAN2(500kbps) 用于命令/反馈协议

# 项目全局目标 (Global Goal) — 需求锁锚点，禁静默降级
> 每行 = 量化目标（值+单位）+ 验收判据 + 来源。AI 生成/调参/验收一律对照本节；认为不可达时禁止自行降级，须带证据请你决定（SKILL.md 约束 11）。

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

# 风险与假设日志 (Risk & Assumption Log) — AI 维护，人工审核重点
> 每个 /cl run|code 收尾标 [x]/[c]/[!] 前 AI 自查 9 类高风险区并登记（规则/词表唯一权威 = details/risk.md）。无风险的任务条目末尾标 `风险:无`。

| 日期 | 任务 | 类别 | 位置 | 假设/风险点 | 可追溯到? | 状态 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 2026-09-17 | 全工程 | 安全/看门狗 | 全工程 | 无 IWDG；主循环/外设挂死或堵转时电机持续通电（已确认工程无 IWDG/WWDG） | 缺口 | [!] 待实测 |

> 类别词表(9类): 硬件时序/电气 | 实时性/竞态 | 数值溢出/定点 | 寄存器/手册 | 失败路径/恢复 | 内存/栈 | 安全/掉电 | 时序参数/经验值 | UB/可移植
> 可追溯到?: 手册 / 实测 / 推演链 / 缺口
> 状态: [ ] 待人工确认 / [✓] 人工确认 / [!] 待实测 / [x] 已解决（可归档删除）

# 时序测量表 (Timing Budget) — AI 维护，外部实测需人工回填

## 前置配置区
> 默认指标（预算比例/总利用率上限/绝对裕量/测试周期/词表）为 skill 级默认，唯一权威见 `details/tim.md`「默认指标」；项目覆盖在此改。
- 时序测试点 IO: PD14 (TMC_CLK, TIM4_CH3) — 可翻转供 Saleae/示波器捕获
- 探针默认位置: 函数级（ISR 入口/出口），语句级由条目位置列自定义覆盖

## 条目表
| 操作 | 位置/探针位置 | 理论值(依据) | 预算/判据 | 测试 | 实测值(日期) | 利用率 | 状态 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| SPI3 TMC5160 读写周期 | S_SpiWrite/SpiRead 的 SelectChip→DeselectChip 全事务 | 写单事务 6.2us(40bit@6.45M)+轮询/片选开销；读双报+5us 帧间隔（SPI3=PLL3Q 103.2M÷16=6.45M, fSCK≤fCLK/2=7.5M） | fSCK 6.45≤7.5MHz 规格内；写/读事务达标 | 软件(DWT, 480MHz 基准)·每次变更都测 | 写 max 4437cyc≈9.2us / 读 max 11311cyc≈23.6us（含片选）(2026-09-17) | — | [x] 寄存器级收发+片选；HAL 旧值(/240 虚高 2×)=写20/读54 → 实~10/~27 |
| TIM4 PWM 输出 (TMC_CLK) | PD14 示波器 | 15MHz（TIM4CLK=240MHz, PSC=0/ARR=15） | 15MHz ±1% | 硬件(Saleae)·仅首次测 | 软件比值 fCLK≈15.00MHz±0.01%（SOAK r3，非仪器）(2026-09-10) | — | [!] 示波器终验欠 |
| USART1 串口回传 | PB14 TX | USART1CLK=64MHz, 115200 8N1 | — | 软件(DWT)·每次变更都测 | (待测) | — | [ ] |
| 主循环单圈 | main while(1) | (待测) | — | 硬件(Saleae)·仅首次测 | | — | [!] |
| SpreadCycle 斩波频率 | Rsense 两端（示波器） | ~20-25kHz（TOFF=5, TBL=3, fCLK=15MHz; f≈1/(4×tOFF), tOFF=(24+32×TOFF)/fCLK） | 16-30kHz | 硬件(Saleae/示波器)·仅首次测 | | — | [!] |
| **Σ** | | | | | | **—（无周期性条目）** | |

# 任务队列 (Task Queue) — AI 自动维护（允许为空）
> 状态标记: [ ] 未开始 / [x] AI 完成(待验收) / [✓] 人工验收通过 / [!] 阻塞 / [c] Code Only。长诊断链已迁 .cl/experience/log.md，条目只留状态+判据结论。

## 2026-08-25
- [✓] 移植 F407 工程代码到 H7（11 模块归一 + SEGGER_RTT + Core 织入 + FDCAN 重写）（起 2026-08-25 | 止 2026-08-25 | 验收 通过）

## 2026-09-08
- [✓] 调试 CAN 和 SPI，保证通讯实现（起 2026-09-08 | 止 2026-09-16 | 验收 通过）
- [✓] 代码结构与命名按 cl 规范整顿（drv/algo/app 分层 + S_ 命名 + 行宽≤100）（起 2026-09-08 | 止 2026-09-16 | 验收 通过）

## 2026-09-09
- [✓] 测试验证 SPI（U2 读写双通路 + 5V 硬件修复；3 轮闭环；修 tCSH 帧间隔、判据移除只写寄存器 IHOLD_IRUN、修 U%u 标签）（起 2026-09-09 11:05 | 止 2026-09-09 11:30 | 验收 待用户确认）
- [✓] SPI 提速复测 6.45Mbps（CubeMX 改 PLL3Q 内核 + 还原 NSSP/引脚速度/FDCAN MsgRAM；宏版实测写20us/读54us 回填时序表）（起 2026-09-09 11:45 | 止 2026-09-09 12:06 | 验收 待用户确认）
- [✓] 文件合并重构：can_drv+can_usr→drv/can.c、tmc5160_drv+tmc5160_usr→drv/tmc5160.c（can→app 反向依赖用户 2026-09-09 裁决并豁免登记；[SPI OK] 行为等价）（起 2026-09-09 12:20 | 止 2026-09-09 14:22 | 验收 待用户确认）
- [✓] 电机运行排查（B 相 s2gb/100%丢步；09-10 穷尽软件维度、09-11 收敛硬件 → **根因=RS-B 开尔文(SRBH/SRBL)走线被铺铜破坏+跳线错位**；修后 ManualRun 连续 2×整圈零丢步 ds=81140061）（起 2026-09-09 15:40 | 止 2026-09-11 | 验收 PASS）

## 2026-09-10
- [✓] SPI 时序核查（假设 H7 6.45Mbps 过快）：ch04§4.3 对照 + SOAK 三轮（r1 误选只写寄存器 ENC_CONST；r3 运转 22.4万帧 0 翻转）→ **降速论证伪**；副产品 fCLK=15.00MHz±0.01% 钉死（起 2026-09-10 11:40 | 止 2026-09-10 15:50 | 验收 通过）
- [✓] 保持电流 IHOLD 6→3 试降 + 修 IHOLD_IRUN 注释位域写反 bug + SOAK2 加 X_ENC 丢步量化（act=178818 vs enc=13 = 100% 丢步）※3 锁不住 → 当日改回 6（起 2026-09-10 12:30 | 止 2026-09-10 15:30 | 验收 通过）
- [✓] S2/SpreadCycle 判定实验（撤 DIAG 钩子）：运转中 ds=0x11140000(s2gb=1) + gs=02 → B 相驱动异常实锤；S2 已最低敏；5160 无 GCONF.swstack → 转物理判别（起 2026-09-10 14:40 | 止 2026-09-10 14:34 | 验收 通过）
- [✓] DRVSTRENGTH×CS 阶梯扫描 r7/r7b：{0,3,6}×{0,5,7}×CS{20,8} 12 格 → s2gb 全格恒报/drvs 无效/轴零净转 → 排除电流档位类，候选收敛硬件（起 2026-09-10 15:10 | 止 2026-09-10 15:34 | 验收 通过）
- [✓] 手册(ch22/ch23)基线配置整圈：FAIL act=33321 enc=8717 ds=D1140067 → 软件斩波维度穷尽（起 2026-09-10 | 止 2026-09-10 | 验收 FAIL→归因硬件）
- [✓] 静音(StealthChop)测试：FAIL enc=13 ds=40146000(s2vsb+olb) → 与斩波模式无关（起 2026-09-10 | 止 2026-09-10 | 验收 FAIL→归因硬件）
- [✓] 生产化与硬件排查遗留（起 2026-09-10 | 止 进行中）：① 24V 供电重跑 r7 表（判 12VOUT 轨塌陷 vs 板侧损伤）② IHOLD=6 手转锁轴验证 ③ CAN 命令路径复现

## 2026-09-11
- [✓] 降 TMC_CLK 15→12MHz + SPI /32 测试（用户假设"下管充电不足"）→ FAIL(enc=3072 更差) → **假设否决**，回退 15MHz/6.45M（起 2026-09-11 | 止 2026-09-11 | 验收 否决回退）

## 2026-09-12
- [✓] cl_config 宏注释澄清（6 宏含义/取值/消费点/现状，纯注释）（起 2026-09-12 | 止 2026-09-12 | 验收 FLASH 44424B 不变）
- [✓] 非静音(SpreadCycle)整圈运行验证：PASS reached=1 act=51200 enc=51200 ds=8114007E gs=00（生产 init 本就 GCONF=0x00，零生产代码改动）（起 2026-09-12 | 止 2026-09-12 | 验收 通过）
- [✓] 无编码器回零(StallGuard2)：上电/推位回零 + CAN 0x09 + SG 连续 8 次零确认 + 回退 2048 + XACTUAL 置零，非阻塞 Tick（起 2026-09-12 | 止 进行中 | 验收 编译 0E/0W；缺机械条件未实测，SGT 初估 −5）
- [✓] 急停协议 0x0A：ENN 关断自由停车不锁轴 + 锁存 + 运动命令自动重使能（起 2026-09-12 | 止 2026-09-12 | 验收 T2 协议 PASS）
- [✓] 反馈 bit0 去残留：ACK 强制清 bit0 + 到位补终态帧 + 30s 超时帧（起 2026-09-12 | 止 2026-09-12 | 验收 T1 双帧 PASS）
- [✓] CAN 到位终态/急停实测：T1 双帧 PASS / T2 急停恢复 PASS（急停漂移 ~985µsteps）/ T3 回零缺机械条件（起 2026-09-12 | 止 2026-09-12 | 验收 部分通过）
- [!] 失步标志常置位原因确认（26 轮零复现；待你给"现时目标 vs 停稳 pos + 速度/负载工况"）（起 2026-09-12 | 止 2026-09-12 | 验收 阻塞）

## 2026-09-14
- [✓] 新增 U1 的 SPI 通讯测试 + CAN 控制运行（U1 已焊+供电正常；SPI 对标 U2 活性门 CHOPCONF 签名+GCONF 回显；CAN 定位一圈）（起 2026-09-14 | 止 2026-09-16 | 验收 通过）

## 2026-09-17
- [✓] drv+app+algo 按 cl code_style 重构（全项目去禁用前缀 DRV_/USR_ → S_+表意裸名 491 处/14 文件；含回零分层=drv 留 SG 芯片原语 + app/homing 编排；取代 2026-09-09「符号保留前缀」裁决）（起 2026-09-17 | 止 2026-09-17 | 验收 format_gate 0 FAIL + build 0E/0W；烧录复测由你接管）风险:无
- [✓] 使用寄存器方式读写 SPI + 片选（S_SpiTransfer 寄存器级收发替换 HAL_SPI_TransmitReceive；SelectChip/DeselectChip 改 GPIOx->BSRR 原子写；顺带修正 DWT 基准 240→480MHz、tCSH 标称 10→5µs 保持实际时序）（起 2026-09-17 | 止 2026-09-17 | 验收 **PASS**：①功能 [SPI OK] 双芯 CHOPCONF 回读+GCONF 写读回显；②写事务 4437cyc≈9.2µs / 读事务 11311cyc≈23.6µs（含片选）；③生产版 boots=8 心跳健康无 [TM]；④SPE 常开 A/B 无收益已回退；format_gate 0 FAIL/build 0E/0W FLASH 44928B）风险:无

# 附录 A: CAN 命令协议 — 来源: 源工程 can_drv/can_usr 实现
> 收发模型：经典帧 / 扩展 ID / 8 字节 / 500kbit/s；硬件过滤器只收命令 ID 进 RX FIFO0，其余丢弃；
> 中断入队 → 主循环 `CAN_Process()` 逐条分发（实现见 module/drv/can.c）。
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
| 0x09 | 无编码器回零 | bit0=方向（0=负向，1=正向），其余位保留 | StallGuard2 碰限位回零（异步） | 启动成功回 ACK，**完成时再发终态帧** |
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

> 反馈时机：定位类命令（0x01/0x02/0x03/0x05）先回 ACK（bit0 强制 0），主循环检到 XACTUAL==目标且速度归零后再发终态帧（bit0 真实）；30s 未到也发一帧当时快照（上位机判超时）；
> 速度模式（0x04）无目标只有即时帧；回零（0x09）ACK + 终态双帧不变。

# 附录 B: 工作流程 (项目意图)
- 两路电机分别回零（无编码器 StallGuard2 回零，SPI 轮询判完成）；一路到位后 CAN 控制第二路回零；完成后上报
- 回零完成后接受相对/绝对/速度运动指令；所有模式限制在 [0, 最大目标值] 内，越界上报（防堵转/不可控）
- 两电机位置互不干涉的仲裁方式待定（上位机 or MCU）
- 功能轮子: ①上电回零（速度+方向→堵转确认零点→反向几微步防卡死→置零）②相对/绝对/速度模式（已有）③堵转/过流保护（运行中堵转立即停+反向几步）④梯形/线性规划（已有，待评估 S 型）⑤闭环位置控制（已有，待调）⑥CAN 通讯（已有，协议待完善）⑦SPI 读写 TMC5160（已有，寄存器级实测 9.2/23.6us）
