#ifndef __BASIC_H__
#define __BASIC_H__
#ifdef __cplusplus
extern "C" {
#endif
#include "stm32f4xx_hal.h"
//#include <stdio.h>
//#include <string.h>
//#include <stdlib.h>
//#include <math.h>
#define NULL  0
#define TRUE  1
#define FALSE 0
#define device_identification   "STM32F411RE"
#define producer_identification "Inseven"
void Clock_init(void);
void Error_Handler(void);
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
void delay_us(uint32_t nus);
void delay_ms(uint16_t nms);
float fast_rsqrt(float number);
uint8_t char_upper(uint8_t input_char);
uint8_t char_lower(uint8_t input_char);
uint8_t is_leap(uint16_t year);
uint8_t get_month_days(uint16_t year,uint8_t month);
uint8_t bin_to_bcd(uint8_t val);
uint8_t bcd_to_bin(uint8_t val);
uint16_t color_blend(uint16_t c1,uint16_t c2,uint16_t t,uint16_t max);
uint16_t smart_color_reverse(uint16_t color);
uint8_t conv_string_to_float(char* s,float* x);
uint8_t conv_string_to_int32(char* s,int32_t* x);
extern const int16_t math_sin_table[360];
extern const int16_t math_cos_table[360];
int16_t math_sin_q15(uint16_t angle);
int16_t math_cos_q15(uint16_t angle);
uint8_t smart_calc_from_str(char* str,double* answer,double x,double y);
void float_string_refine(char* str);
extern const uint8_t asc_1206[95][12];
extern const uint8_t asc_1608[95][16];
extern const uint8_t asc_2412[95][36];
extern const uint8_t asc_3216[95][64];
#ifdef __cplusplus
}
#endif
#endif
