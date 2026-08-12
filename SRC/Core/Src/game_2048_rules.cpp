#include "game_2048_rules.hpp"
#include "rng.h"
#include <limits.h>
#include <stddef.h>

namespace {

static uint32_t saturating_add(uint32_t left,uint32_t right){return UINT32_MAX-left<right?UINT32_MAX:left+right;}
static uint32_t saturating_multiply(uint32_t value,uint8_t factor){return factor!=0U&&value>UINT32_MAX/factor?UINT32_MAX:value*factor;}

static void compact_line(uint8_t* values,uint8_t* depths,uint8_t size){
    uint8_t compacted[Game2048Rules::MaxSize]={0U},compacted_depths[Game2048Rules::MaxSize]={0U},write=size;
    for(uint8_t offset=0U;offset<size;offset++){
        uint8_t source=(uint8_t)(size-1U-offset);
        if(values[source]!=0U){write--;compacted[write]=values[source];compacted_depths[write]=depths[source];}
    }
    for(uint8_t i=0U;i<size;i++){values[i]=compacted[i];depths[i]=compacted_depths[i];}
}

static uint8_t line_cell(uint8_t size,uint8_t line,uint8_t position,Game2048Direction direction){
    switch(direction){
    case Game2048Direction::Right:return Game2048Rules::cell_index(position,line);
    case Game2048Direction::Left:return Game2048Rules::cell_index((uint8_t)(size-1U-position),line);
    case Game2048Direction::Down:return Game2048Rules::cell_index(line,position);
    default:return Game2048Rules::cell_index(line,(uint8_t)(size-1U-position));
    }
}

}

