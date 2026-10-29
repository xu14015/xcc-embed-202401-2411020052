/**
  ******************************************************************************
  * @file    keypad.c
  * @brief   实验二：3×4 矩阵键盘（计数消抖 + 长按连发）
  *
  * 思路：
  *   1. 每 3 ms 采样一次 12 个按键，每个按键配一个"命中计数器"：
  *      采样值和当前状态不同就 +1，相同就清零；连续命中 2 次才翻转状态。
  *      这种计数式消抖比"延时 16 ms"占用少，也不会阻塞其它逻辑。
  *   2. 按下瞬间产生一次事件；如果一直按住，超过 500 ms 后每 150 ms
  *      再产生一次，模拟键盘的自动连发。
  *   3. 事件先进环形队列，主循环用 KEY_GetEvent() 取，扫描和显示互不干扰。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "keypad.h"
#include "stm32f4xx_hal.h"

/* Private define ------------------------------------------------------------*/
#define ROW_NUM            3u
#define COL_NUM            4u

#define ROW_PORT           GPIOE
#define ROW_FIRST_PIN      GPIO_PIN_4                /* PE4~PE6 为行线输出 */
#define ROW_ALL_PINS       (GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6)

#define COL_PORT           GPIOE
#define COL_ALL_PINS       (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3)

#define POLL_PERIOD_MS     3u                        /* 采样周期 */
#define DEBOUNCE_HITS      2u                        /* 连续 2 次一致才认为稳定 */
#define HOLD_START_MS      500u                      /* 按住多久开始连发 */
#define HOLD_REPEAT_MS     150u                      /* 连发间隔 */

#define EVENT_QUEUE_LEN    8u

/* Private variables ---------------------------------------------------------*/

/* 键值表：s_key_value[扫描行][列] */
static const uint8_t s_key_value[ROW_NUM][COL_NUM] =
{
  {  9u, 10u, 11u, 12u },   /* 扫描行 0（板上第 3 行） */
  {  1u,  2u,  3u,  4u },   /* 扫描行 1（板上第 1 行） */
  {  5u,  6u,  7u,  8u }    /* 扫描行 2（板上第 2 行） */
};

static uint8_t  s_stable[ROW_NUM][COL_NUM];   /* 去抖后的状态：1=按下 */
static uint8_t  s_hits[ROW_NUM][COL_NUM];     /* 连续不一致的采样次数 */

static uint8_t  s_queue[EVENT_QUEUE_LEN];
static uint8_t  s_head;
static uint8_t  s_tail;
static uint8_t  s_count;

static uint8_t  s_held_key;                   /* 当前按住的键值，0=无 */
static uint32_t s_repeat_at;                  /* 下一次连发的时刻 */
static uint32_t s_last_poll;                  /* 上次采样时刻 */

/* Private functions ---------------------------------------------------------*/

/** 三根行线全部恢复高电平 */
static void rows_idle(void)
{
  ROW_PORT->BSRR = (uint32_t)ROW_ALL_PINS;
}

/**
  * @brief  把指定行拉低，返回该行四根列线的电平（bit=0 表示被拉低）
  */
static uint8_t read_row(uint8_t row)
{
  uint32_t pin = (uint32_t)ROW_FIRST_PIN << row;

  ROW_PORT->BSRR = (uint32_t)ROW_ALL_PINS;   /* 先全部拉高 */
  ROW_PORT->BSRR = pin << 16u;               /* 再拉低当前行 */

  /* 列线是上拉输入，切换行之后需要短暂时间稳定 */
  for (volatile uint8_t settle = 0u; settle < 25u; settle++)
  {
    __NOP();
  }

  return (uint8_t)(COL_PORT->IDR & (uint32_t)COL_ALL_PINS);
}

/** 事件入队，队列满则丢弃 */
static void push_event(uint8_t key)
{
  if (s_count >= EVENT_QUEUE_LEN)
  {
    return;
  }

  s_queue[s_tail] = key;
  s_tail = (uint8_t)((s_tail + 1u) % EVENT_QUEUE_LEN);
  s_count++;
}

/* Exported functions --------------------------------------------------------*/

void KEY_Init(void)
{
  uint8_t r;
  uint8_t c;

  for (r = 0u; r < ROW_NUM; r++)
  {
    for (c = 0u; c < COL_NUM; c++)
    {
      s_stable[r][c] = 0u;
      s_hits[r][c]   = 0u;
    }
  }

  s_head = 0u;
  s_tail = 0u;
  s_count = 0u;
  s_held_key = 0u;
  s_repeat_at = 0u;
  s_last_poll = HAL_GetTick();

  rows_idle();
}

void KEY_Poll(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t  row;
  uint8_t  col;
  uint8_t  cols;
  uint8_t  raw_down;
  uint8_t  key;

  /* 限制采样频率，避免主循环转太快把 CPU 占满 */
  if ((now - s_last_poll) < POLL_PERIOD_MS)
  {
    return;
  }
  s_last_poll = now;

  for (row = 0u; row < ROW_NUM; row++)
  {
    cols = read_row(row);

    for (col = 0u; col < COL_NUM; col++)
    {
      raw_down = ((cols & (uint8_t)(1u << col)) == 0u) ? 1u : 0u;

      if (raw_down == s_stable[row][col])
      {
        s_hits[row][col] = 0u;             /* 和当前状态一样，计数清零 */
        continue;
      }

      s_hits[row][col]++;
      if (s_hits[row][col] < DEBOUNCE_HITS)
      {
        continue;                          /* 还没连续够次数，再等等 */
      }

      /* 连续 DEBOUNCE_HITS 次都和状态不一致 → 状态翻转 */
      s_hits[row][col]   = 0u;
      s_stable[row][col] = raw_down;
      key = s_key_value[row][col];

      if (raw_down != 0u)
      {
        s_held_key = key;                  /* 记录按住的键 */
        s_repeat_at = now + HOLD_START_MS;
        push_event(key);                   /* 按下沿产生事件 */
      }
      else if (s_held_key == key)
      {
        s_held_key = 0u;                   /* 松手，停止连发 */
      }
    }
  }

  rows_idle();

  /* 长按连发：按住超过 HOLD_START_MS 后，每隔 HOLD_REPEAT_MS 再发一次 */
  if ((s_held_key != 0u) && ((int32_t)(now - s_repeat_at) >= 0))
  {
    push_event(s_held_key);
    s_repeat_at = now + HOLD_REPEAT_MS;
  }
}

uint8_t KEY_GetEvent(void)
{
  uint8_t key;

  if (s_count == 0u)
  {
    return KEY_EVENT_NONE;
  }

  key = s_queue[s_head];
  s_head = (uint8_t)((s_head + 1u) % EVENT_QUEUE_LEN);
  s_count--;

  return key;
}
