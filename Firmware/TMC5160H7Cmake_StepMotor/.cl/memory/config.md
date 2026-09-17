# .cl/memory/ — 配置推导值

## 电机参数 (57CME13)
motor_phases = 2                    依据: 电机规格书 日期: 2026-08-24 来源: 推导
motor_hold_torque = 1.3 N·m         依据: 电机规格书 日期: 2026-08-24 来源: 推导
motor_step_angle = 1.8°             依据: 电机规格书 (200 steps/rev) 日期: 2026-08-24 来源: 推导
motor_rated_current = 4 A           依据: 电机规格书 日期: 2026-08-24 来源: 推导
motor_phase_resistance = 0.42 Ω     依据: 电机规格书 日期: 2026-08-24 来源: 推导
motor_phase_inductance = 1.6 mH     依据: 电机规格书 日期: 2026-08-24 来源: 推导
motor_rotor_inertia = 3e-5 kg·m²   依据: 电机规格书 0.3kg·cm² 转换 日期: 2026-08-24 来源: 推导
motor_weight = 0.8 kg               依据: 电机规格书 日期: 2026-08-24 来源: 推导

## MOSFET 参数 (AOD4126)
mosfet_vds = 100 V                  依据: AOD4126 规格书 日期: 2026-08-24 来源: 推导
mosfet_id_max = 43 A                依据: AOD4126 规格书 @VGS=10V 日期: 2026-08-24 来源: 推导
mosfet_rds_on = 24 mΩ               依据: AOD4126 规格书 @VGS=10V, typ 日期: 2026-08-24 来源: 推导
mosfet_qg = 28 nC (typ)             依据: AOD4126 规格书 日期: 2026-08-24 来源: 推导
mosfet_qgd = 10 nC (typ)            依据: AOD4126 规格书 日期: 2026-08-24 来源: 推导
mosfet_vgs_th = 3.3 V (typ)         依据: AOD4126 规格书 日期: 2026-08-24 来源: 推导

## 外围电容选型 (TMC5160A 应用, ch03.p015-p017 2026-09-10 补提取入库)
board_bootstrap_cap = 470 nF ×4      依据: TMC5160 图3.1(ch03.p015)明文规则 "220nF for MOSFETs with QG<20nC, 470nF for larger QG"; AOD4126 Qg=28nC(typ)>20nC → 470nF; 每半桥一只: CA1-BMA1/CA2-BMA2/CB1-BMB1/CB2-BMB2(ch02.p014: CA/CB=正端, BMA/BMB=桥中点=负端); 待核对板上现值(若 220nF 即低于 Qg=28nC 规则值) 日期: 2026-09-10 来源: 推导(ch02/ch03+mosfet_qg)
board_12vout_cap = 10 µF             依据: ch02.p012 12VOUT "2.2µF~10µF 且 ≥10× 自举电容" → 470nF×10=4.7µF → 取窗口上限 10µF; 若板上仅 2.2µF 违反 10× 规则 日期: 2026-09-10 来源: 推导
board_pump_caps: CPI-CPO=22nF/100V, VCP-VS=100nF/16V  依据: ch02.p013 + 图3.1(ch03.p015) 定值 日期: 2026-09-10 来源: datasheet
board_vs_bulk_cap ≥ 100µF/A          依据: ch03.p015 "minimum capacity of 100µF per ampere of coil current" → 4A 电机 ≥400µF 低 ESR 日期: 2026-09-10 来源: datasheet

