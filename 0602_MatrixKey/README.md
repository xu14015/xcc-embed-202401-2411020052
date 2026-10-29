# 实验二：3×4 矩阵键盘（计数消抖 + 长按连发版）

| 项目 | 内容 |
| --- | --- |
| 实验名称 | 3×4 矩阵键盘扫描与 BCD 显示 |
| 姓名 / 学号 | 汪传贵 / 2411020054 |
| 班级 | 24 电信 1 班 |
| 开发板 | STM32F407ZETX |
| 开发工具 | STM32CubeIDE 2.2.0 |
| 主频 | 168 MHz |

## 一、实验现象

按下任意按键，PF0～PF7 的 8 个 LED 以 **8421BCD 码**显示键值（1～12）：

- 个位 → PF0～PF3，十位 → PF4～PF7
- 按住不放超过 0.5 秒后，显示每隔 0.15 秒"重复触发"一次（长按连发），
  可以把键值当成可连续输入的按键使用

## 二、设计思路

### 1. 计数式消抖

每个按键配一个"命中计数器"：

- 本次采样值和当前稳定状态**不同** → 计数器 +1；
- **相同** → 计数器清零；
- 连续 `DEBOUNCE_HITS = 2` 次不一致 → 认为状态真的变了。

采样周期 3 ms，所以 2 次命中 ≈ 6 ms 消抖窗口。整个过程不调用 `HAL_Delay`，
主循环始终可以继续处理其它逻辑。

### 2. 事件队列

中断/扫描只负责把"按下了哪个键"放进环形队列（深度 8），
主循环用 `KEY_GetEvent()` 按自己的节奏取，扫描和显示互不阻塞。
队列满时丢弃新事件，保证扫描流程不被拖住。

### 3. 长按连发

按下时记录键值和时刻，保持按住超过 500 ms 后，每隔 150 ms 再投递一次同一键值——
效果类似键盘按住某键后的自动重复。

## 三、程序结构

| 文件 | 作用 |
| --- | --- |
| `Core/Src/main.c` | 初始化 + 主循环：轮询键盘、取事件、刷新 BCD |
| `Core/Src/keypad.c` | 行扫描、计数消抖、事件队列、长按连发 |
| `Core/Inc/keypad.h` | 对外接口：`KEY_Init` / `KEY_Poll` / `KEY_GetEvent` |
| `Core/Src/gpio.c` | CubeMX 生成的引脚初始化 |

主循环：

```c
while (1)
{
  KEY_Poll();                     /* 每 3 ms 真正采样一次 */

  event = KEY_GetEvent();
  if (event != KEY_EVENT_NONE)
  {
    led_show_bcd(event);          /* 有事件才刷新显示 */
  }
}
```

## 四、关键代码

### 1. 行扫描（BSRR 只动 PE4～PE6）

```c
static uint8_t read_row(uint8_t row)
{
  uint32_t pin = (uint32_t)ROW_FIRST_PIN << row;

  ROW_PORT->BSRR = (uint32_t)ROW_ALL_PINS;   /* 先全部拉高 */
  ROW_PORT->BSRR = pin << 16u;               /* 再拉低当前行 */

  for (volatile uint8_t settle = 0u; settle < 25u; settle++)
  {
    __NOP();                                 /* 等列线上拉恢复 */
  }

  return (uint8_t)(COL_PORT->IDR & (uint32_t)COL_ALL_PINS);
}
```

### 2. 计数消抖

```c
if (raw_down == s_stable[row][col])
{
  s_hits[row][col] = 0u;                     /* 一致，计数清零 */
  continue;
}

if (++s_hits[row][col] >= DEBOUNCE_HITS)     /* 连续 2 次不一致 */
{
  s_hits[row][col]   = 0u;
  s_stable[row][col] = raw_down;             /* 状态翻转 */
  ...
}
```

### 3. 长按连发

```c
if ((s_held_key != 0u) && ((int32_t)(now - s_repeat_at) >= 0))
{
  push_event(s_held_key);                    /* 再投递一次同一个键 */
  s_repeat_at = now + HOLD_REPEAT_MS;
}
```

### 4. BCD 显示（不用除法）

```c
static void led_show_bcd(uint8_t value)
{
  uint8_t tens = 0u;
  uint8_t ones = value;

  while (ones >= 10u)          /* 循环减法"数"出十位 */
  {
    ones = (uint8_t)(ones - 10u);
    tens++;
  }

  uint32_t bcd = ((uint32_t)tens << 4u) | (uint32_t)ones;
  uint32_t lit = bcd & LED_ALL_PINS;

  LED_PORT->BSRR = (LED_ALL_PINS & ~lit) | (lit << 16u);
}
```

## 五、键值映射

```c
static const uint8_t s_key_value[3][4] =
{
  {  9u, 10u, 11u, 12u },   /* 扫描行 0（板上第 3 行） */
  {  1u,  2u,  3u,  4u },   /* 扫描行 1（板上第 1 行） */
  {  5u,  6u,  7u,  8u }    /* 扫描行 2（板上第 2 行） */
};
```

如果上板发现键值对不上，只改这张二维表即可。

## 六、编译验证

| 方式 | 结果 |
| --- | --- |
| GNU Tools for STM32 14.3.rel1（`-Wall -Wextra`） | 22/22 文件编译通过，0 错误 0 警告，链接成功 |
| STM32CubeIDE 2.2.0 headless build | `Build Finished. 0 errors, 0 warnings` |

```
   text    data     bss     dec
   4928      12    1612    6552
```

## 七、使用方法

1. STM32CubeIDE → Import → Existing Projects into Workspace，选择本文件夹；
2. Build → Debug/Run 下载；
3. 逐个按键观察 LED 显示的 BCD 码；
4. 按住一个键不放，可以看到显示每隔 150 ms 重复刷新一次。

## 八、说明

- 采样周期 3 ms、消抖 2 次命中，可通过宏调整；
- 同时按下多个键时，按扫描顺序取先确认的那个；
- 长按连发只在"保持按住"时生效，松手立即停止；
- 队列深度 8，连续快速按键时不会丢事件。
