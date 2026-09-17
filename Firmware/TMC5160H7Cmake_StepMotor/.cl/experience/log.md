# 调试日志

> **滚动窗口**：log.md 只存「活性记录 + 最近进行中」——已封存条目随 `/cl end` 精简删除（git 历史兜底）。
> **价值三层模型**：结论层→require.md/.cl/memory；条件层→诊断案例或 details/tools；过程层→本文件。
> **精简判据/提升规则** 见 templates/log.md。

## [2026-09-17] 任务: SPI 寄存器级收发+片选改造（含 DWT 基准修正）— /cl end 验收通过
- **模式**: /cl run（全自动闭环；中途按用户裁决追加"片选寄存器级"与"SPE 常开 A/B 实验并回退"）
- **现象**: 需求=SPI 收发/片选走寄存器级提速。HAL 基线(旧表 /240 基准)=写 20us/读 54us；寄存器级实测(DWT 修正 480MHz 后)=写事务 4437cyc≈9.2us / 读事务 11311cyc≈23.6us(含片选)
- **尝试**:
  - 澄清 Q1-Q4（仅换收发/临时开自检作判据/跑时序门禁/扫描串口）→ 生成 S_SpiTransfer（H7 high-end 序列 TSIZE→SPE→CSTART→轮询 TXP/RXP→等 EOT→清 IFCR），替换 SpiWrite/SpiRead/DebugTransfer
  - 功能判据先过：`[SPI OK] tested U1+U2 link`（双芯 CHOPCONF=0x000181C5 回读 + GCONF 写读回显）
  - 用户追加：片选改 `GPIOx->BSRR` 寄存器级 → 读事务中位 48.99→47.13us(/240 基准)
  - 发现 DWT 基准错：OpenOCD 自由运行读 CYCCNT 962.5Mcyc/2s≈481MHz → `tim_test.c` /240 改 /480；`S_DelayUs` 同步(tCSH 标称 10→5us 保持实际时序)
  - SPE 常开 A/B：tag0 4437→4373cyc / tag1 ~0 → 无收益，用户裁决回退
  - 生产版还原(宏关+COMM_Test_SPI 还原)烧录：boots=8 心跳健康无 [TM]
- **最终方案**: 寄存器级 S_SpiTransfer(逐帧 SPE) + 寄存器级片选 BSRR + DWT 480MHz 基准修正；生产版 FLASH 44928B
- **验证结果**: ✅ 功能 [SPI OK] 双芯；时序表 [SPI3] 条回填 [x]；format_gate 0 FAIL / build 0E/0W
- **经验引用**: memory += `spi_spe_always_on_no_gain` / `stm32_dwt_cyccnt_clk` / `tmc5160_tCSH` 修正；工具坑=cmake_build.py GBK 崩溃、OpenOCD SWD IDCODE 瞬态失败(重试恢复)
- **提示词压缩**: `[STM32H750/SPI3+TMC5160] [寄存器级收发:TSIZE→CSTART→轮询TXP/RXP→等EOT] [片选GPIOx->BSRR] [DWT基准=CPU=480MHz] [判据[SPI OK]+[TM]写9.2/读23.6us]`
- **违规自检**: ✅已读 run/codegen/watcher/preflight/probe/tim/build/flash/format-gate/risk/run-fault/end；✅全自动闭环；✅澄清+用户裁决后才生成；✅工具问题按 run-fault 处理；⚠️过程教训：时序门禁应"功能判据先过再测 [TM]"（初次先建宏版测量，经用户纠正）

## [2026-09-11] 任务: 电机运行排查 B 相 s2gb — /cl end 验收通过（根因=RS-B Kelvin 跳线错位）
- **模式**: /cl run（静音轮→手册基线轮→降频轮→回归轮，共 5 版固件）+ 硬件侧用户排查
- **现象**: 承 09-10 未结案；新增：StealthChop 报 s2vsb+olb（ds=40146000）；手册基线轮电机首次位移 8717(17%)后锁（ds=D1140067）；12MHz 轮 enc=3072(6%)更差；用户实测链：HB2-BMB2=0、A 栅 4.5V/B 栅 22V、桥阻对称、换片换电容 MOS 完好、空载全导通、电源无跌落、线圈对调故障不离桥、B 下管也不导通
- **尝试**:
  - 静音轮(`COMM_Test_SPI_Quiet`, GCONF=0x04)：FAIL act=17801 enc=13 ds=40146000(s2gb=0,s2vsb+olb=1) → 斩波模式无关
  - 手册基线轮(`COMM_Test_ManualRun`, CHOPCONF=0x000100C5+DRV_CONF 复位缺省)：FAIL act=33321 enc=8717 ds=D1140067 → 斩波/drvs 维度穷尽；附带修钩子 bug（绝对目标撞 XACTUAL 编码器同步起始位→改相对+51200，到位比较改绝对变量）
  - 降频轮(15→12MHz, SPI/32)：FAIL enc=3072 更差 → "下管充电不足"假设否决 → 用户放弃，回退 15MHz/6.45M（[TM] 复验写20us/读54us 与 09-09 一致）
  - ch11.p079 核对：短路响应=**分桥关断**(B 整桥 HS+LS 全停、A 继续) → "B 下管不导通"=同一跳闸之果；双检测器齐跳(s2gb+s2vsb)=回路整体过流 → 嫌疑重排首位=采样通路（修正上轮"全局关断"说法）
  - 用户硬件侧穷尽：换片✗换电容✗MOS✗电源✗对调不离桥 → 最终查出**板铺铜破坏 RS-B Kelvin(SRBH/SRBL)走线 + 补救跳线错位**（修复确认）