## TMC5160 驱动配置
tmc5160_rs = 0.05 Ω                依据: 硬件资源池 日期: 2026-08-24 来源: 你
tmc5160_vfs = 0.325 V               依据: TMC5160 datasheet ch28 感应电阻峰值阈值 (VSRT) 日期: 2026-08-24 来源: 推导
tmc5160_ifs = 6.5 A                 依据: VFS/RS = 0.325/0.05 日期: 2026-08-24 来源: 推导
tmc5160_globalscaler = 256(复位默认, 未写)  依据: init 代码未写 GLOBALSCALER, GS=256→满量程 IFS=VFS/RS=6.5A, 电流全靠 CS 分频; 早期推导 157+CS31 方案未采用 日期: 2026-09-10 来源: 代码核对
tmc5160_irun = 20                   依据: (20+1)/32×6.5/√2=3.02A RMS; 实测 CS=27(4A) 双机满流过热 OTPW→热致 S2 误触发, 降 20 后 150 轮浸泡 0 次 日期: 2026-09-01 来源: 实测校准
tmc5160_ihold = 3                   依据: (3+1)/32×6.5/√2=0.576A RMS, 保持力矩≈0.19N·m; 用户 2026-09-10 定案"静止发热大"从 6(1.0A) 降档 日期: 2026-09-10 来源: 用户定值
tmc5160_ihold_delay = 8             依据: IHOLDDELAY[19:16]=8 → 2^18×8/15MHz=140ms 后降至 IHOLD (09-10 代码核对: 旧记录"DELAY=6"系注释位域写反误读, 实际代码一直=8) 日期: 2026-09-10 来源: 代码核对
tmc5160_tpowerdown = 128            依据: 推荐值, 约 1-2 秒延迟 日期: 2026-08-24 来源: 推导

## TMC5160 时钟
tmc5160_clk_source = external        依据: PD14 TIM4_CH3 PWM 输出 日期: 2026-08-24 来源: 你
tmc5160_clk_freq = 15 MHz           依据: require.md TIM4 PWM 配置(2026-09-11 放弃 12MHz 实验回退 Period=15; 假设否决见 u2_fclk12_boot_test_hypothesis_rejected); 软件比值验证: SOAK r3 XACTUAL 17881/s=20000×15e6/2^24 精确匹配 → fCLK=15.00MHz±0.01% (板现为 12M 测试固件, 回烧后生效) 日期: 2026-09-11 来源: 推导+软件实测
tmc5160_spi_max_sck = 7.5 MHz       依据: TMC5160 datasheet ch4: fSCK = fCLK/2 = 15/2 日期: 2026-09-11 来源: 推导

## TMC5160 SPI 配置
tmc5160_spi_mode = 3                 依据: TMC5160 datasheet ch4 SPI MODE 3 (CPOL=HIGH, CPHA=2EDGE) 日期: 2026-08-24 来源: 推导
tmc5160_spi_datasize = 8-bit         依据: TMC5160 40-bit datagram = 8-bit addr + 32-bit data 日期: 2026-08-24 来源: 推导
stm32_spi_prescaler = 16             依据: SPI3 kernel=PLL3Q 103.2MHz ÷16 = 6.45MHz (≤fCLK/2=7.5MHz; 2026-09-11 放弃 12MHz 实验回退, 32→16) 日期: 2026-09-11 来源: 实测校准
stm32_spi_baudrate = 6.45 Mbit/s     依据: PLL3Q 103.2MHz/16; 2026-09-11 回退; 12MHz 实验期实测 34/82µs 见时序表历史; 原 09-09 实测 写20us/读54us 日期: 2026-09-11 来源: 实测校准
stm32_spi_kernel_clk = 103.2 MHz     依据: PLL3: HSE8MHz/M5=1.6 ×N129=206.4 VCO ÷Q2 日期: 2026-09-09 来源: CubeMX+推导

