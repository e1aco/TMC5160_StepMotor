# .cl/memory/ — 配置推导值

> 写纪律见 `details/init.md`「memory 写纪律」：每条单行 ≤120 字符 / 只写当前有效值 / 禁过程叙事 / ≤200 行·12KB。
> 排查过程（s2gb/Kelvin 等）已移 `experience/log.md`；清理前全量快照见 git 提交 8db5a7b。

## 定论（已否决 / 已解决 · 勿重开）
spi_speed_downgrade = 否决: 6.45Mbps 过快致位翻转(降速论证伪)  依据: SOAK r3 22.4万帧 0 翻转 日期: 2026-09-10 来源: 实测
tmc_clk_12m_s2gb = 否决: 降 TMC_CLK 12MHz 可解 s2gb(实测 enc=3072 更差)  依据: log 2026-09-11 日期: 2026-09-11 来源: 实测
u2_s2gb_sw = 否决: s2gb 属斩波/驱动档位/SPI 速率软件问题(斩波双模式+DRVS×CS 12格+SPI 降速全穷尽)  依据: log 2026-09-10/11 日期: 2026-09-11 来源: 实测
spi_spe_always_on = 否决: SPE 常开可省时(A/B 实测 tag0 -0.13us≈0, 保留逐帧开关)  依据: 实测 日期: 2026-09-17 来源: 实测
u2_s2gb = 已解决: B 相 s2gb/100%丢步, 根因=RS-B 开尔文(SRBH/SRBL)走线被铺铜破坏+跳线错位  依据: 修后 ManualRun 2×整圈零丢步 日期: 2026-09-11 来源: 你定案

## 电机参数 (57CME13)
motor_phases = 2                    依据: 电机规格书 日期: 2026-08-24 来源: 推导
motor_hold_torque = 1.3 N·m         依据: 电机规格书 日期: 2026-08-24 来源: 推导
motor_step_angle = 1.8°             依据: 电机规格书(200 steps/rev) 日期: 2026-08-24 来源: 推导
motor_rated_current = 4 A           依据: 电机规格书 日期: 2026-08-24 来源: 推导
motor_phase_resistance = 0.42 Ω     依据: 电机规格书 日期: 2026-08-24 来源: 推导
motor_phase_inductance = 1.6 mH     依据: 电机规格书 日期: 2026-08-24 来源: 推导
motor_rotor_inertia = 3e-5 kg·m²    依据: 0.3kg·cm² 转换 日期: 2026-08-24 来源: 推导
motor_weight = 0.8 kg               依据: 电机规格书 日期: 2026-08-24 来源: 推导
motor_fullsteps_per_rev = 200       依据: 1.8° 步距角 日期: 2026-08-24 来源: 电机规格书
motor_microsteps = 256              依据: CHOPCONF MRES=0 日期: 2026-08-24 来源: 推导
motor_counts_per_rev = 51200        依据: 200×256 微步/圈(实测确认) 日期: 2026-09-01 来源: 实测

## MOSFET 参数 (AOD4126)
mosfet_vds = 100 V                  依据: AOD4126 规格书 日期: 2026-08-24 来源: 推导
mosfet_id_max = 43 A                依据: AOD4126 @VGS=10V 日期: 2026-08-24 来源: 推导
mosfet_rds_on = 24 mΩ               依据: AOD4126 @VGS=10V typ 日期: 2026-08-24 来源: 推导
mosfet_qg = 28 nC (typ)             依据: AOD4126 规格书 日期: 2026-08-24 来源: 推导
mosfet_qgd = 10 nC (typ)            依据: AOD4126 规格书 日期: 2026-08-24 来源: 推导
mosfet_vgs_th = 3.3 V (typ)         依据: AOD4126 规格书 日期: 2026-08-24 来源: 推导

## 外围电容选型 (TMC5160A 应用, ch03.p015-017)
board_bootstrap_cap = 470 nF ×4     依据: Qg>20nC→470nF(ch03.p015), 每半桥一只 日期: 2026-09-10 来源: 推导
board_12vout_cap = 10 µF            依据: ≥10×自举电容=4.7µF→取上限 10µF(ch02.p012) 日期: 2026-09-10 来源: 推导
board_pump_caps = CPI-CPO 22nF/100V, VCP-VS 100nF/16V  依据: ch02.p013 定值 日期: 2026-09-10 来源: datasheet
board_vs_bulk_cap >= 100µF/A       依据: ch03.p015 "100µF per ampere" → 4A ≥400µF 日期: 2026-09-10 来源: datasheet

