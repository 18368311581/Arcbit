#include "audio.h"

#define AUDIO_TIMER_BASE 1000000U
#define AUDIO_DURATION_COUNTER 2000U

static const uint8_t audio_duty_percent[AUDIO_VOLUME_LEVEL_COUNT]={0U,4U,8U,17U,50U};
static TIM_HandleTypeDef audio_pwm_timer;
static TIM_HandleTypeDef audio_duration_timer;
static Audio_note audio_notes[AUDIO_SEQUENCE_MAX];
static volatile uint8_t audio_note_count;
static volatile uint8_t audio_note_index;
static volatile uint8_t audio_busy;
static volatile uint8_t audio_volume=AUDIO_VOLUME_MAX;
static volatile uint32_t audio_duration_counts;

/* APB1 分频不为 1 时，STM32F4 定时器时钟自动变为 PCLK1 的 2 倍。 */
static uint32_t audio_timer_clock(void){
    RCC_ClkInitTypeDef clock_config;
    uint32_t flash_latency;
    uint32_t clock=HAL_RCC_GetPCLK1Freq();
    HAL_RCC_GetClockConfig(&clock_config,&flash_latency);
    if(clock_config.APB1CLKDivider!=RCC_HCLK_DIV1){clock*=2U;}
    return clock;
}

static void audio_output_stop(void){
    /* 停止时先关闭功放，再停止载波。 */
    HAL_GPIO_WritePin(AMP_SD_GPIO_Port,AMP_SD_Pin,GPIO_PIN_RESET);
    HAL_TIM_PWM_Stop(&audio_pwm_timer,TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(&audio_pwm_timer,TIM_CHANNEL_1,0U);
}

static uint8_t audio_set_tone(uint16_t frequency){
    uint32_t period;
    uint32_t pulse;
    uint8_t volume=audio_volume;
    if(frequency==0U||volume==0U){
        audio_output_stop();
        return 0U;
    }
    if(frequency<20U||frequency>20000U){return 1U;}
    period=AUDIO_TIMER_BASE/frequency;
    if(period<2U){return 1U;}
    HAL_GPIO_WritePin(AMP_SD_GPIO_Port,AMP_SD_Pin,GPIO_PIN_RESET);
    HAL_TIM_PWM_Stop(&audio_pwm_timer,TIM_CHANNEL_1);
    pulse=(period*(uint32_t)audio_duty_percent[volume]+50U)/100U;
    if(pulse==0U){pulse=1U;}
    if(pulse>=period){pulse=period-1U;}
    __HAL_TIM_SET_AUTORELOAD(&audio_pwm_timer,period-1U);
    __HAL_TIM_SET_COMPARE(&audio_pwm_timer,TIM_CHANNEL_1,pulse);
    __HAL_TIM_SET_COUNTER(&audio_pwm_timer,0U);
    HAL_TIM_GenerateEvent(&audio_pwm_timer,TIM_EVENTSOURCE_UPDATE);
    if(HAL_TIM_PWM_Start(&audio_pwm_timer,TIM_CHANNEL_1)!=HAL_OK){return 1U;}
    /* 先建立 PWM，再退出 PAM8302A 关断状态，减少启动噪声。 */
    HAL_GPIO_WritePin(AMP_SD_GPIO_Port,AMP_SD_Pin,GPIO_PIN_SET);
    return 0U;
}

static uint8_t audio_start_duration_chunk(void){
    uint32_t chunk=audio_duration_counts;
    if(chunk>65536U){chunk=65536U;}
    if(chunk==0U){return 1U;}
    audio_duration_counts-=chunk;
    HAL_TIM_Base_Stop_IT(&audio_duration_timer);
    __HAL_TIM_SET_AUTORELOAD(&audio_duration_timer,chunk-1U);
    __HAL_TIM_SET_COUNTER(&audio_duration_timer,0U);
    __HAL_TIM_CLEAR_FLAG(&audio_duration_timer,TIM_FLAG_UPDATE);
    return HAL_TIM_Base_Start_IT(&audio_duration_timer)==HAL_OK?0U:1U;
}

static uint8_t audio_start_duration(uint16_t duration_ms){
    if(duration_ms==0U){return 1U;}
    audio_duration_counts=(uint32_t)duration_ms*2U;
    return audio_start_duration_chunk();
}

void Audio_init(void){
    GPIO_InitTypeDef gpio={0};
    TIM_OC_InitTypeDef pwm={0};
    uint32_t timer_clock;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();
    HAL_GPIO_WritePin(AMP_SD_GPIO_Port,AMP_SD_Pin,GPIO_PIN_RESET);
    gpio.Pin=AMP_SD_Pin;
    gpio.Mode=GPIO_MODE_OUTPUT_PP;
    gpio.Pull=GPIO_NOPULL;
    gpio.Speed=GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(AMP_SD_GPIO_Port,&gpio);
    gpio.Pin=AUDIO_PWM_Pin;
    gpio.Mode=GPIO_MODE_AF_PP;
    gpio.Pull=GPIO_NOPULL;
    gpio.Speed=GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate=GPIO_AF2_TIM4;
    HAL_GPIO_Init(AUDIO_PWM_GPIO_Port,&gpio);
    timer_clock=audio_timer_clock();
    audio_pwm_timer.Instance=TIM4;
    audio_pwm_timer.Init.Prescaler=(timer_clock/AUDIO_TIMER_BASE)-1U;
    audio_pwm_timer.Init.CounterMode=TIM_COUNTERMODE_UP;
    audio_pwm_timer.Init.Period=999U;
    audio_pwm_timer.Init.ClockDivision=TIM_CLOCKDIVISION_DIV1;
    audio_pwm_timer.Init.AutoReloadPreload=TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&audio_pwm_timer);
    pwm.OCMode=TIM_OCMODE_PWM1;
    pwm.Pulse=0U;
    pwm.OCPolarity=TIM_OCPOLARITY_HIGH;
    pwm.OCFastMode=TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&audio_pwm_timer,&pwm,TIM_CHANNEL_1);
    audio_duration_timer.Instance=TIM3;
    /* 100 MHz 时 PSC=49999，得到 2 kHz 计数和 0.5 ms 硬件分辨率。 */
    audio_duration_timer.Init.Prescaler=(timer_clock/AUDIO_DURATION_COUNTER)-1U;
    audio_duration_timer.Init.CounterMode=TIM_COUNTERMODE_UP;
    audio_duration_timer.Init.Period=0xFFFFU;
    audio_duration_timer.Init.ClockDivision=TIM_CLOCKDIVISION_DIV1;
    audio_duration_timer.Init.AutoReloadPreload=TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&audio_duration_timer);
    HAL_NVIC_SetPriority(TIM3_IRQn,6U,0U);
    HAL_NVIC_ClearPendingIRQ(TIM3_IRQn);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    audio_note_count=0U;
    audio_note_index=0U;
    audio_busy=0U;
    audio_volume=AUDIO_VOLUME_MAX;
    audio_duration_counts=0U;
}

