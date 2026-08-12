#ifndef __INPUT_REPEAT_HPP__
#define __INPUT_REPEAT_HPP__

#include <stdint.h>

struct InputRepeatState {
    uint32_t next_ms;
    uint16_t active_key;
};

static inline void Input_repeat_reset(InputRepeatState* repeat){
    if(repeat!=0){repeat->next_ms=0U;repeat->active_key=0U;}
}

static inline uint16_t Input_repeat_update(InputRepeatState* repeat,uint16_t state,uint16_t pressed,uint16_t allowed_keys,uint32_t now_ms,uint16_t initial_delay_ms,uint16_t interval_ms){
    if(repeat==0||allowed_keys==0U){return 0U;}
    uint16_t held=(uint16_t)(state&allowed_keys);
    uint16_t newly_pressed=(uint16_t)(pressed&allowed_keys);
    if(held==0U||(held&(uint16_t)(held-1U))!=0U){Input_repeat_reset(repeat);return 0U;}
    if(newly_pressed!=0U){
        if((newly_pressed&(uint16_t)(newly_pressed-1U))!=0U){Input_repeat_reset(repeat);return 0U;}
        repeat->active_key=held;repeat->next_ms=now_ms+initial_delay_ms;return held;
    }
    if(repeat->active_key!=held){Input_repeat_reset(repeat);return 0U;}
    if((int32_t)(now_ms-repeat->next_ms)<0){return 0U;}
    repeat->next_ms=now_ms+interval_ms;return held;
}

#endif
