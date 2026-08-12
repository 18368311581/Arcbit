#include "power.h"

#define POWER_ADC_SAMPLES 16U

static ADC_HandleTypeDef power_adc;
static volatile uint8_t power_shutdown_request;

/* 执行一次软件触发 ADC 转换，所有等待均带超时。 */
static uint8_t power_adc_once(uint16_t* raw){
    if(HAL_ADC_Start(&power_adc)!=HAL_OK){return 1U;}
    if(HAL_ADC_PollForConversion(&power_adc,20U)!=HAL_OK){
        HAL_ADC_Stop(&power_adc);
        return 1U;
    }
    *raw=(uint16_t)HAL_ADC_GetValue(&power_adc);
    return HAL_ADC_Stop(&power_adc)==HAL_OK?0U:1U;
}

uint8_t Power_init(void){
    GPIO_InitTypeDef gpio={0};
    ADC_ChannelConfTypeDef channel={0};
    HAL_GPIO_WritePin(BAT_SENSE_EN_GPIO_Port,BAT_SENSE_EN_Pin,GPIO_PIN_RESET);
    HAL_GPIO_WritePin(POWER_KILL_GPIO_Port,POWER_KILL_Pin,GPIO_PIN_SET);
    gpio.Pin=BAT_SENSE_EN_Pin;
    gpio.Mode=GPIO_MODE_OUTPUT_PP;
    gpio.Pull=GPIO_NOPULL;
    gpio.Speed=GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BAT_SENSE_EN_GPIO_Port,&gpio);
    gpio.Pin=POWER_KILL_Pin;
    HAL_GPIO_Init(POWER_KILL_GPIO_Port,&gpio);
    gpio.Pin=POWER_INT_Pin;
    gpio.Mode=GPIO_MODE_IT_FALLING;
    gpio.Pull=GPIO_PULLUP;
    HAL_GPIO_Init(POWER_INT_GPIO_Port,&gpio);
    power_shutdown_request=HAL_GPIO_ReadPin(POWER_INT_GPIO_Port,POWER_INT_Pin)==GPIO_PIN_RESET?1U:0U;
    HAL_NVIC_SetPriority(EXTI15_10_IRQn,7U,0U);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    gpio.Pin=BAT_ADC_Pin;
    gpio.Mode=GPIO_MODE_ANALOG;
    gpio.Pull=GPIO_NOPULL;
    HAL_GPIO_Init(BAT_ADC_GPIO_Port,&gpio);
    power_adc.Instance=ADC1;
    power_adc.Init.ClockPrescaler=ADC_CLOCK_SYNC_PCLK_DIV4;
    power_adc.Init.Resolution=ADC_RESOLUTION_12B;
    power_adc.Init.ScanConvMode=DISABLE;
    power_adc.Init.ContinuousConvMode=DISABLE;
    power_adc.Init.DiscontinuousConvMode=DISABLE;
    power_adc.Init.ExternalTrigConvEdge=ADC_EXTERNALTRIGCONVEDGE_NONE;
    power_adc.Init.ExternalTrigConv=ADC_SOFTWARE_START;
    power_adc.Init.DataAlign=ADC_DATAALIGN_RIGHT;
    power_adc.Init.NbrOfConversion=1;
    power_adc.Init.DMAContinuousRequests=DISABLE;
    power_adc.Init.EOCSelection=ADC_EOC_SINGLE_CONV;
    if(HAL_ADC_Init(&power_adc)!=HAL_OK){return 1U;}
    channel.Channel=ADC_CHANNEL_0;
    channel.Rank=1U;
    channel.SamplingTime=ADC_SAMPLETIME_480CYCLES;
    if(HAL_ADC_ConfigChannel(&power_adc,&channel)!=HAL_OK){return 1U;}
    return 0U;
}

uint8_t Power_shutdown_requested(void){
    if(HAL_GPIO_ReadPin(POWER_INT_GPIO_Port,POWER_INT_Pin)==GPIO_PIN_RESET){
        power_shutdown_request=1U;
    }
    return power_shutdown_request;
}

uint8_t Power_read_battery_raw(uint16_t* raw){
    uint32_t total=0U;
    uint16_t sample;
    uint8_t i;
    if(raw==NULL){return 1U;}
    HAL_GPIO_WritePin(BAT_SENSE_EN_GPIO_Port,BAT_SENSE_EN_Pin,GPIO_PIN_SET);
    /* 高阻分压与采样电容需要建立时间，首个样本只用于稳定 ADC。 */
    HAL_Delay(50U);
    if(power_adc_once(&sample)!=0U){
        HAL_GPIO_WritePin(BAT_SENSE_EN_GPIO_Port,BAT_SENSE_EN_Pin,GPIO_PIN_RESET);
        return 1U;
    }
    for(i=0U;i<POWER_ADC_SAMPLES;i++){
        if(power_adc_once(&sample)!=0U){
            HAL_GPIO_WritePin(BAT_SENSE_EN_GPIO_Port,BAT_SENSE_EN_Pin,GPIO_PIN_RESET);
            return 1U;
        }
        total+=sample;
    }
    HAL_GPIO_WritePin(BAT_SENSE_EN_GPIO_Port,BAT_SENSE_EN_Pin,GPIO_PIN_RESET);
    *raw=(uint16_t)((total+(POWER_ADC_SAMPLES/2U))/POWER_ADC_SAMPLES);
    return 0U;
}

uint8_t Power_read_battery_mv(uint32_t* millivolts){
    uint16_t raw;
    uint64_t value;
    if(millivolts==NULL||Power_read_battery_raw(&raw)!=0U){return 1U;}
    value=(uint64_t)raw*3300U*1150U;
    value=(value+(4095U*470U/2U))/(4095U*470U);
    value=(value*6380U+2935U)/5870U;
    *millivolts=(uint32_t)value+80U;
    return 0U;
}

void Power_shutdown(void){
    /* KILL# 拉低后硬件应切断主电源；若未断电则停在此处，禁止继续执行。 */
    HAL_GPIO_WritePin(POWER_KILL_GPIO_Port,POWER_KILL_Pin,GPIO_PIN_RESET);
    __disable_irq();
    while(1){}
}

void EXTI15_10_IRQHandler(void){
    HAL_GPIO_EXTI_IRQHandler(POWER_INT_Pin);
}

void HAL_GPIO_EXTI_Callback(uint16_t pin){
    /* 中断中只锁存请求，存档和断电必须由主流程顺序完成。 */
    if(pin==POWER_INT_Pin){power_shutdown_request=1U;}
}

void Hold_power_early(void){
    GPIO_InitTypeDef gpio={0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_WritePin(POWER_KILL_GPIO_Port,POWER_KILL_Pin,GPIO_PIN_SET);
    gpio.Pin=POWER_KILL_Pin;
    gpio.Mode=GPIO_MODE_OUTPUT_PP;
    gpio.Pull=GPIO_NOPULL;
    gpio.Speed=GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(POWER_KILL_GPIO_Port,&gpio);
}
