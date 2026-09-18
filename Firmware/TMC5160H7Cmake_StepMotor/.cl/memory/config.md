# .cl/memory/ — 配置推导值（terse）

> 写纪律见 `details/init.md`「memory 写纪律」：一行一条 `key = value  @依据  #来源  日期` / 只留当前有效值 / ≤200 行·12KB。
> `@` = 寄存器/位 / datasheet 章节 / 器件规格书 / 上游 memory key；`#` = 推导·实测·实测校准·你·datasheet·CubeMX·代码核对·待实测确认。
> 排查过程（s2gb/Kelvin）→ `experience/log.md`；全量快照见 git 8db5a7b。

## 定论（已否决 / 已解决）
spi_speed_downgrade = 否决: 6.45Mbps 过快致位翻转(降速论证伪)  @SOAK r3 0 翻转  #实测  2026-09-10
tmc_clk_12m_s2gb = 否决: 降 12MHz 可解 s2gb(enc=3072 更差)  @log 09-11  #实测  2026-09-11
u2_s2gb_sw = 否决: s2gb 属软件问题(斩波双模式+DRVS×CS+SPI 降速全穷尽)  @log 09-10/11  #实测  2026-09-11
spi_spe_always_on = 否决: SPE 常开可省时(A/B tag0 -0.13us≈0)  @实测  #实测  2026-09-17
u2_s2gb = 已解决: B 相 100%丢步, 根因 RS-B 开尔文走线被铺铜破坏+跳线错位  @ManualRun 2×整圈零丢步  #你定案  2026-09-11

## 电机参数 (57CME13)
motor_phases = 2  @电机规格书  #推导  2026-08-24
motor_hold_torque = 1.3 N·m  @电机规格书  #推导  2026-08-24
motor_step_angle = 1.8°  @电机规格书(200步/圈)  #推导  2026-08-24
motor_rated_current = 4 A  @电机规格书  #推导  2026-08-24
motor_phase_resistance = 0.42 Ω  @电机规格书  #推导  2026-08-24
motor_phase_inductance = 1.6 mH  @电机规格书  #推导  2026-08-24
motor_rotor_inertia = 3e-5 kg·m²  @0.3kg·cm²换算  #推导  2026-08-24
motor_weight = 0.8 kg  @电机规格书  #推导  2026-08-24
motor_fullsteps_per_rev = 200  @1.8°步距角  #规格书  2026-08-24
motor_microsteps = 256  @CHOPCONF.MRES  #推导  2026-08-24
motor_counts_per_rev = 51200  @200×256  #实测  2026-09-01

## MOSFET (AOD4126)
mosfet_vds = 100 V  @AOD4126  #推导  2026-08-24
mosfet_id_max = 43 A  @AOD4126 @VGS=10V  #推导  2026-08-24
mosfet_rds_on = 24 mΩ  @AOD4126 @VGS=10V typ  #推导  2026-08-24
mosfet_qg = 28 nC typ  @AOD4126  #推导  2026-08-24
mosfet_qgd = 10 nC typ  @AOD4126  #推导  2026-08-24
mosfet_vgs_th = 3.3 V typ  @AOD4126  #推导  2026-08-24

## 外围电容 (TMC5160A 应用)
board_bootstrap_cap = 470 nF ×4  @Qg>20nC→470nF, 每半桥一只  #推导  2026-09-10
board_12vout_cap = 10 µF  @≥10×自举电容→取上限  #推导  2026-09-10
board_pump_caps = CPI-CPO 22nF/100V, VCP-VS 100nF/16V  @ch02.p013  #datasheet  2026-09-10
board_vs_bulk_cap >= 100µF/A  @ch03.p015(4A→≥400µF)  #datasheet  2026-09-10

## TMC5160 驱动配置
tmc5160_rs = 0.05 Ω  @硬件资源池  #你  2026-08-24
tmc5160_vfs = 0.325 V  @ch28(VSRT)  #推导  2026-08-24
tmc5160_ifs = 6.5 A  @VFS/RS  #推导  2026-08-24
tmc5160_globalscaler = 256  @init 未写 GLOBALSCALER(复位默认)  #代码核对  2026-09-10
tmc5160_irun = 20  @IHOLD_IRUN; (20+1)/32×6.5/√2=3.02A RMS  #实测校准  2026-09-01
tmc5160_ihold = 6  @IHOLD_IRUN; (6+1)/32×6.5/√2≈1.01A(0.33N·m); 3 锁不住回调  #实测  2026-09-10
tmc5160_ihold_delay = 8  @IHOLD_IRUN.IHOLDDELAY; 2^18×8/15MHz≈140ms  #代码核对  2026-09-10
tmc5160_tpowerdown = 40  @TPOWERDOWN; 2^18×40/15MHz≈699ms  #代码核对  2026-09-10