## TMC5160 通讯事实 (2026-09-09 SPI 验证轮沉淀)
tmc5160_ihold_irun_readable = false   依据: IHOLD_IRUN(0x10) 只写寄存器, 回读恒0, 不可作回读判据; TPOWERDOWN(0x11) 同 W (.cl/datasheet/pages/TMC5160A_Datasheet_Rev1.14.ch05.p038.md) 日期: 2026-09-09 来源: datasheet+实测
tmc5160_enc_const_readable = false    依据: ENC_CONST(0x3A) 标 W (ch06.p045), 回读恒0, 与 IHOLD_IRUN 同类; SPI 保真测试需 RW 寄存器时选 XTARGET(0x2D, ch06.p041 RW) 日期: 2026-09-10 来源: datasheet+实测(SOAK r1 教训)
tmc5160_spi_fidelity_6p45M = verified 依据: 2026-09-10 SOAK r3, U2 真实运转(VACTUAL=20000 恒速, CS_ACTUAL=20 满载流)噪声工况下 XTARGET 写读回显 222170 帧 + 静置 2000 帧 + GCONF 对照, 合计 0 bit 翻转 → 6.45Mbps 位保真成立, "H7 比 F407 快导致 SPI 位翻转"假设证伪 日期: 2026-09-10 来源: 实测
u2_bphase_s2gb_confirmed = true       依据: 2026-09-10 r7/r7b 扫描: DRVSTRENGTH{0,5,7 最强}×CS{20,8} 全 12 格 s2gb(ds bit28) 恒报 + GSTAT.drv_err 恒有 + olb 多格同报 + 轴零净转 → 驱动电流档位论排除(drvs 调寄存器的幅度救不了)；s2gb 语义=高边导通时 BMA/BMB 桥中点电压异常低(ch02 p13-14 HB/CB/BM 脚定义+Figure 11.1)，olb=电流不达标 → 组合=B 桥实际不导通且桥中点被拽低；候选=VSA=12V 时 11.5V 栅压稳压器(12VOUT)压差仅 0.5V 轨塌陷 / 桥中点自举电容(CB1/CB2)损坏 / 焊桥拉低 LA-BM 相邻脚(37-40 脚区) / B 低边管击穿。鉴别：24V 重跑全清=轨问题；表量 12VOUT<10.5V 静态=轨问题；断电机重跑仍 s2gb=板侧 日期: 2026-09-10 来源: 实测(r6+r7b)+ch02/ch11
u2_quiet_mode_s2vsb_confirmed = true  依据: 2026-09-10 静音(StealthChop)直调实测——GCONF=0x04 真实生效(DRV_STATUS.stealth bit14=1)下 U2 速度模式 1s: act=17801(斜坡正常推进) enc=13(100% 丢步不转), ds=0x40146000(olb=1 s2vsb=1 cs_actual=20, s2gb=0) gs=02(drv_err 锁桥) ferr=0; 对照 SpreadCycle r7 报 s2gb(bit28) → 同一 B 相桥臂故障在两种斩波模式下呈现不同检测标志(s2gb↔s2vsb+olb), 均锁桥不转 → B 相故障与斩波模式无关, 软件侧无解, 硬件定案(候选同 u2_bphase_s2gb_confirmed) 日期: 2026-09-10 来源: 实测(COMM_Test_SPI_Quiet)
u2_manual_baseline_s2gb_partial_rot = true  依据: 2026-09-10 手册基线(ch23 例 CHOPCONF=0x000100C5: TBL=36clk/HEND=1 + DRV_CONF 复位缺省 DRVSTRENGTH=medium(2) r7 唯一未测档)定位一整圈 51200µsteps: reached=1 act=33321(XACTUAL 自编码器同步位 17879 跑到目标) enc=8717(电机真实转 17% 圈后锁, **历轮首次观察到实际位移**) ds=0xD1140067(s2gb=1 olb=1 stst=1 SG=1 CS=20) gs=02; 至此软件维度全部穷尽(SpreadCycle 调优/手册基线/StealthChop × drvs{0,2,5,7} × CS{8,20} × SHORT_CONF 最低敏) s2gb 均在 → 硬件定案四候选: VSA=12V→12VOUT 0.5V 裕量(供电实况 VS=24V+VSA=12V 用户确认, 无法用"24V 重跑"鉴别, 需直接量 12VOUT 带载) / 自举电容 / 焊桥 / B低边管; "转 17% 后锁"轻微削弱"完全死短路"假设, 略强化"轨裕量瞬态塌陷"假设 日期: 2026-09-10 来源: 实测(COMM_Test_ManualRun)+ch22/ch23
u2_hb2_bmb2_zero_vgs_confirmed = true 依据: 2026-09-10 用户示波器/表实测——运转中 HB2 对 BMB2 无电压(=B2 高边栅驱动无输出, FET 永不导通) → 与 s2gb(检测高边压降, ch11 §11.2 p78)机制闭环, 候选②方向坐实; 注意判读边界: 锁桥后驱动器 disable 全部输出浮空, 表笔必读 0——有效测量窗=使能后锁桥前(~100ms 级), 待示波器 PA4 触发复核 + 补测 HB1-BMB1/LB1/LB2 + 断电电容档量 CB2-BMB2 判开路/短路 日期: 2026-09-10 来源: 你实测
u2_b_gate_rail_floating_fault = true 依据: 2026-09-10 用户实测——A 栅极对地 4.5V(正常斩波均), B 栅极对地 22V(≈VS, 异常); 且 VGS_B=0 已确认 → B 源极(BMB)同样浮 ~22V ≈ VS, B 桥中点无任何桥臂受控(HS 栅死+LS 未拉低) → B 线圈电流不受控 → olb/s2gb/丢步全解释; 排除: 换芯片/换自举电容/B 高边 MOS 本体 后仍复现 → 故障=B 相栅极或桥中点网络被板上拉到 VS 电位(候选: ①BMB↔VS 短路 ②栅↔漏焊桥 ③B 低边通路断致中点悬空), 对应断电三刀量测(对照 A 相): BMB↔GND 开路=低边链路断; BMB↔VS 0Ω=桥中点焊桥(头号嫌疑); 栅↔VS 0Ω=栅漏焊桥 日期: 2026-09-10 来源: 你实测(A=4.5V/B=22V/0V)
u2_fclk12_boot_test_hypothesis_rejected = true 依据: 2026-09-11 降 fCLK 15→12MHz(TIM4 Period=19, ch26 推荐带内)+SPI 6.45→3.225M(/32) 重跑手册基线整圈: act=51200 斜坡完整跑完, **enc=3072(6%后锁, 比 15MHz 轮 8717 更差)**, ds=0xD106203D(s2gb+s2vsb+olb 同报, cs 降至 6=IHOLD), gs=02; TOFF 绝对充电窗 12.3→15.3µs(+25%)反而更糟 → 用户"下管时间太短自举充不满"假设不成立; 至此时钟维度也排除: fCLK{15,12}×SPI{6.45,3.225}×斩波{SC调优,SC手册,StealthChop}×drvs{0,2,5,7}×CS{8,20} 全锁桥, 硬件定案(绕组对地绝缘/线缆为最后未测项, 板侧静态桥阻已证对称); [TM] 实测: 12MHz/3.225M 写 34µs 读 82µs(含 tCSH 10µs 两报间隔) 日期: 2026-09-11 来源: 实测
u2_noload_gates_all_conduct_board_cleared = true 依据: 2026-09-11 用户空载实测——电机断开状态下 A/B 两相栅极均有驱动、FET 都导通 → 板侧整链(12VOUT 轨/自举充电/B 高边驱动/栅极通路)无罪释放; 故障签名定型为"命令时空载好、电流一来(接上 B 负载)才炸 s2gb" → 唯一剩候选=**负载侧对地短路**(B 绕组对机壳 / 线缆磨破碰屏蔽, ch11: 短路电流绕开 RS → 斩波拉长导通 → HS 压降大 → S2G 触发); 待量插头端四阻值: B 两芯↔机壳(>10MΩ 正常)、A 两芯↔机壳(对照)、A↔B(相间应开路)、B 绕组 DC 阻 vs A 日期: 2026-09-11 来源: 你实测
u2_fault_follows_bridge_not_coil = true 依据: 2026-09-11 用户线圈对调实验——B 线圈接 A 桥、A 线圈接 B 桥后重跑: **仍是 A 好 B 锁(s2gb 不跟线圈走)** → 负载侧(线圈/线缆/连接器)双重无罪; 电源轨实测无跌落(候选①出局); 空载全导通(栅驱动链/自举/轨全好); 至此唯一存活假设=**B 桥大电流下通路电阻异常**: ① B 高边 FET 内伤(键合线部分 Lift/沟道损伤——万用表二极管档测不出, 小电流导通正常、3A 时 Rds 飙升→Vds 大→S2G 触发的正是高边压降 ch11; 两半周各用一条腿, 任一条 HS 内伤都报 s2gb) ② B 桥任一 FET 源/漏铜箔焊点虚焊微裂(同签名) ③ 微桥类缺陷(概率低); 处置: A/B 桥四只 FET 互换定位 或直接换 B 桥四只 AOD4126; 修后注意电机线恢复原位+闭环方向符号 日期: 2026-09-11 来源: 你实测(对调实验)
u2_s2g_per_bridge_shutdown_b_ls_off_explained = true 依据: 2026-09-11 用户实测"接电机后 B 相下管也不导通" + ch11.p079 Hint 原文"the corresponding driver bridge (A or B) becomes switched off" → TMC5160 短路响应=**分桥关断**(B 桥整桥 HS+LS 全关, A 桥继续) → "B 下管不导通"=s2gb 跳闸之果非第二故障, 并解释锁桥后 A 仍 1.45V 驱动的旧疑点(证据链闭环, 修正上轮"全局关断"说法); 新增量: 基线/12MHz 轮 ds 同含 s2gb(HS 压降大)+s2vsb(LS+RS 压降大=真实电流超额定 150-200%, §11.2) → **B 回路整体过流** → 嫌疑重排: ① 头号=B 相采样通路(RS-B 本体/焊点/SRBH-SRBL 开尔文抽气虚断→斩波见不到电流→满占空长期导通→远超 4.2A 峰值→双检测器齐跳; "空载开路无电流→好, 带载→炸"完美吻合) ② B 桥 FET Rds 退化 ③ BMB 网络非线性漏电(静态低阻测不出, 对调实验不排除——线圈换的是板上同一网络); 鉴别顺序: 断电量 RS-B 阻值+SRBH/SRBL 抽气通断+补焊 → A/B FET 互换 → 兆欧表/注 24V 测漏 日期: 2026-09-11 来源: 你实测+ch11.p079
u2_root_cause_kelvin_jumper_misplaced = **已修复** 依据: 2026-09-11 用户定案——**根因=板设计铺铜破坏了 RS-B 开尔文(SRBH/SRBL)走线, 补救跳线接错位置** → B 采样恒 0 → 斩波满占空 → 真实过流 → s2gb/s2vsb 分桥锁断(嫌疑①命中, 上条鉴别顺序正确); 全案证据链: s2gb 恒报与模式/频率/drvs/CS 无关(09-10 穷尽)+换片换电容无效+空载全导通(09-11)+线圈对调不离桥+电源无跌落+STC 轮 s2vsb(过流证据)+双检测器齐跳 → 教训固化: ①外部 MOS 方案**铺铜必须让采样电流与 SRBH/SRBL 抽气分离走线(真 Kelvin)**, 否则勿信单点量测 ②"空载正常/带载锁桥"签名=优先查电流反馈通路而非驱动通路 ③s2gb 是分桥关断(ch11.p079), A 活着 B 全灭=同一跳闸非两个故障 日期: 2026-09-11 来源: 你定案(修复确认)
u2_kelvin_fix_regression_pass = true 依据: 2026-09-11 开尔文跳线修正后回归——15MHz/6.45M 宏版 ManualRun 相对整圈 **连续 2× [MNLRUN PASS]: reached=1 act=51200 enc=51200(一整圈零丢步) ds=0x81140061(s2gb=0 s2vsb=0 olb=0, CS 正常) gs=00**; SPI [TM] 复验 写20us/读54us 与 09-09 一致(回退行为等价); 生产版(SPI_MANUAL=0)已烧, 心跳 act=102400(两圈累积) enc=0 锁轴 gs=00 健康 日期: 2026-09-11 来源: 实测
tmc5160_spi_all_zero_reads = chip_no_power_or_no_clk  依据: 2026-09-10 r5/r6 实测——TMC5160 无 VM(24V) 供电时 MISO 恒 0, 全部寄存器读回 0x00000000(非 0xFFFFFFFF), 连 DRV_STATUS.stst(静止必1)都为 0; 判活必须用"非零签名"(CHOPCONF 应=init 写入值)而非等值回显(死芯片读 0 会让 GCONF=0 对照组假通过) 日期: 2026-09-10 来源: 实测
tmc5160_spi_status_byte = 0x38 静止正常  依据: 回复帧 byte[39:32]=SPI_STATUS(bit5 pos_reached|bit4 vel_reached|bit3 standstill|bit1 drv_err|bit0 reset), 非 sync 字节, 静止态=0x38 (ch04 §4.1.2) 日期: 2026-09-09 来源: datasheet+实测
spi_spe_always_on_no_gain = true    依据: 2026-09-17 A/B 实测(寄存器级 S_SpiTransfer) SPE 常开 vs 逐帧开关: tag0 写事务 4437→4373cyc(-0.13µs), tag1 读事务 11311→11305cyc(~0) → ~3µs/帧固定开销不在 SPE 跨钟域同步; 已回退保留逐帧开关 日期: 2026-09-17 来源: 实测
tmc5160_tCSH_us = 5                  依据: tCSH>2×tCLK+10ns=320ns@6.45MHz, drv 读写帧后加 5us 间隔 (ch04 §4.3 表); 2026-09-17 修正 DWT 基准(实际 480MHz, 原按 240 使标称 10us 实际仅 5us)后 tCSH 标称 10→5us, 保持实际时序不变 日期: 2026-09-17 来源: datasheet, drv 已实施
u1_chip_populated = false             依据: comm_test 注释"芯片1未焊接", boot 自检仅测 U2 (PD3 CS) 日期: 2026-09-09 来源: 你(硬件)

