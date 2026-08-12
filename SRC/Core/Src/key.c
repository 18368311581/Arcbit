#include "key.h"

static uint16_t key_stable_state;
static uint16_t key_last_sample;
static uint16_t key_pressed;
static uint16_t key_released;
static uint32_t key_last_scan;
static uint8_t key_same_count;

/*
 * 方向键直接读取低电平；矩阵每次只拉低一行，其余行保持高电平，
 * 扫描结束后恢复全部行，避免长期形成无效电流通路。
 */
static uint16_t key_read_raw(void){
    static const uint16_t row_pins[3]={KEY_ROW0_Pin,KEY_ROW1_Pin,KEY_ROW2_Pin};
    static const uint16_t col_pins[3]={KEY_COL0_Pin,KEY_COL1_Pin,KEY_COL2_Pin};
    uint16_t state=0U;
    uint8_t row;
    uint8_t col;
    if(HAL_GPIO_ReadPin(KEY_UP_GPIO_Port,KEY_UP_Pin)==GPIO_PIN_RESET){state|=KEY_UP;}
    if(HAL_GPIO_ReadPin(KEY_DOWN_GPIO_Port,KEY_DOWN_Pin)==GPIO_PIN_RESET){state|=KEY_DOWN;}
    if(HAL_GPIO_ReadPin(KEY_LEFT_GPIO_Port,KEY_LEFT_Pin)==GPIO_PIN_RESET){state|=KEY_LEFT;}
    if(HAL_GPIO_ReadPin(KEY_RIGHT_GPIO_Port,KEY_RIGHT_Pin)==GPIO_PIN_RESET){state|=KEY_RIGHT;}
    for(row=0U;row<3U;row++){
        HAL_GPIO_WritePin(GPIOC,KEY_ROW0_Pin|KEY_ROW1_Pin|KEY_ROW2_Pin,GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOC,row_pins[row],GPIO_PIN_RESET);
        for(volatile uint32_t wait=0U;wait<20U;wait++){}
        for(col=0U;col<3U;col++){
            if(HAL_GPIO_ReadPin(GPIOC,col_pins[col])==GPIO_PIN_RESET){
                state|=KEY_MATRIX(row,col);
            }
        }
    }
    HAL_GPIO_WritePin(GPIOC,KEY_ROW0_Pin|KEY_ROW1_Pin|KEY_ROW2_Pin,GPIO_PIN_SET);
    return state;
}

void Key_init(void){
    GPIO_InitTypeDef gpio={0};
    HAL_GPIO_WritePin(GPIOC,KEY_ROW0_Pin|KEY_ROW1_Pin|KEY_ROW2_Pin,GPIO_PIN_SET);
    gpio.Pin=KEY_ROW0_Pin|KEY_ROW1_Pin|KEY_ROW2_Pin;
    gpio.Mode=GPIO_MODE_OUTPUT_PP;
    gpio.Pull=GPIO_NOPULL;
    gpio.Speed=GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC,&gpio);
    gpio.Pin=KEY_COL0_Pin|KEY_COL1_Pin|KEY_COL2_Pin|KEY_UP_Pin|KEY_DOWN_Pin|KEY_LEFT_Pin|KEY_RIGHT_Pin;
    gpio.Mode=GPIO_MODE_INPUT;
    gpio.Pull=GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC,&gpio);
    key_stable_state=0U;
    key_last_sample=key_read_raw();
    key_pressed=0U;
    key_released=0U;
    key_last_scan=HAL_GetTick();
    key_same_count=1U;
}

void Key_scan(void){
    uint16_t sample;
    uint16_t changed;
    uint32_t now=HAL_GetTick();
    if((uint32_t)(now-key_last_scan)<5U){return;}
    key_last_scan=now;
    sample=key_read_raw();
    /* 整个 13 位状态连续 2 次一致后更新，响应约为 5～10 ms。 */
    if(sample==key_last_sample){
        if(key_same_count<2U){key_same_count++;}
    }
    else{
        key_last_sample=sample;
        key_same_count=1U;
    }
    if(key_same_count<2U||sample==key_stable_state){return;}
    changed=(uint16_t)(sample^key_stable_state);
    /* 边沿事件累积保存，直到对应读取函数取走并清零。 */
    key_pressed|=(uint16_t)(changed&sample);
    key_released|=(uint16_t)(changed&key_stable_state);
    key_stable_state=sample;
}

uint16_t Key_get_state(void){
    return key_stable_state;
}

uint16_t Key_get_pressed(void){
    uint16_t state=key_pressed;
    key_pressed=0U;
    return state;
}

uint16_t Key_get_released(void){
    uint16_t state=key_released;
    key_released=0U;
    return state;
}
