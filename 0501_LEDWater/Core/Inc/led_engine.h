/**
  ******************************************************************************
  * @file    led_engine.h
  * @brief   实验一：LED 流水灯效果引擎（定时器帧驱动）
  ******************************************************************************
  */

#ifndef __LED_ENGINE_H
#define __LED_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  初始化 LED 效果引擎：配置 TIM7 为 1 kHz 节拍并启动播放
  * @note   启动后所有帧切换都在中断里完成，主循环不需要参与
  */
void LED_Engine_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __LED_ENGINE_H */