## 无编码器回零 (StallGuard2, 2026-09-12 推导)
home_velocity_rps = 2               依据: ch13 §13.4 回零推荐 1~5RPS 区间内取值 日期: 2026-09-12 来源: 推导
home_vmax = 114532                  依据: 要求 2RPS → V=2×51200=102400µsteps/s → VMAX=V×2^24/fCLK=102400×16777216/15e6≈114532 (ch12 t=2^24/fCLK) 日期: 2026-09-12 来源: 推导
home_amax = 20000                   依据: 与参数组 4 同量级 → a≈2.05M µsteps/s², 0→2RPS 约 50ms/2560µsteps 起点须远于此 日期: 2026-09-12 来源: 推导
home_tcoolthrs = 200                依据: TSTEP=2^24/114532≈146 → 取 200 略高于 (ch13 §13.1 + ch05.p038 TCOOLTHRS≥TSTEP) 日期: 2026-09-12 来源: 推导
home_sgt = 0                        依据: ch13 §13.1 起始值；阈值待真机交互调定 日期: 2026-09-12 来源: 待实测确认
home_sg_confirm = 8                 依据: 用户定值（连续零确认抗抖） 日期: 2026-09-12 来源: 你
home_backoff = 2048 microsteps      依据: 用户定值（防卡死） 日期: 2026-09-12 来源: 你
home_sg_poll_ms = 2                 依据: fullstep 周期@2RPS=1/(2×200)=2.5ms → 取 2ms 覆盖每 fullstep（SG_RESULT 随 fullstep 更新, ch13 §13.1） 日期: 2026-09-17 来源: 推导
home_run_timeout_ms = 30000         依据: 机械行程未知 → 2RPS 下 30s=60 转作上限兜底（待实测确认） 日期: 2026-09-17 来源: 待实测确认
home_bo_timeout_ms = 10000          依据: 回退 2048µsteps 三角斜坡 t=2×√(2048/2.05e6)≈64ms → 取 10s 兜底堵死/异常（待实测确认） 日期: 2026-09-17 来源: 待实测确认

