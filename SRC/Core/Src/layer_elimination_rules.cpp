#include "layer_elimination_rules.hpp"
#include "rng.h"

namespace {

static uint8_t generation_pool[LayerEliminationRules::MaxCells];
static LayerEliminationState restore_candidate;
static uint8_t validation_target[LayerEliminationRules::BitsetBytes];

static void clear_bits(uint8_t* bits){for(uint8_t i=0U;i<LayerEliminationRules::BitsetBytes;i++){bits[i]=0U;}}

static bool bits_equal(const uint8_t* left,const uint8_t* right){
    for(uint8_t i=0U;i<LayerEliminationRules::BitsetBytes;i++){if(left[i]!=right[i]){return false;}}
    return true;
}

static bool active_nonzero(const uint8_t* bits,uint8_t cell_count){
    for(uint8_t cell=0U;cell<cell_count;cell++){if(LayerEliminationRules::bit_get(bits,cell)){return true;}}
    return false;
}

static bool inactive_clear(const uint8_t* bits,uint8_t cell_count){
    for(uint8_t cell=cell_count;cell<LayerEliminationRules::MaxCells;cell++){if(LayerEliminationRules::bit_get(bits,cell)){return false;}}
    return true;
}

static uint8_t active_count(const uint8_t* bits,uint8_t cell_count){
    uint8_t count=0U;for(uint8_t cell=0U;cell<cell_count;cell++){if(LayerEliminationRules::bit_get(bits,cell)){count++;}}return count;
}

}

