# 实验一：LED 流水灯（帧表 + 定时器驱动版）

| 项目 | 内容 |
| --- | --- |
| 实验名称 | LED 流水灯 |
| 姓名 / 学号 | 汪传贵 / 2411020054 |
| 班级 | 24 电信 1 班 |
| 开发板 | STM32F407ZETX |
| 开发工具 | STM32CubeIDE 2.2.0 |
| 主频 | 168 MHz |

## 一、实验现象

8 个 LED（PF0～PF7）按下面的顺序循环播放，每帧 100 ms：

1. 单个灯从 PF0 流到 PF7（8 帧）
2. 再从 PF7 流回 PF0（6 帧，不含两端）
3. 全亮 / 全灭闪两下（4 帧）
4. 交叉点亮：0x7E → 0xBD → 0xDB → 0xE7（4 帧）
5. 隔灯交替：0xAA / 0x55 各两帧（4 帧）

一整轮 26 帧 ≈ 2.6 秒，然后从头循环。

## 二、设计思路

这个版本把动画拆成"帧"，用**查表 + 定时器中断**来播：

```
s_frames[]  ← 每帧 1 字节，就是 PF0~PF7 的输出掩码
     │
     │  TIM7 每 1 ms 中断一次
     ▼
  计满 100 次（100 ms）→ 帧号 +1 → BSRR 一次性输出
```

三个关键点：

1. **帧表驱动**：想换效果只改 `s_frames[]` 数组，逻辑代码一行都不用动；
2. **硬件定时**：节奏来自 TIM7，不受主循环忙闲影响，也不会因为编译优化等级变化而跑偏；
3. **主循环休眠**：所有工作都在中断里做，`while(1)` 只需要 `__WFI()`，CPU 大部分时间在睡眠。

## 三、程序结构

| 文件 | 作用 |
| --- | --- |
| `Core/Src/main.c` | 初始化 + `while(1) __WFI()` |
| `Core/Src/led_engine.c` | 帧表、TIM7 配置、中断里切换帧并输出 |
| `Core/Inc/led_engine.h` | 对外只暴露 `LED_Engine_Init()` |
| `Core/Src/gpio.c` | CubeMX 生成的引脚初始化（PF0～PF7 推挽输出） |

## 四、关键代码

### 1. 帧表

```c
static const uint8_t s_frames[] =
{
  0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F,   /* 单灯自左向右 */
  0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD,               /* 再自右向左 */
  0x00, 0xFF, 0x00, 0xFF,                           /* 全亮 / 全灭闪两下 */
  0x7E, 0xBD, 0xDB, 0xE7,                           /* 交叉点亮 */
  0xAA, 0x55, 0xAA, 0x55                            /* 隔灯交替 */
};
```

掩码里 **bit = 0 表示该灯点亮**（低电平点亮）。

### 2. 一次 BSRR 输出整帧

```c
static void led_write_frame(uint8_t frame)
{
  uint32_t lit = (uint32_t)(~frame) & LED_ALL_PINS;   /* 要点亮的位 */
  uint32_t off = LED_ALL_PINS & ~lit;                 /* 要熄灭的位 */

  LED_PORT->BSRR = off | (lit << 16u);
}
```

`BSRR` 写一次就同时完成"点亮 + 熄灭"，不会出现中间态。

### 3. 定时器中断换帧

```c
void TIM7_IRQHandler(void)
{
  if ((TIM7->SR & TIM_SR_UIF) != 0u)
  {
    TIM7->SR = (uint32_t)~TIM_SR_UIF;      /* 写 0 清标志 */

    if (++s_tick_in_frame >= LED_FRAME_MS) /* 累计 100 ms */
    {
      s_tick_in_frame = 0u;
      s_frame_index = (s_frame_index + 1u) % FRAME_COUNT;
      led_write_frame(s_frames[s_frame_index]);
    }
  }
}
```

### 4. TIM7 时基

APB1 = 42 MHz → 定时器时钟 84 MHz：

- 预分频 84 → 计数频率 1 MHz
- 自动重装 1000 → 1 ms 一次更新中断

## 五、编译验证

| 方式 | 结果 |
| --- | --- |
| GNU Tools for STM32 14.3.rel1（`-Wall -Wextra`） | 22/22 文件编译通过，0 错误 0 警告，链接成功 |
| STM32CubeIDE 2.2.0 headless build | `Build Finished. 0 errors, 0 warnings` |

```
   text    data     bss     dec
   4468      12    1572    6052
```

## 六、使用方法

1. STM32CubeIDE → Import → Existing Projects into Workspace，选择本文件夹；
2. Build → Debug/Run 下载到开发板；
3. 观察 8 个 LED 的循环动画。

## 七、说明

- 帧切换在中断里完成，中断执行时间只有几微秒；
- `__WFI()` 让 CPU 在空闲时进入睡眠，任意中断到来都会自动唤醒；
- 想调整速度只改 `LED_FRAME_MS`（默认 100 ms）；
- PF0～PF7 之外的高位不参与输出，`LED_ALL_PINS` 限定为 `0x00FF`。
