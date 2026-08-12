#ifndef __POWER_H__
#define __POWER_H__
#ifdef __cplusplus
extern "C" {
#endif
#include "basic.h"

/* LTC2951 INT#、KILL# 均按低有效处理；BAT_SENSE_EN 高电平使能分压。 */
#define BAT_ADC_Pin             GPIO_PIN_0
#define BAT_ADC_GPIO_Port       GPIOA
#define BAT_SENSE_EN_Pin        GPIO_PIN_1
#define BAT_SENSE_EN_GPIO_Port  GPIOA
#define POWER_INT_Pin           GPIO_PIN_10
#define POWER_INT_GPIO_Port     GPIOC
#define POWER_KILL_Pin          GPIO_PIN_11
#define POWER_KILL_GPIO_Port    GPIOC

/* 初始化电源控制 GPIO 和 ADC1。返回 0 成功，非零失败。 */
uint8_t Power_init(void);
/* 返回 1 表示 LTC2951 已请求普通关机；请求会被中断锁存。 */
uint8_t Power_shutdown_requested(void);
/* 读取平均 ADC 原始值或按理论分压换算的电池毫伏值。 */
uint8_t Power_read_battery_raw(uint16_t* raw);
uint8_t Power_read_battery_mv(uint32_t* millivolts);
/* 拉低 KILL# 触发最终断电；若硬件未断电则函数不会返回。 */
void Power_shutdown(void);
/* 早期的电源稳定 */
void Hold_power_early(void);

#ifdef __cplusplus
}
#endif
#endif