## TMC5160 驱动配置
tmc5160_rs = 0.05 Ω                 依据: 硬件资源池 日期: 2026-08-24 来源: 你
tmc5160_vfs = 0.325 V               依据: ch28 感应电阻峰值阈值 VSRT 日期: 2026-08-24 来源: 推导
tmc5160_ifs = 6.5 A                 依据: VFS/RS=0.325/0.05 日期: 2026-08-24 来源: 推导
tmc5160_globalscaler = 256(复位默认) 依据: init 未写 GLOBALSCALER, 电流全靠 CS 分频 日期: 2026-09-10 来源: 代码核对
tmc5160_irun = 20                   依据: (20+1)/32×6.5/√2=3.02A RMS; CS=27 双机满流过热 OTPW→降 20 日期: 2026-09-01 来源: 实测校准
tmc5160_ihold = 6                   依据: (6+1)/32×6.5/√2≈1.01A RMS≈0.33N·m; 曾试 3(0.58A)锁不住回调 6 日期: 2026-09-10 来源: 实测
tmc5160_ihold_delay = 8             依据: IHOLDDELAY=8 → 2^18×8/15MHz≈140ms 后降 IHOLD 日期: 2026-09-10 来源: 代码核对
tmc5160_tpowerdown = 40             依据: 2^18×40/15MHz≈699ms; 需≥2 保证 StealthChop 自动调校 日期: 2026-09-10 来源: 代码核对

## TMC5160 时钟
tmc5160_clk_source = external       依据: PD14 TIM4_CH3 PWM 输出 日期: 2026-08-24 来源: 你
tmc5160_clk_freq = 15 MHz           依据: TIM4 PWM(Period=15); XACTUAL 17881/s 匹配 → 15.00MHz±0.01% 日期: 2026-09-11 来源: 实测
tmc5160_spi_max_sck = 7.5 MHz       依据: ch4 fSCK=fCLK/2=15/2 日期: 2026-09-11 来源: 推导

## TMC5160 SPI 配置
tmc5160_spi_mode = 3                依据: ch4 SPI MODE 3(CPOL=HIGH,CPHA=2EDGE) 日期: 2026-08-24 来源: 推导
tmc5160_spi_datasize = 8-bit        依据: 40-bit 数据报=8bit 地址+32bit 数据 日期: 2026-08-24 来源: 推导
stm32_spi_prescaler = 16            依据: SPI3 kernel=PLL3Q 103.2MHz ÷16=6.45MHz(≤7.5MHz) 日期: 2026-09-11 来源: 实测校准
stm32_spi_baudrate = 6.45 Mbit/s    依据: 103.2MHz/16; 寄存器级实测写 9.2us/读 23.6us(含片选) 日期: 2026-09-17 来源: 实测校准
stm32_spi_kernel_clk = 103.2 MHz    依据: PLL3 HSE8/M5 ×N129 /Q2 日期: 2026-09-09 来源: CubeMX+推导
tmc5160_tCSH_us = 5                 依据: tCSH>2×tCLK+10ns=320ns@6.45MHz, 读写帧后加 5us 日期: 2026-09-17 来源: datasheet
## TMC5160 通讯事实
tmc5160_spi_status_byte = 0x38      依据: byte[39:32]=SPI_STATUS, 静止态=0x38(ch04 §4.1.2) 日期: 2026-09-09 来源: datasheet+实测
tmc5160_ihold_irun_readable = false 依据: IHOLD_IRUN(0x10) 只写, 回读恒 0 日期: 2026-09-09 来源: datasheet+实测
tmc5160_enc_const_readable = false  依据: ENC_CONST(0x3A) 标 W, 回读恒 0; 保真测试用 XTARGET 日期: 2026-09-10 来源: datasheet+实测
tmc5160_spi_fidelity_6p45M = verified 依据: SOAK r3 运转下 XTARGET 222170 帧 0 翻转 日期: 2026-09-10 来源: 实测
tmc5160_spi_all_zero_reads = 无 VM 供电时 MISO 恒 0 依据: 判活须用非零签名(CHOPCONF), 非等值回显 日期: 2026-09-10 来源: 实测
u1_chip_populated = true            依据: U1 已焊 + SPI/CAN 测试通过 日期: 2026-09-16 来源: 实测

## 无编码器回零 (StallGuard2)
home_velocity_rps = 2               依据: ch13 §13.4 回零推荐 1~5RPS 区间 日期: 2026-09-12 来源: 推导
home_vmax = 114532                  依据: V=2×51200; VMAX=102400×2^24/15e6 日期: 2026-09-12 来源: 推导
home_amax = 20000                   依据: 与参数组 4 同量级 → a≈2.05M µsteps/s² 日期: 2026-09-12 来源: 推导
home_tcoolthrs = 200                依据: TSTEP≈146 → 取 200(ch13 §13.1) 日期: 2026-09-12 来源: 推导
home_sgt = 0                        依据: ch13 §13.1 起始值; 阈值待真机交互调定 日期: 2026-09-12 来源: 待实测确认
home_sg_confirm = 8                 依据: 用户定值(连续零确认抗抖) 日期: 2026-09-12 来源: 你
home_backoff = 2048 microsteps      依据: 用户定值(防卡死) 日期: 2026-09-12 来源: 你
home_sg_poll_ms = 2                 依据: fullstep 周期@2RPS=2.5ms → 取 2ms 覆盖每 fullstep 日期: 2026-09-17 来源: 推导
home_run_timeout_ms = 30000         依据: 机械行程未知 → 2RPS 下 30s 兜底 日期: 2026-09-17 来源: 待实测确认
home_bo_timeout_ms = 10000          依据: 回退 2048µsteps 约 64ms → 取 10s 兜底堵死 日期: 2026-09-17 来源: 待实测确认

