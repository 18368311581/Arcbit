#ifndef __KEY_H__
#define __KEY_H__
#ifdef __cplusplus
extern "C" {
#endif
#include "basic.h"

/* 方向键和矩阵列均为上拉输入，按下为低电平；矩阵行逐行拉低扫描。 */
#define KEY_ROW0_Pin            GPIO_PIN_0
#define KEY_ROW0_GPIO_Port      GPIOC
#define KEY_ROW1_Pin            GPIO_PIN_1
#define KEY_ROW1_GPIO_Port      GPIOC
#define KEY_ROW2_Pin            GPIO_PIN_2
#define KEY_ROW2_GPIO_Port      GPIOC
#define KEY_COL0_Pin            GPIO_PIN_3
#define KEY_COL0_GPIO_Port      GPIOC
#define KEY_COL1_Pin            GPIO_PIN_4
#define KEY_COL1_GPIO_Port      GPIOC
#define KEY_COL2_Pin            GPIO_PIN_5
#define KEY_COL2_GPIO_Port      GPIOC
#define KEY_UP_Pin              GPIO_PIN_6
#define KEY_UP_GPIO_Port        GPIOC
#define KEY_DOWN_Pin            GPIO_PIN_7
#define KEY_DOWN_GPIO_Port      GPIOC
#define KEY_LEFT_Pin            GPIO_PIN_8
#define KEY_LEFT_GPIO_Port      GPIOC
#define KEY_RIGHT_Pin           GPIO_PIN_9
#define KEY_RIGHT_GPIO_Port     GPIOC
#define KEY_UP                  (1U<<0)
#define KEY_DOWN                (1U<<1)
#define KEY_LEFT                (1U<<2)
#define KEY_RIGHT               (1U<<3)
#define KEY_MATRIX(row,col)     (1U<<(4U+(uint16_t)(row)*3U+(uint16_t)(col)))
#define KEY_L                   KEY_MATRIX(0U,0U)
#define KEY_HOME                KEY_MATRIX(0U,1U)
#define KEY_R                   KEY_MATRIX(0U,2U)
#define KEY_MENU                KEY_MATRIX(1U,0U)
#define KEY_Y                   KEY_MATRIX(1U,1U)
#define KEY_X                   KEY_MATRIX(1U,2U)
#define KEY_PAUSE_EXIT          KEY_MATRIX(2U,0U)
#define KEY_B                   KEY_MATRIX(2U,1U)
#define KEY_A                   KEY_MATRIX(2U,2U)
#define KEY_ALL                 0x1FFFU

/* 初始化 4 个方向键和 3×3 按键矩阵。 */
void Key_init(void);
/* 每次主循环调用；内部限制为 5 ms 采样并完成约 10 ms 去抖。 */
void Key_scan(void);
/* 返回当前稳定按键状态，位定义见 KEY_UP～KEY_A。 */
uint16_t Key_get_state(void);
/* 返回按下/释放边沿；读取后对应边沿状态清零。 */
uint16_t Key_get_pressed(void);
uint16_t Key_get_released(void);

#ifdef __cplusplus
}
#endif
#endif
