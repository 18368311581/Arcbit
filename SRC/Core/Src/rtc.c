#include "rtc.h"

#define RTC_BACKUP_MAGIC 0x4750U

static RTC_HandleTypeDef rtc_handle;

/* HAL RTC 的年份字段只有两位，本驱动固定映射为 2000～2099 年。 */
static uint8_t rtc_datetime_valid(const Rtc_datetime* datetime){
    uint8_t days;
    if(datetime==NULL){return 0U;}
    if(datetime->year<2000U||datetime->year>2099U){return 0U;}
    if(datetime->month<1U||datetime->month>12U){return 0U;}
    days=get_month_days(datetime->year,datetime->month);
    if(datetime->day<1U||datetime->day>days){return 0U;}
    if(datetime->weekday<RTC_WEEKDAY_MONDAY||datetime->weekday>RTC_WEEKDAY_SUNDAY){return 0U;}
    if(datetime->hour>23U||datetime->minute>59U||datetime->second>59U){return 0U;}
    return 1U;
}

uint8_t Rtc_init(void){
    RCC_PeriphCLKInitTypeDef clock={0};
    /* 访问 RTC 时钟源和备份寄存器前必须解除备份域写保护。 */
    HAL_PWR_EnableBkUpAccess();
    clock.PeriphClockSelection=RCC_PERIPHCLK_RTC;
    clock.RTCClockSelection=RCC_RTCCLKSOURCE_LSE;
    if(HAL_RCCEx_PeriphCLKConfig(&clock)!=HAL_OK){return 1U;}
    __HAL_RCC_RTC_ENABLE();
    rtc_handle.Instance=RTC;
    rtc_handle.Init.HourFormat=RTC_HOURFORMAT_24;
    rtc_handle.Init.AsynchPrediv=127U;
    rtc_handle.Init.SynchPrediv=255U;
    rtc_handle.Init.OutPut=RTC_OUTPUT_DISABLE;
    rtc_handle.Init.OutPutPolarity=RTC_OUTPUT_POLARITY_HIGH;
    rtc_handle.Init.OutPutType=RTC_OUTPUT_TYPE_OPENDRAIN;
    if(HAL_RTC_Init(&rtc_handle)!=HAL_OK){return 1U;}
    return 0U;
}

uint8_t Rtc_set_datetime(const Rtc_datetime* datetime){
    RTC_TimeTypeDef time={0};
    RTC_DateTypeDef date={0};
    if(!rtc_datetime_valid(datetime)){return 1U;}
    time.Hours=datetime->hour;
    time.Minutes=datetime->minute;
    time.Seconds=datetime->second;
    time.DayLightSaving=RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation=RTC_STOREOPERATION_RESET;
    date.Year=(uint8_t)(datetime->year-2000U);
    date.Month=datetime->month;
    date.Date=datetime->day;
    date.WeekDay=datetime->weekday;
    if(HAL_RTC_SetTime(&rtc_handle,&time,RTC_FORMAT_BIN)!=HAL_OK){return 1U;}
    if(HAL_RTC_SetDate(&rtc_handle,&date,RTC_FORMAT_BIN)!=HAL_OK){return 1U;}
    HAL_RTCEx_BKUPWrite(&rtc_handle,RTC_BKP_DR0,RTC_BACKUP_MAGIC);
    return 0U;
}

uint8_t Rtc_get_datetime(Rtc_datetime* datetime){
    RTC_TimeTypeDef time={0};
    RTC_DateTypeDef date={0};
    if(datetime==NULL){return 1U;}
    /*
     * 必须先读 Time 再读 Date，读 Date 会释放 RTC 影子寄存器锁，
     * 从而保证两次读取属于同一时刻。
     */
    if(HAL_RTC_GetTime(&rtc_handle,&time,RTC_FORMAT_BIN)!=HAL_OK){return 1U;}
    if(HAL_RTC_GetDate(&rtc_handle,&date,RTC_FORMAT_BIN)!=HAL_OK){return 1U;}
    datetime->year=(uint16_t)date.Year+2000U;
    datetime->month=date.Month;
    datetime->day=date.Date;
    datetime->weekday=date.WeekDay;
    datetime->hour=time.Hours;
    datetime->minute=time.Minutes;
    datetime->second=time.Seconds;
    return rtc_datetime_valid(datetime)?0U:1U;
}

uint8_t Rtc_is_valid(void){
    Rtc_datetime datetime;
    /* 备份标记只在完整设置日期和时间成功后写入。 */
    if(HAL_RTCEx_BKUPRead(&rtc_handle,RTC_BKP_DR0)!=RTC_BACKUP_MAGIC){return 0U;}
    return Rtc_get_datetime(&datetime)==0U?1U:0U;
}