- **最终方案**: 硬件修正跳线；固件生产 init 本就正确无改动；测试钩子(`SPI_MANUAL`/`SPI_QUIET`)验收后删除归位生产版
- **验证结果**: ✅ 15MHz 宏版 ManualRun **连续 2× `[MNLRUN PASS] reached=1 act=51200 enc=51200`（一整圈零丢步）`ds=81140061 gs=00`**；生产版(SPI_MANUAL=0)已烧，心跳 act=102400 enc=0 锁轴 gs=00 健康
- **经验引用**: memory 新增 10 条（quiet_s2vsb/manual_partial/hb2_zero/gate_rail/fclk12_rejected/noload_cleared/follows_bridge/per_bridge_shutdown/kelvin_jumper_fixed/regression_pass）；违规自检：无（run 全自动/先澄清后生成/未降判据/工具问题按 run-fault 处理）
- **提示词压缩**: `[TMC5160 s2gb 空载好带载锁] [先查采样通路RS/SRBH/SRBL再查驱动] [分桥关断A活B灭] [ManualRun相对整圈判据] [跳线修复+回归PASS]`

## [2026-09-12] 任务: 生产配置 SpreadCycle 整圈运行验证（无错误标志）— /cl end 验收通过
- **模式**: /cl run（第 1 轮直 PASS，无迭代）
- **现象**: 用户要求非静音模式 + 运转全程无错误标志；查生产 init 本就 GCONF=0x00 SpreadCycle（零生产代码改动，任务本质为物理验证）
- **尝试**:
  - 生成：新增宏门控钩子 `COMM_Test_ProdRun`（test/comm_test.c，不重写斩波，仅 ApplyProfile(4)；GCONF=0x00 门控确认；判据=reached 且 |enc|≥25600 且 s2g/s2vs/ol 全 0 且 drv_err=0）+ `SPI_PRODRUN`（cl_config.h）+ main.c 上电段接线；format 门禁顺手收敛旧超宽行 534
  - 编译：cmake_build.py 0E/0W；时序预检未命中（未改 drv/时钟/while 体）
  - 烧录监听：COM8(CH340 独立 UART)+ST-Link/openocd 直调（wrapper --search 失效，见经验）；watcher 一次读 status 即命中
- **最终方案**: 生产配置验证通过即归位（SPI_PRODRUN→0，重编重烧生产版）
- **验证结果**: ✅ `[PRODRUN PASS] reached=1 act=51200 enc=51200(一整圈零丢步) ds=8114007E(s2g/s2vs/ol 全 0) gs=00`；生产版 boots=2 心跳健康
- **经验引用**: 诊断案例不入库（1 轮直中，不满足判据①）；工具坑 3 项修 skill（openocd_run.py --search 透传；烧录反斜杠路径；start_mon -Tools 探测）；过程项仅本 log
- **提示词压缩**: `[STM32H750/TMC5160 生产SpreadCycle] [GCONF=0x00+ApplyProfile(4)相对整圈] [ds掩码0x38003000+gs全0+enc跟随] [PRODRUN钩子+宏归零重烧] [首轮PASS]`
- **违规自检**: ✅已读 run/watcher/preflight/flash/run-fault/end；✅全自动闭环未推手动；✅澄清后生成；✅无静默降级（最严判据档）；✅工具问题按 run-fault 处理；违规记录：无

## [2026-09-12] 任务: CAN 回零/急停/到位反馈闭环实测 + 失步标志猎捕 — 部分通过
- **模式**: /cl code（回零/急停/反馈逻辑）+ /cl run（CAN 实测 20+ 连测）
- **现象**: 回零缺机械条件；失步 flag 用户偶现、26 轮零复现
- **尝试**:
  - 回零逻辑：app/homing.c 状态机 + CAN 0x09 + memory 回零参数段；编译 0E/0W
  - 急停 0x0A + 反馈 bit0 去残留 + 终态补帧：编译 0E/0W
  - T1 反馈双帧 PASS / T2 急停恢复 PASS（急停漂移 985µsteps）/ T3 回零双向 60 转超时（SG 60 点恒定 226~272；修终态重启动 busy bug）
  - 失步猎捕：早/正常抢占、速度切换、精确复现帧三档 gap 全干净；20 连测 20/20 全干净（60 帧）
- **最终方案**: 回零/失步阻塞等条件与数据；急停/反馈按实测 PASS 生效
- **验证结果**: ✅ T1/T2；❌ 回零（无硬限位迹象，SGT 初估 −5）；❌ 失步复现（26 轮零）
- **经验引用**: 诊断案例不入库（无根因，不满足判据②）；skill 固化 can_monitor --hexdump + can_send home/estop 快捷（本轮已修验）；过程项仅本 log
- **提示词压缩**: `[STM32H750/TMC5160 CAN闭环] [0x09回零+0x0A急停+终态补帧] [PCAN同句柄收发+二进制解码] [T1T2过回零失步阻塞] [20连测全净]`
- **违规自检**: ✅全自动（20 连测无人值守）；✅澄清后生成；✅无静默降级（阻塞如实[!]/[c]）；✅工具坑走 skill 固化；违规记录：无
