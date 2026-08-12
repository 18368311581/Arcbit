#ifndef __INPUT_HPP__
#define __INPUT_HPP__

#include "key.h"
#include "input_repeat.hpp"

enum class InputAction : uint8_t {
    Up=0U,Down,Left,Right,Confirm,Cancel,
    Action1,Action2,Action3,Action4,
    Home,Menu,Pause
};

struct InputEvent {
    uint16_t state;
    uint16_t pressed;
    uint16_t released;
};

static inline uint16_t Input_key(InputAction action){
    static const uint16_t keys[]={
        KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,
        KEY_Y,KEY_X,KEY_L,KEY_R,KEY_A,KEY_B,
        KEY_HOME,KEY_MENU,KEY_PAUSE_EXIT
    };
    return keys[(uint8_t)action];
}

static inline bool Input_is_pressed(const InputEvent& input,InputAction action){return (input.pressed&Input_key(action))!=0U;}
static inline bool Input_is_released(const InputEvent& input,InputAction action){return (input.released&Input_key(action))!=0U;}
static inline bool Input_is_held(const InputEvent& input,InputAction action){return (input.state&Input_key(action))!=0U;}

/* 仅由 APP 主循环调用，每轮读取并清除一次按键边沿。 */
static inline InputEvent Input_capture(void){
    InputEvent input={Key_get_state(),Key_get_pressed(),Key_get_released()};
    return input;
}

#endif