## 编码器配置
encoder_ppr = 1000 PPR               依据: 实测 ENC_CONST=12.8 反推 200×256/12.8=4000cpr÷4=1000PPR 日期: 2026-09-01 来源: 你实测
encoder_cpr = 4000 counts/rev        依据: 1000PPR ×4 正交解码 日期: 2026-09-01 来源: 推导
encoder_resolution = 0.09°/count     依据: 360°/4000 日期: 2026-09-01 来源: 推导
encoder_enc_const = 12.8             依据: TMC5160 ENC_CONST = FSC×USC/cpr = 200×256/4000 日期: 2026-09-01 来源: 推导
encoder_encmodes = 0x00              依据: 源工程 tmc5160_usr.c ENCMODE 写入值 日期: 2026-09-01 来源: 源工程
encoder_tol = 256 microsteps         依据: TMC5160_ENC_TOLERANCE 定义 日期: 2026-09-01 来源: 源工程
motor_fullsteps_per_rev = 200        依据: 1.8° 步距角 日期: 2026-08-24 来源: 电机规格书
motor_microsteps = 256               依据: CHOPCONF MRES 配置 日期: 2026-08-24 来源: 推导
motor_counts_per_rev = 51200         依据: 200×256=51200 微步/圈（实测确认）日期: 2026-09-01 来源: 你实测