## 编码器配置
encoder_ppr = 1000 PPR              依据: ENC_CONST=12.8 反推 200×256/12.8=4000cpr÷4 日期: 2026-09-01 来源: 实测
encoder_cpr = 4000 counts/rev       依据: 1000PPR ×4 正交解码 日期: 2026-09-01 来源: 推导
encoder_resolution = 0.09°/count    依据: 360°/4000 日期: 2026-09-01 来源: 推导
encoder_enc_const = 12.8            依据: ENC_CONST=FSC×USC/cpr=200×256/4000 日期: 2026-09-01 来源: 推导
encoder_encmodes = 0x00             依据: 源工程 ENCMODE 写入值 日期: 2026-09-01 来源: 源工程
encoder_tol = 256 microsteps        依据: TMC5160_ENC_TOLERANCE 定义 日期: 2026-09-01 来源: 源工程

## TMC5160 栅极驱动 / 斩波 (值取自 init 实写)
tmc5160_drvstrength = 2             依据: AOD4126 Qgd=10nC, 中等驱动强度 日期: 2026-08-24 来源: 推导
tmc5160_bbmtime = 16                依据: 死区 200ns typ, 防直通 日期: 2026-08-24 来源: 推导
tmc5160_chopconf = 0x000181C5       依据: TOFF=5/HSTRT=4/HEND=3/TBL=3(54clk)/MRES=0, 抑制 S2 误触发 日期: 2026-09-10 来源: 代码核对
chopper_toff = 5                    依据: ch01.p052 CHOPCONF bit[3:0] 日期: 2026-08-24 来源: 推导
chopper_tbl = 3                     依据: ch01.p052 CHOPCONF bit[16:15]=54clk 最长死区 日期: 2026-09-10 来源: 代码核对
chopper_hstart = 4                  依据: ch01.p052 CHOPCONF bit[6:4] 日期: 2026-09-10 来源: 代码核对
chopper_hend = 3                    依据: ch01.p052 CHOPCONF bit[10:7] 日期: 2026-09-10 来源: 代码核对

## FDCAN2 配置
fdcan2_baud = 500 kbit/s            依据: NominalPrescaler=1/Seg1=13/Seg2=2, 16TQ@8MHz HSE 日期: 2026-09-16 来源: CubeMX+推导
fdcan2_autoretransmit = ENABLE      依据: CubeMX 配置 日期: 2026-09-16 来源: CubeMX
fdcan2_msgram = ExtFiltersNbr=1/RxFifo0ElmtsNbr=8/TxFifoQueueElmtsNbr=3 依据: 移植适配, CubeMX 重生成需保留 日期: 2026-09-16 来源: 源工程

## STM32 时钟树
stm32_hse = 8 MHz                   依据: CubeMX .ioc 日期: 2026-08-24 来源: 推导
stm32_sysclk = 480 MHz              依据: PLL1 8MHz/1×120/2 日期: 2026-08-24 来源: 推导
stm32_hclk = 240 MHz                依据: HPRE=DIV2 日期: 2026-08-24 来源: 推导
stm32_apb1 = 120 MHz                依据: D2PPRE1=DIV2 日期: 2026-08-24 来源: 推导
stm32_apb2 = 120 MHz                依据: D2PPRE2=DIV2 日期: 2026-08-24 来源: 推导
stm32_tim4clk = 240 MHz             依据: APB1=120MHz, APB1 timer×2 日期: 2026-08-24 来源: 推导
stm32_spi3clk = 103.2 MHz           依据: PLL3Q(2026-09-09 CubeMX 改); 旧值 64MHz(CLKP) 已废 日期: 2026-09-17 来源: CubeMX
stm32_dwt_cyccnt_clk = 480 MHz      依据: DWT=CPU=SYSCLK(D1CPRE=1); HCLK=240 是总线钟; 实测 481MHz 日期: 2026-09-17 来源: 实测
stm32_usart1clk = 64 MHz            依据: HSI 源 日期: 2026-08-24 来源: 推导
stm32_fdcanclk = 8 MHz              依据: HSE 直接 日期: 2026-08-24 来源: 推导
