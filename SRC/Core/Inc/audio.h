#ifndef __AUDIO_H__
#define __AUDIO_H__
#ifdef __cplusplus
extern "C" {
#endif
#include "basic.h"

/* PB6 输出 TIM4_CH1 PWM；PAM8302A SD# 低电平关闭功放。 */
#define AUDIO_PWM_Pin           GPIO_PIN_6
#define AUDIO_PWM_GPIO_Port     GPIOB
#define AMP_SD_Pin              GPIO_PIN_7
#define AMP_SD_GPIO_Port        GPIOB
#define AUDIO_SEQUENCE_MAX      128U
#define AUDIO_VOLUME_LEVEL_COUNT 5U
#define AUDIO_VOLUME_MAX        (AUDIO_VOLUME_LEVEL_COUNT-1U)

/* frequency=0 表示休止符，duration_ms 以毫秒为单位。 */
typedef struct{
    uint16_t frequency;
    uint16_t duration_ms;
}Audio_note;

/* 初始化 1 MHz 定时器基准，默认停止 PWM 并关闭功放。 */
void Audio_init(void);
/* 设置0～4档音量；0为静音，4为50%占空比最大音量。 */
void Audio_set_volume(uint8_t volume);
uint8_t Audio_get_volume(void);
/* 播放20～20000 Hz音调。返回0成功，非零失败。 */
uint8_t Audio_play(uint16_t frequency);
/*
 * 后台播放固定长度音符序列，最多 128 个音符。
 * 时长以毫秒为单位；新序列会立即替换当前播放内容。
 */
uint8_t Audio_play_sequence(const Audio_note* notes,uint8_t count);
/* 返回 1 表示当前存在音调或音符序列。 */
uint8_t Audio_is_busy(void);
/* 先关闭功放，再停止 PWM。 */
void Audio_stop(void);
#ifdef __cplusplus
}
#endif
#endif
