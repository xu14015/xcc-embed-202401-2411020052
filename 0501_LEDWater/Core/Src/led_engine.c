/**
  ******************************************************************************
  * @file    led_engine.c
  * @brief   实验一：LED 流水灯效果引擎（定时器帧驱动）
  *
  * 思路：
  *   把整个动画拆成一串"帧"，每帧就是 PF0~PF7 的 8 位输出掩码，
  *   全部放在一张常量表里；TIM7 每 1 ms 进一次中断，累计 100 ms
  *   就切到下一帧并直接写 BSRR 输出。
  *
  *   这样做的好处：
  *     - 动画节奏由硬件定时器保证，不受主循环忙不忙影响；
  *     - 想换效果只改 s_frames[] 表，不用动逻辑；
  *     - 主循环可以整个 __WFI() 睡下去，功耗最低。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "led_engine.h"
#include "stm32f4xx_hal.h"

/* Private define ------------------------------------------------------------*/
#define LED_PORT           GPIOF
#define LED_ALL_PINS       0x00FFu        /* PF0~PF7，8 个 LED，低电平点亮 */
#define LED_FRAME_MS       100u           /* 每帧停留 100 ms */

#define TIM7_CLK_HZ        84000000u      /* APB1 = 42 MHz，定时器时钟 84 MHz */
#define TIM7_PSC           (TIM7_CLK_HZ / 1000000u)   /* 计数频率 1 MHz */
#define TIM7_ARR           (1000000u / 1000u)         /* 1000 次 = 1 ms */

/* Private variables ---------------------------------------------------------*/

/**
  * 动画帧表：每帧一个字节，bit = 0 表示该位 LED 点亮。
  * 0xFE = 1111_1110 → 只有 PF0 亮，以此类推。
  */
static const uint8_t s_frames[] =
{
  0xFEu, 0xFDu, 0xFBu, 0xF7u, 0xEFu, 0xDFu, 0xBFu, 0x7Fu,   /* 单灯自左向右 */
  0xBFu, 0xDFu, 0xEFu, 0xF7u, 0xFBu, 0xFDu,                 /* 再自右向左回来 */
  0x00u, 0xFFu, 0x00u, 0xFFu,                               /* 全亮 / 全灭闪两下 */
  0x7Eu, 0xBDu, 0xDBu, 0xE7u,                               /* 交叉点亮 */
  0xAAu, 0x55u, 0xAAu, 0x55u                                /* 隔灯交替 */
};

#define FRAME_COUNT   (sizeof(s_frames) / sizeof(s_frames[0]))

static volatile uint16_t s_frame_index;
static volatile uint16_t s_tick_in_frame;

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  把一帧掩码输出到 LED
  * @param  frame 掩码，bit=0 → 输出低电平 → 点亮
  * @note   一次 BSRR 写入同时完成"点亮"和"熄灭"，没有中间态
  */
static void led_write_frame(uint8_t frame)
{
  uint32_t lit = (uint32_t)(~frame) & LED_ALL_PINS;    /* 需要点亮的位 */
  uint32_t off = LED_ALL_PINS & ~lit;                  /* 需要熄灭的位 */

  LED_PORT->BSRR = off | (lit << 16u);
}

/* Exported functions --------------------------------------------------------*/

void LED_Engine_Init(void)
{
  /* 1. LED 全灭（输出高电平），动画从第 0 帧开始 */
  LED_PORT->BSRR = LED_ALL_PINS;
  s_frame_index  = 0u;
  s_tick_in_frame = 0u;

  /* 2. 打开 TIM7 时钟并配置为 1 ms 更新一次 */
  RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;

  TIM7->PSC = (uint32_t)(TIM7_PSC - 1u);
  TIM7->ARR = (uint32_t)(TIM7_ARR - 1u);

  TIM7->EGR = TIM_EGR_UG;          /* 立即装载分频与重装值 */
  TIM7->SR  = 0u;                  /* 清掉 UG 产生的更新标志 */

  TIM7->DIER = TIM_DIER_UIE;
  TIM7->CR1  = TIM_CR1_CEN;

  /* 3. 使能中断，优先级低于 SysTick */
  NVIC_SetPriority(TIM7_IRQn, 3u);
  NVIC_EnableIRQ(TIM7_IRQn);
}

/**
  * @brief  TIM7 更新中断：1 ms 一次，累计到 100 ms 切换一帧
  */
void TIM7_IRQHandler(void)
{
  if ((TIM7->SR & TIM_SR_UIF) != 0u)
  {
    TIM7->SR = (uint32_t)~TIM_SR_UIF;      /* 状态位写 0 清除 */

    if (++s_tick_in_frame >= LED_FRAME_MS)
    {
      s_tick_in_frame = 0u;

      s_frame_index++;
      if (s_frame_index >= FRAME_COUNT)
      {
        s_frame_index = 0u;                /* 播完从头循环 */
      }

      led_write_frame(s_frames[s_frame_index]);
    }
  }
}
