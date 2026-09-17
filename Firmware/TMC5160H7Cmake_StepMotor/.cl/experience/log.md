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
