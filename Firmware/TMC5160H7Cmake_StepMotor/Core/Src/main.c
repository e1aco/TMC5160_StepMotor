/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fdcan.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "algo/queue.h"
#include "drv/can.h"
#include "drv/uart_dbg.h"
#include "drv/rtt_dbg.h"
#include "drv/tmc5160.h"
#include "app/motor_ctrl.h"
#include "app/closed_loop.h"
#include "app/homing.h"
#include "comm_test.h"
#include "tim_test.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 闭环节拍标志：预留（目标板无 TIM7，闭环 Tick 暂未启用，与源工程行为等价） */
volatile uint8_t g_cl_tick_flag = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_FDCAN2_Init();
  MX_SPI3_Init();
  MX_TIM4_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  /* TMC5160 外部时钟: TIM4_CH3 PWM 输出 15MHz (TIM4CLK=240MHz/(PSC=0)/(ARR=15+1))
   * 必须在 TMC5160_Init 前启动，否则芯片无 fCLK 不工作 */
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);

  QUEUE_Init(&g_queue_st);
  CAN_Init();
  TMC5160_Init();
  MOTOR_Init();
  CLOSEDLOOP_Init();
  UART_DBG_Init();
  //RTT_DBG_Init(); // RTT初始化
  UART_DBG_Str("UART ready 115200\r\n");
  RTT_DBG_Str("RTT ready\r\n");
  UART_DBG_Str("[BOOT] TMC5160H7 StepMotor\r\n");
  RTT_DBG_Str("[BOOT] TMC5160H7 StepMotor\r\n");
#ifdef CL_TIMING_MEASURE
  TEST_TIM_Init();
  TEST_SPI_SaleaeTriggerInit();
  UART_DBG_Str("[SALEAE] 3-wire SCK PC10 MOSI PC12 MISO PC11 TRIG PA4\r\n");
#endif

  /* == 测试函数 == */
  /* 上电 SPI 自检：双芯 GSTAT/DRVSTATUS 回读，串口+RTT 双通道输出判据 */
  // COMM_Test_SPI();
#if SPI_SOAK
  /* SPI 位保真浸泡诊断（2026-09-10 电机排查轮，实现见 test/comm_test.c；
   * 含 U2 速度模式运转 10s，结束自动停并恢复 ENC_CONST；生产版置 0） */
  COMM_Test_SPI_Soak();
#endif
#if SPI_QUIET
  /* 静音模式(StealthChop)电机运行测试（2026-09-10 用户定案，实现见
   * test/comm_test.c；判据=转+无故障，S2 保持开启；结束恢复 GCONF=0x00） */
  COMM_Test_SPI_Quiet();
#endif
#if SPI_MANUAL
  /* 手册(ch22/ch23)基线配置+一整圈定位运转（2026-09-10 用户定案，实现见
   * test/comm_test.c；判据=|enc|≥25600 且 S2/drv_err 全 0；VS=24V+VSA=12V 实况） */
  COMM_Test_ManualRun();
#endif
#if SPI_PRODRUN
  /* 生产配置 SpreadCycle 整圈运行验证（2026-09-12 用户定案，实现见
   * test/comm_test.c；判据=到位且全程无错误标志+enc跟随；生产版置 0） */
  COMM_Test_ProdRun();
#endif
#if SPI_PRODRUN_U1
  /* U1 生产配置 SpreadCycle 整圈运行验证（2026-09-14 新增，
   * 实现见 test/comm_test.c COMM_Test_ProdRun_U1()；
   * 判据对标 PRODRUN；生产版置 0） */
  COMM_Test_ProdRun_U1();
#endif

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
#ifdef CL_TIMING_MEASURE
    /* 3-wire Saleae 连续触发: 每 10ms 一次 U2 GSTAT 读, PA4 窄脉冲作同步 */
    {
        static uint32_t s_saleae_last = 0;
        if ((HAL_GetTick() - s_saleae_last) >= 10)
        {
            s_saleae_last = HAL_GetTick();
            TEST_SPI_SaleaePulse();
            (void)TMC5160_SpiRead(TMC5160_CHIP_2, 0x01); /* GSTAT */
        }
    }
#endif
    CAN_Process();
    HOME_Tick();
    COMM_Test_CAN_Heartbeat();
    /* 双通道心跳遥测：1Hz 打印双电机实际/编码器位置 (USART1 115200 + RTT) */
    {
      static uint32_t s_rtt_last_tick = 0;
      uint32_t now = HAL_GetTick();
      if ((now - s_rtt_last_tick) >= 1000)
      {
        s_rtt_last_tick = now;
        /* U2 追加运动诊断量: v=VACTUAL(0x22) rs=RAMP_STAT cs=CS_ACTUAL[9:0]
         * ds=DRVSTATUS(OL/OT/S2 位) gs=GSTAT —— 遥测先行定位抖动 (retrieval.md) */
        {
          TMC5160_CHIP_T *u2 = MOTOR_GetChip(MOTOR_CTRL_U2);
          uint32_t ds = TMC5160_GetDrvStatus(u2);
          UART_DBG_Printf("[t=%u] U1 act=%d enc=%d | U2 act=%d enc=%d "
                          "v=%d rs=%lX cs=%lu ds=%08lX gs=%02X\r\n",
                          (unsigned)now,
                          (int)MOTOR_GetPosition(MOTOR_CTRL_U1),
                          (int)MOTOR_GetEncoderPosition(MOTOR_CTRL_U1),
                          (int)MOTOR_GetPosition(MOTOR_CTRL_U2),
                          (int)MOTOR_GetEncoderPosition(MOTOR_CTRL_U2),
                          (int)TMC5160_GetVelocity(u2),
                          (unsigned long)TMC5160_GetRampStat(u2),
                          (unsigned long)(ds & 0x3FFUL),
                          (unsigned long)ds,
                          (unsigned int)TMC5160_GetGStat(u2));
        }
        RTT_DBG_Printf("[t=%u] U1 act=%d enc=%d | U2 act=%d enc=%d\r\n",
                       (unsigned)now,
                       (int)MOTOR_GetPosition(MOTOR_CTRL_U1),
                       (int)MOTOR_GetEncoderPosition(MOTOR_CTRL_U1),
                       (int)MOTOR_GetPosition(MOTOR_CTRL_U2),
                       (int)MOTOR_GetEncoderPosition(MOTOR_CTRL_U2));
      }
    }

    /* 闭环控制：目标板无 TIM7 节拍定时器，保持注释态（与源工程行为等价） */
    // if (g_cl_tick_flag)
    // {
    //   g_cl_tick_flag = 0;
    //   CLOSEDLOOP_Tick(MOTOR_CTRL_U1);
    //   CLOSEDLOOP_Tick(MOTOR_CTRL_U2);
    // }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 120;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2; 
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_CKPER;
  PeriphClkInitStruct.CkperClockSelection = RCC_CLKPSOURCE_HSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