namespace LayerEliminationRules {

uint8_t grid_size(uint8_t variant){return variant<3U?8U:12U;}
uint8_t difficulty(uint8_t variant){return variant<VariantCount?(uint8_t)(variant%DifficultyCount):0U;}

uint8_t generation_count(uint8_t variant){
    static const uint8_t coefficients[DifficultyCount]={14U,22U,30U};
    uint8_t valid_variant=variant<VariantCount?variant:1U,size=grid_size(valid_variant),level=difficulty(valid_variant);
    return (uint8_t)(((uint16_t)size*size*coefficients[level])/100U);
}

uint8_t cell_index(uint8_t column,uint8_t row,uint8_t size){return (uint8_t)(row*size+column);}
uint8_t cell_column(uint8_t cell,uint8_t size){return size==0U?0U:(uint8_t)(cell%size);}
uint8_t cell_row(uint8_t cell,uint8_t size){return size==0U?0U:(uint8_t)(cell/size);}

bool bit_get(const uint8_t* bits,uint8_t cell){return bits!=0&&cell<MaxCells&&(bits[cell>>3]&(uint8_t)(1U<<(cell&7U)))!=0U;}

void bit_set(uint8_t* bits,uint8_t cell,bool value){
    if(bits==0||cell>=MaxCells){return;}uint8_t mask=(uint8_t)(1U<<(cell&7U));
    if(value){bits[cell>>3]|=mask;}else{bits[cell>>3]&=(uint8_t)~mask;}
}

void toggle_cross(uint8_t* bits,uint8_t size,uint8_t cell){
    if(bits==0||(size!=8U&&size!=12U)||cell>=(uint8_t)(size*size)){return;}
    uint8_t row=cell_row(cell,size),column=cell_column(cell,size);bit_set(bits,cell,!bit_get(bits,cell));
    if(row>0U){uint8_t next=cell_index(column,(uint8_t)(row-1U),size);bit_set(bits,next,!bit_get(bits,next));}
    if(row+1U<size){uint8_t next=cell_index(column,(uint8_t)(row+1U),size);bit_set(bits,next,!bit_get(bits,next));}
    if(column>0U){uint8_t next=cell_index((uint8_t)(column-1U),row,size);bit_set(bits,next,!bit_get(bits,next));}
    if(column+1U<size){uint8_t next=cell_index((uint8_t)(column+1U),row,size);bit_set(bits,next,!bit_get(bits,next));}
}

bool complete(const LayerEliminationState& state){return bits_equal(state.current,state.target);}
bool all_white(const LayerEliminationState& state){return !active_nonzero(state.current,(uint8_t)(grid_size(state.variant)*grid_size(state.variant)));}

bool state_valid(const LayerEliminationState& state){
    if(state.variant>=VariantCount||state.phase>LayerEliminationPhase::AnswerReview||state.reset_focus>1U){return false;}
    uint8_t size=grid_size(state.variant),cell_count=(uint8_t)(size*size);
    if(state.selected_cell>=cell_count||!inactive_clear(state.current,cell_count)||!inactive_clear(state.target,cell_count)||!inactive_clear(state.answer,cell_count)||complete(state)){return false;}
    if(active_count(state.answer,cell_count)!=generation_count(state.variant)){return false;}clear_bits(validation_target);
    for(uint8_t cell=0U;cell<cell_count;cell++){if(bit_get(state.answer,cell)){toggle_cross(validation_target,size,cell);}}
    if(!bits_equal(validation_target,state.target)){return false;}
    if(state.phase==LayerEliminationPhase::ResetConfirm&&all_white(state)){return false;}
    if((state.phase==LayerEliminationPhase::Playing||state.phase==LayerEliminationPhase::AnswerReview)&&state.reset_focus!=0U){return false;}
    return true;
}

bool update_fastest_time(uint32_t* fastest_seconds,uint8_t variant,uint32_t seconds,bool victory){
    if(fastest_seconds==0||variant>=VariantCount||seconds==0U||!victory){return false;}
    if(fastest_seconds[variant]!=0U&&seconds>=fastest_seconds[variant]){return false;}
    fastest_seconds[variant]=seconds;return true;
}

void add_elapsed_ms(LayerEliminationState* state,uint32_t elapsed_ms){
    if(state==0){return;}state->elapsed_ms=UINT32_MAX-state->elapsed_ms<elapsed_ms?UINT32_MAX:state->elapsed_ms+elapsed_ms;
}

void reset_current(LayerEliminationState* state){
    if(state==0||state->variant>=VariantCount){return;}clear_bits(state->current);uint8_t size=grid_size(state->variant);
    state->selected_cell=cell_index((uint8_t)(size/2U-1U),(uint8_t)(size/2U-1U),size);state->phase=LayerEliminationPhase::Playing;state->reset_focus=0U;
}

void start(LayerEliminationState* state,uint8_t variant){
    if(state==0){return;}state->variant=variant<VariantCount?variant:1U;state->elapsed_ms=0U;state->phase=LayerEliminationPhase::Playing;state->reset_focus=0U;
    clear_bits(state->current);clear_bits(state->target);clear_bits(state->answer);uint8_t size=grid_size(state->variant),cell_count=(uint8_t)(size*size),count=generation_count(state->variant);
    for(uint8_t i=0U;i<cell_count;i++){generation_pool[i]=i;}
    for(uint8_t i=0U;i<count;i++){
        uint8_t selected=(uint8_t)Rng_get_range_int(i,(int)cell_count-1);uint8_t temporary=generation_pool[i];generation_pool[i]=generation_pool[selected];generation_pool[selected]=temporary;
        bit_set(state->answer,generation_pool[i],true);toggle_cross(state->target,size,generation_pool[i]);
    }
    state->selected_cell=cell_index((uint8_t)(size/2U-1U),(uint8_t)(size/2U-1U),size);
}

bool make_snapshot(const LayerEliminationState& state,LayerEliminationSnapshot* snapshot){
    if(snapshot==0||!state_valid(state)){return false;}snapshot->magic=SnapshotMagic;snapshot->version=SnapshotVersion;snapshot->payload_size=sizeof(LayerEliminationSnapshot);snapshot->elapsed_ms=state.elapsed_ms;
    for(uint8_t i=0U;i<BitsetBytes;i++){snapshot->current[i]=state.current[i];snapshot->target[i]=state.target[i];snapshot->answer[i]=state.answer[i];}
    snapshot->variant=state.variant;snapshot->selected_cell=state.selected_cell;snapshot->phase=(uint8_t)state.phase;snapshot->reset_focus=state.reset_focus;
    for(uint8_t i=0U;i<sizeof(snapshot->reserved);i++){snapshot->reserved[i]=0U;}return true;
}

bool restore_snapshot(const LayerEliminationSnapshot& snapshot,LayerEliminationState* state){
    if(state==0||snapshot.magic!=SnapshotMagic||snapshot.version!=SnapshotVersion||snapshot.payload_size!=sizeof(LayerEliminationSnapshot)){return false;}
    for(uint8_t i=0U;i<sizeof(snapshot.reserved);i++){if(snapshot.reserved[i]!=0U){return false;}}
    restore_candidate.elapsed_ms=snapshot.elapsed_ms;restore_candidate.variant=snapshot.variant;restore_candidate.selected_cell=snapshot.selected_cell;restore_candidate.phase=(LayerEliminationPhase)snapshot.phase;restore_candidate.reset_focus=snapshot.reset_focus;
    for(uint8_t i=0U;i<BitsetBytes;i++){restore_candidate.current[i]=snapshot.current[i];restore_candidate.target[i]=snapshot.target[i];restore_candidate.answer[i]=snapshot.answer[i];}
    if(!state_valid(restore_candidate)){return false;}*state=restore_candidate;return true;
}

}