## TMC5160 栅极驱动
tmc5160_drvstrength = 2              依据: AOD4126 Qgd=10nC, 中等驱动强度平衡开关速度与EMI 日期: 2026-08-24 来源: 推导
tmc5160_bbmtime = 16                 依据: 死区时间 200ns typ, 防止直通 日期: 2026-08-24 来源: 推导

## 斩波器配置 (初始值, 待实测调优)
chopper_toff = 5                     依据: Quick Configuration Guide 推荐起始值 日期: 2026-08-24 来源: 推导
chopper_tbl = 2                      依据: Quick Configuration Guide 推荐起始值 日期: 2026-08-24 来源: 推导
chopper_hstart = 4                   依据: Quick Configuration Guide 推荐起始值 日期: 2026-08-24 来源: 推导
chopper_hend = 0                     依据: Quick Configuration Guide 推荐起始值 日期: 2026-08-24 来源: 推导

## STM32 时钟树
stm32_hse = 8 MHz                    依据: CubeMX .ioc 日期: 2026-08-24 来源: 推导
stm32_sysclk = 480 MHz               依据: PLL1: 8MHz/1×120/2 日期: 2026-08-24 来源: 推导
stm32_hclk = 240 MHz                 依据: HPRE=DIV2 日期: 2026-08-24 来源: 推导
stm32_apb1 = 120 MHz                 依据: D2PPRE1=DIV2 日期: 2026-08-24 来源: 推导
stm32_apb2 = 120 MHz                 依据: D2PPRE2=DIV2 日期: 2026-08-24 来源: 推导
stm32_tim4clk = 240 MHz              依据: APB1=120MHz, APB1 timer×2 日期: 2026-08-24 来源: 推导
stm32_spi3clk = 103.2 MHz            依据: PLL3Q(2026-09-09 CubeMX 改); 旧值 64MHz(CLKP/HSI) 已废 日期: 2026-09-17 来源: CubeMX
stm32_dwt_cyccnt_clk = 480 MHz       依据: DWT CYCCNT 计数钟=CPU=SYSCLK(D1CPRE=DIV1), HCLK=240MHz 仅总线钟; OpenOCD 自由运行实测 962.5Mcyc/2s≈481MHz; 修正 tim_test.c 计量基准 240→480 日期: 2026-09-17 来源: 实测
stm32_usart1clk = 64 MHz             依据: HSI 源 日期: 2026-08-24 来源: 推导
stm32_fdcanclk = 8 MHz               依据: HSE 直接 日期: 2026-08-24 来源: 推导
