#include "rng.h"
#include "basic.h"

static uint32_t rng_state;

/* 对启动时可取得的计数与芯片信息做一次快速混合。 */
static uint32_t rng_mix(uint32_t value){
    value^=value>>16;
    value*=0x7FEB352DU;
    value^=value>>15;
    value*=0x846CA68BU;
    value^=value>>16;
    return value;
}

void Rng_init(void){
    const uint32_t* uid=(const uint32_t*)UID_BASE;
    uint32_t seed;
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT=0U;
    DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
    seed=uid[0]^rng_mix(uid[1])^rng_mix(uid[2]);
    seed^=DWT->CYCCNT^SysTick->VAL^HAL_GetTick()^RTC->SSR;
    rng_state=rng_mix(seed);
    if(rng_state==0U){rng_state=0x6D2B79F5U;}
}

uint32_t Rng_get_int(void){
    uint32_t value;
    if(rng_state==0U){Rng_init();}
    /* 调用时刻的周期计数可降低每次上电得到相同序列的概率。 */
    rng_state^=DWT->CYCCNT+0x9E3779B9U;
    value=rng_state;
    value^=value<<13;
    value^=value>>17;
    value^=value<<5;
    rng_state=value==0U?0x6D2B79F5U:value;
    return rng_state;
}

int Rng_get_range_int(int min,int max){
    int temp;
    uint64_t span64;
    uint32_t span;
    uint32_t threshold;
    uint32_t value;
    if(min>max){temp=min;min=max;max=temp;}
    span64=(uint64_t)((int64_t)max-(int64_t)min)+1U;
    if(span64>=0x100000000ULL){return (int32_t)Rng_get_int();}
    span=(uint32_t)span64;
    threshold=(uint32_t)(0U-span)%span;
    do{value=Rng_get_int();}while(value<threshold);
    return (int)((int64_t)min+(int64_t)(value%span));
}

float Rng_get_range_float(float min,float max){
    float temp;
    float unit;
    if(min>max){temp=min;min=max;max=temp;}
    if(min==max){return min;}
    unit=(float)(Rng_get_int()>>8)*(1.0F/16777216.0F);
    return min+(max-min)*unit;
}
