#ifndef __GOBANG_AI_HPP__
#define __GOBANG_AI_HPP__

#include "basic.h"

struct GobangAiMove {
    uint8_t row;
    uint8_t column;
};

bool GobangAi_choose(uint8_t* board,GobangAiMove* move);
bool GobangAi_has_five(const uint8_t* board,uint8_t row,uint8_t column,uint8_t stone);

#endif