void Audio_set_volume(uint8_t volume){
    audio_volume=volume<AUDIO_VOLUME_LEVEL_COUNT?volume:AUDIO_VOLUME_MAX;
}

uint8_t Audio_get_volume(void){
    return audio_volume;
}

uint8_t Audio_play(uint16_t frequency){
    if(frequency<20U||frequency>20000U){return 1U;}
    HAL_NVIC_DisableIRQ(TIM3_IRQn);
    HAL_TIM_Base_Stop_IT(&audio_duration_timer);
    HAL_NVIC_ClearPendingIRQ(TIM3_IRQn);
    audio_note_count=0U;
    audio_note_index=0U;
    audio_duration_counts=0U;
    if(audio_set_tone(frequency)!=0U){
        audio_busy=0U;
        HAL_NVIC_EnableIRQ(TIM3_IRQn);
        return 1U;
    }
    audio_busy=1U;
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    return 0U;
}

uint8_t Audio_play_sequence(const Audio_note* notes,uint8_t count){
    uint8_t i;
    if(notes==NULL||count==0U||count>AUDIO_SEQUENCE_MAX){return 1U;}
    for(i=0U;i<count;i++){
        if(notes[i].duration_ms==0U){return 1U;}
        if(notes[i].frequency!=0U&&(notes[i].frequency<20U||notes[i].frequency>20000U)){return 1U;}
    }
    HAL_NVIC_DisableIRQ(TIM3_IRQn);
    HAL_TIM_Base_Stop_IT(&audio_duration_timer);
    HAL_NVIC_ClearPendingIRQ(TIM3_IRQn);
    audio_output_stop();
    for(i=0U;i<count;i++){audio_notes[i]=notes[i];}
    audio_note_count=count;
    audio_note_index=0U;
    audio_busy=1U;
    if(audio_set_tone(audio_notes[0].frequency)!=0U||
       audio_start_duration(audio_notes[0].duration_ms)!=0U){
        audio_note_count=0U;
        audio_busy=0U;
        audio_duration_counts=0U;
        audio_output_stop();
        HAL_NVIC_EnableIRQ(TIM3_IRQn);
        return 1U;
    }
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    return 0U;
}

uint8_t Audio_is_busy(void){
    return audio_busy;
}

void Audio_stop(void){
    HAL_NVIC_DisableIRQ(TIM3_IRQn);
    HAL_TIM_Base_Stop_IT(&audio_duration_timer);
    HAL_NVIC_ClearPendingIRQ(TIM3_IRQn);
    audio_note_count=0U;
    audio_note_index=0U;
    audio_busy=0U;
    audio_duration_counts=0U;
    audio_output_stop();
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
}

void TIM3_IRQHandler(void){
    HAL_TIM_IRQHandler(&audio_duration_timer);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* timer){
    if(timer->Instance!=TIM3){return;}
    HAL_TIM_Base_Stop_IT(&audio_duration_timer);
    if(audio_duration_counts!=0U){
        if(audio_start_duration_chunk()!=0U){
            audio_note_count=0U;
            audio_busy=0U;
            audio_duration_counts=0U;
            audio_output_stop();
        }
        return;
    }
    audio_note_index++;
    if(audio_note_index>=audio_note_count){
        audio_note_count=0U;
        audio_busy=0U;
        audio_duration_counts=0U;
        audio_output_stop();
        return;
    }
    if(audio_set_tone(audio_notes[audio_note_index].frequency)!=0U||
       audio_start_duration(audio_notes[audio_note_index].duration_ms)!=0U){
        audio_note_count=0U;
        audio_busy=0U;
        audio_duration_counts=0U;
        audio_output_stop();
    }
}
