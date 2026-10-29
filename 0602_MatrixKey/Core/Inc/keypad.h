/**
  ******************************************************************************
  * @file    keypad.h
  * @brief   实验二：3×4 矩阵键盘（计数消抖 + 长按连发）
  ******************************************************************************
  */

#ifndef __KEYPAD_H
#define __KEYPAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** 没有事件时的返回值 */
#define KEY_EVENT_NONE   0u

/* 对外接口 ----------------------------------------------------------------*/

/** 初始化：清空状态，行线拉高 */
void KEY_Init(void);

/**
  * @brief  周期调用（主循环里调），内部限制为每 3 ms 采样一次
  * @note   不阻塞，未到采样时刻立即返回
  */
void KEY_Poll(void);

/**
  * @brief  取一个按键事件
  * @retval 0 = 无事件；1~12 = 键值
  * @note   首次按下会产生一次事件，按住超过 500 ms 后每 150 ms 再产生一次（长按连发）
  */
uint8_t KEY_GetEvent(void);

#ifdef __cplusplus
}
#endif

#endif /* __KEYPAD_H */