## TMC5160 时钟
tmc5160_clk_source = external  @PD14 TIM4_CH3 PWM  #你  2026-08-24
tmc5160_clk_freq = 15 MHz  @TIM4 PWM; XACTUAL 17881/s 匹配 15.00MHz±0.01%  #实测  2026-09-11
tmc5160_spi_max_sck = 7.5 MHz  @ch4 fSCK=fCLK/2  #推导  2026-09-11

## TMC5160 SPI 配置
tmc5160_spi_mode = 3  @ch4 (CPOL=HIGH,CPHA=2EDGE)  #推导  2026-08-24
tmc5160_spi_datasize = 8-bit  @40bit 数据报=8bit 地址+32bit 数据  #推导  2026-08-24
stm32_spi_prescaler = 16  @SPI3 kernel PLL3Q 103.2MHz ÷16=6.45MHz  #实测校准  2026-09-11
stm32_spi_baudrate = 6.45 Mbit/s  @103.2MHz/16; 寄存器级写 9.2us/读 23.6us  #实测校准  2026-09-17
stm32_spi_kernel_clk = 103.2 MHz  @PLL3 HSE8/M5 ×N129 /Q2  #CubeMX  2026-09-09
tmc5160_tCSH_us = 5  @ch04 §4.3 (2×tCLK+10ns=320ns); 帧后加 5us  #datasheet  2026-09-17

## TMC5160 通讯事实
tmc5160_spi_status_byte = 0x38  @ch04 §4.1.2 (byte[39:32], 静止=0x38)  #datasheet+实测  2026-09-09
tmc5160_ihold_irun_readable = false  @ch05.p038 (只写, 回读恒 0)  #datasheet+实测  2026-09-09
tmc5160_enc_const_readable = false  @ENC_CONST 标 W; 保真测试用 XTARGET  #datasheet+实测  2026-09-10
tmc5160_spi_fidelity_6p45M = verified  @SOAK r3 XTARGET 222170 帧 0 翻转  #实测  2026-09-10
tmc5160_spi_all_zero_reads = 无 VM 供电时 MISO 恒 0  @判活须用非零签名  #实测  2026-09-10
u1_chip_populated = true  @U1 已焊 + SPI/CAN 测试通过  #实测  2026-09-16

## 无编码器回零 (StallGuard2)
home_velocity_rps = 2  @ch13 §13.4 (1~5RPS)  #推导  2026-09-12
home_vmax = 114532  @V=2×51200; 102400×2^24/15e6  #推导  2026-09-12
home_amax = 20000  @与参数组4同量级 → a≈2.05M µsteps/s²  #推导  2026-09-12
home_tcoolthrs = 200  @ch13 §13.1 (TSTEP≈146→取 200)  #推导  2026-09-12
home_sgt = 0  @ch13 §13.1 起始值; 阈值待真机调定  #待实测确认  2026-09-12
home_sg_confirm = 8  @连续零确认抗抖  #你  2026-09-12
home_backoff = 2048 microsteps  @防卡死  #你  2026-09-12
home_sg_poll_ms = 2  @fullstep 周期@2RPS=2.5ms  #推导  2026-09-17
home_run_timeout_ms = 30000  @机械行程未知 → 30s 兜底  #待实测确认  2026-09-17
home_bo_timeout_ms = 10000  @回退 2048≈64ms → 10s 兜底  #待实测确认  2026-09-17

## 编码器配置
encoder_ppr = 1000 PPR  @ENC_CONST=12.8 反推 200×256/12.8=4000cpr÷4  #实测  2026-09-01
encoder_cpr = 4000 counts/rev  @1000PPR ×4 正交  #推导  2026-09-01
encoder_resolution = 0.09°/count  @360°/4000  #推导  2026-09-01
encoder_enc_const = 12.8  @ENC_CONST=FSC×USC/cpr  #推导  2026-09-01
encoder_encmodes = 0x00  @源工程 ENCMODE 写入值  #源工程  2026-09-01
encoder_tol = 512 microsteps  @TMC5160_ENC_TOLERANCE  #实测  2026-09-18
encoder_lag_max = 263 microsteps  @G6 1442rpm巡航跟随滞后(到位回落Δ13)  #实测  2026-09-18

