#ifndef __RTC_H__
#define __RTC_H__
#ifdef __cplusplus
extern "C" {
#endif
#include "basic.h"

/* RTC 日期时间使用 2000～2099 年、24 小时制，weekday 范围为 1～7。 */
typedef struct{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
}Rtc_datetime;

/* 使用 LSE 和 127/255 预分频初始化 RTC，不覆盖已经保持的日期时间。 */
uint8_t Rtc_init(void);
/* 设置/读取日期时间。返回 0 成功，非零表示参数或 HAL 操作失败。 */
uint8_t Rtc_set_datetime(const Rtc_datetime* datetime);
uint8_t Rtc_get_datetime(Rtc_datetime* datetime);
/* 同时检查备份标记和当前日期时间，返回 1 有效，0 无效。 */
uint8_t Rtc_is_valid(void);
#ifdef __cplusplus
}
#endif
#endif