namespace Game2048Rules {

uint8_t board_size(uint8_t size_index){return size_index<VariantCount?(uint8_t)(size_index+4U):4U;}
uint8_t cell_index(uint8_t x,uint8_t y){return (uint8_t)(y*MaxSize+x);}

uint32_t tile_value(uint8_t exponent){
    if(exponent==0U){return 0U;}
    return exponent<32U?(uint32_t)(1UL<<exponent):UINT32_MAX;
}

uint8_t sound_tier(uint32_t score_gain){
    if(score_gain<16U){return 0U;}
    if(score_gain<64U){return 1U;}
    if(score_gain<256U){return 2U;}
    return 3U;
}

bool update_high_score(uint32_t* high_scores,uint8_t size_index,uint32_t score){
    if(high_scores==0||size_index>=VariantCount||score<=high_scores[size_index]){return false;}
    high_scores[size_index]=score;return true;
}

bool can_move(const Game2048State& state){
    uint8_t size=board_size(state.size_index);
    for(uint8_t y=0U;y<size;y++){
        for(uint8_t x=0U;x<size;x++){
            uint8_t value=state.cells[cell_index(x,y)];
            if(value==0U){return true;}
            if(value<MaxExponent&&x+1U<size&&state.cells[cell_index((uint8_t)(x+1U),y)]==value){return true;}
            if(value<MaxExponent&&y+1U<size&&state.cells[cell_index(x,(uint8_t)(y+1U))]==value){return true;}
        }
    }
    return false;
}

bool state_valid(const Game2048State& state){
    if(state.size_index>=VariantCount||state.running!=1U){return false;}
    uint8_t size=board_size(state.size_index),occupied=0U;
    for(uint8_t y=0U;y<MaxSize;y++){
        for(uint8_t x=0U;x<MaxSize;x++){
            uint8_t value=state.cells[cell_index(x,y)];
            if(x>=size||y>=size){if(value!=0U){return false;}}
            else if(value>MaxExponent){return false;}
            else if(value!=0U){occupied++;}
        }
    }
    return occupied>=2U&&can_move(state);
}

bool spawn(Game2048State* state){
    if(state==0||state->size_index>=VariantCount){return false;}
    uint8_t size=board_size(state->size_index),empty_count=0U;
    for(uint8_t y=0U;y<size;y++){for(uint8_t x=0U;x<size;x++){if(state->cells[cell_index(x,y)]==0U){empty_count++;}}}
    if(empty_count==0U){return false;}
    uint8_t selected=(uint8_t)Rng_get_range_int(0,(int)empty_count-1);
    uint8_t exponent=Rng_get_range_int(0,4)<4?1U:2U;
    for(uint8_t y=0U;y<size;y++){
        for(uint8_t x=0U;x<size;x++){
            uint8_t index=cell_index(x,y);if(state->cells[index]!=0U){continue;}
            if(selected==0U){state->cells[index]=exponent;return true;}selected--;
        }
    }
    return false;
}

void start(Game2048State* state,uint8_t size_index){
    if(state==0){return;}
    for(uint8_t i=0U;i<MaxCells;i++){state->cells[i]=0U;}
    state->score=0U;state->size_index=size_index<VariantCount?size_index:0U;state->running=1U;
    (void)spawn(state);(void)spawn(state);
}

bool slide(Game2048State* state,Game2048Direction direction,Game2048MoveInfo* info){
    Game2048MoveInfo local={0U,0U,0U};if(info!=0){*info=local;}
    if(state==0||state->running==0U||state->size_index>=VariantCount){return false;}
    uint8_t original[MaxCells];for(uint8_t i=0U;i<MaxCells;i++){original[i]=state->cells[i];}
    uint8_t size=board_size(state->size_index);
    for(uint8_t line=0U;line<size;line++){
        uint8_t values[MaxSize]={0U},depths[MaxSize]={0U};
        for(uint8_t position=0U;position<size;position++){values[position]=state->cells[line_cell(size,line,position,direction)];}
        compact_line(values,depths,size);
        for(uint8_t position=0U;position+1U<size;position++){
            if(values[position]==0U||values[position]!=values[position+1U]||values[position]>=MaxExponent){continue;}
            uint8_t result_exponent=(uint8_t)(values[position]+1U);
            uint8_t result_depth=(uint8_t)((depths[position]>depths[position+1U]?depths[position]:depths[position+1U])+1U);
            values[position]=0U;depths[position]=0U;values[position+1U]=result_exponent;depths[position+1U]=result_depth;
            local.score_gain=saturating_add(local.score_gain,saturating_multiply(tile_value(result_exponent),result_depth));local.merge_count++;
            if(result_depth>local.highest_depth){local.highest_depth=result_depth;}
        }
        compact_line(values,depths,size);
        for(uint8_t position=0U;position<size;position++){state->cells[line_cell(size,line,position,direction)]=values[position];}
    }
    bool changed=false;for(uint8_t i=0U;i<MaxCells;i++){if(original[i]!=state->cells[i]){changed=true;break;}}
    if(!changed){return false;}
    state->score=saturating_add(state->score,local.score_gain);if(info!=0){*info=local;}return true;
}

bool move(Game2048State* state,Game2048Direction direction,Game2048MoveInfo* info){
    Game2048MoveInfo local;
    if(!slide(state,direction,&local)){if(info!=0){Game2048MoveInfo empty={0U,0U,0U};*info=empty;}return false;}
    (void)spawn(state);state->running=can_move(*state)?1U:0U;if(info!=0){*info=local;}return true;
}

bool make_snapshot(const Game2048State& state,Game2048Snapshot* snapshot){
    if(snapshot==0||!state_valid(state)){return false;}
    snapshot->magic=SnapshotMagic;snapshot->version=SnapshotVersion;snapshot->size_index=state.size_index;snapshot->reserved=0U;
    snapshot->score=state.score;snapshot->reserved_random=0U;
    for(uint8_t i=0U;i<MaxCells;i++){snapshot->cells[i]=state.cells[i];}
    return true;
}

bool restore_snapshot(const Game2048Snapshot& snapshot,Game2048State* state){
    if(state==0||snapshot.magic!=SnapshotMagic||snapshot.version!=SnapshotVersion||snapshot.reserved!=0U||snapshot.reserved_random!=0U){return false;}
    Game2048State candidate;for(uint8_t i=0U;i<MaxCells;i++){candidate.cells[i]=snapshot.cells[i];}
    candidate.score=snapshot.score;candidate.size_index=snapshot.size_index;candidate.running=1U;
    if(!state_valid(candidate)){return false;}*state=candidate;return true;
}

}