## TMC5160 栅极驱动 / 斩波
tmc5160_drvstrength = 2  @AOD4126 Qgd=10nC(中等强度)  #推导  2026-08-24
tmc5160_bbmtime = 16  @死区 200ns typ 防直通  #推导  2026-08-24
tmc5160_chopconf = 0x000181C5  @CHOPCONF; TOFF=5/HSTRT=4/HEND=3/TBL=3/MRES=0  #代码核对  2026-09-10
chopper_toff = 5  @ch01.p052 CHOPCONF[3:0]  #推导  2026-08-24
chopper_tbl = 3  @CHOPCONF[16:15]=54clk  #代码核对  2026-09-10
chopper_hstart = 4  @CHOPCONF[6:4]  #代码核对  2026-09-10
chopper_hend = 3  @CHOPCONF[10:7]  #代码核对  2026-09-10

## FDCAN2 配置
fdcan2_baud = 500 kbit/s  @NominalPrescaler=1/Seg1=13/Seg2=2, 16TQ@8MHz  #CubeMX  2026-09-16
fdcan2_autoretransmit = ENABLE  @CubeMX  #CubeMX  2026-09-16
fdcan2_msgram = EF=1/RxFifo0=8/TxFIFO=3  @CubeMX 重生成需保留  #源工程  2026-09-16

## STM32 时钟树
stm32_hse = 8 MHz  @.ioc  #推导  2026-08-24
stm32_sysclk = 480 MHz  @PLL1 8/1×120/2  #推导  2026-08-24
stm32_hclk = 240 MHz  @HPRE=DIV2  #推导  2026-08-24
stm32_apb1 = 120 MHz  @D2PPRE1=DIV2  #推导  2026-08-24
stm32_apb2 = 120 MHz  @D2PPRE2=DIV2  #推导  2026-08-24
stm32_tim4clk = 240 MHz  @APB1×2  #推导  2026-08-24
stm32_spi3clk = 103.2 MHz  @PLL3Q(2026-09-09 改)  #CubeMX  2026-09-17
stm32_dwt_cyccnt_clk = 480 MHz  @DWT=CPU=SYSCLK(D1CPRE=1); 实测 481MHz  #实测  2026-09-17
stm32_usart1clk = 64 MHz  @HSI  #推导  2026-08-24
stm32_fdcanclk = 8 MHz  @HSE  #推导  2026-08-24

## 调试通道
dbg_channel = RTT  @cl_config.h RTT_DBG=1/UART_DBG=0  #用户  2026-09-18
rtt_buffer_up = 1024 B  @SEGGER_RTT_Conf.h BUFFER_SIZE_UP  #推导  2026-09-18

## 主循环时序
main_loop_typ_us = 1  @main while(1) tag2抽稀500:1  #实测  2026-09-18
main_loop_worst_us = 625  @1Hz心跳圈tag5(含12×SPI读)  #实测  2026-09-18

## 高速边界 (24V+传送带)
vmax_stall_U2_rpm = (1462,1481]  @vel二分+RTT enc冻结判  #实测  2026-09-18
vmax_stall_U1_rpm = (1500,1572]  @U1@1481零滑动, 1572丢  #实测  2026-09-18
vel_cmd_unit = 寄存器原值  @SetVelocity直写VMAX, 物理=值/1.1178  #实测  2026-09-18
profile5_vmax = 572303  @10rps×51200×2^24/15e6, 600rpm生产档  #推导  2026-09-18
profile5_amax = 28000  @40000试探干净×0.7, 56rev/s²  #实测  2026-09-18
profile6_amax = 39200  @40000试探干净×0.98, 极限语义  #实测  2026-09-18
profile14_amax = 28000  @40000试探干净×0.7, G1~G4统一  #实测  2026-09-18
profile6_vmax = 1375452  @1442rpm=1471×0.98, 弱电机U2边界内  #推导  2026-09-18
profile6_scope = 极限运行专用  @生产禁用·值守·用后查失步  #你  2026-09-18
