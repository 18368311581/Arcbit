#include "flappy_bird_rules.hpp"
#include "rng.h"
#include <stddef.h>

namespace {

static const FlappyBirdDifficulty difficulties[FlappyBirdRules::DifficultyCount]={
    {58U,94U,154U,44U},
    {72U,82U,138U,40U},
    {88U,72U,124U,34U},
    {108U,64U,112U,28U}
};

static constexpr int32_t GravityQ8PerStep=18*FlappyBirdRules::FixedOne;
static constexpr int32_t FlapVelocityQ8=-240*FlappyBirdRules::FixedOne;
static constexpr int32_t MaxFallVelocityQ8=320*FlappyBirdRules::FixedOne;

static int32_t abs_i32(int32_t value){return value<0?-value:value;}

static int32_t pipe_step_q8(uint8_t difficulty){
    return ((int32_t)difficulties[difficulty].speed_px_s*FlappyBirdRules::FixedOne*(int32_t)FlappyBirdRules::StepMs+500)/1000;
}

static bool hitbox_inside_gap(const FlappyBirdState& state,const FlappyBirdPipeState& pipe){
    int32_t bird_left=(int32_t)(FlappyBirdRules::BirdX+FlappyBirdRules::HitboxInset)*FlappyBirdRules::FixedOne;
    int32_t bird_right=(int32_t)(FlappyBirdRules::BirdX+FlappyBirdRules::BirdWidth-FlappyBirdRules::HitboxInset)*FlappyBirdRules::FixedOne;
    int32_t pipe_left=pipe.x_q8;
    int32_t pipe_right=pipe.x_q8+(int32_t)FlappyBirdRules::PipeWidth*FlappyBirdRules::FixedOne;
    if(bird_right<=pipe_left||bird_left>=pipe_right){return true;}
    int32_t bird_top=state.bird_y_q8+(int32_t)FlappyBirdRules::HitboxInset*FlappyBirdRules::FixedOne;
    int32_t bird_bottom=state.bird_y_q8+(int32_t)(FlappyBirdRules::BirdHeight-FlappyBirdRules::HitboxInset)*FlappyBirdRules::FixedOne;
    int32_t gap_top=(int32_t)pipe.gap_top*FlappyBirdRules::FixedOne;
    int32_t gap_bottom=(int32_t)(pipe.gap_top+difficulties[state.difficulty].gap_height)*FlappyBirdRules::FixedOne;
    return bird_top>=gap_top&&bird_bottom<=gap_bottom;
}

}

const FlappyBirdDifficulty& FlappyBirdRules::difficulty(uint8_t index){return difficulties[index<DifficultyCount?index:1U];}

uint16_t FlappyBirdRules::next_gap_top(uint8_t difficulty_index,uint16_t previous_gap_top){
    const FlappyBirdDifficulty& config=difficulty(difficulty_index);
    int32_t previous_center=(int32_t)previous_gap_top+(int32_t)config.gap_height/2;
    int32_t offset=Rng_get_range_int(-(int)config.max_center_change,(int)config.max_center_change);
    int32_t center=previous_center+offset;
    int32_t minimum=(int32_t)EdgeMargin+(int32_t)config.gap_height/2;
    int32_t maximum=(int32_t)PlayHeight-(int32_t)EdgeMargin-(int32_t)config.gap_height/2;
    if(center<minimum){center=minimum;}else if(center>maximum){center=maximum;}
    return (uint16_t)(center-(int32_t)config.gap_height/2);
}

void FlappyBirdRules::start(FlappyBirdState* state,uint8_t difficulty_index){
    if(state==NULL){return;}
    uint8_t selected=difficulty_index<DifficultyCount?difficulty_index:1U;
    const FlappyBirdDifficulty& config=difficulty(selected);
    state->score=0U;
    state->bird_y_q8=(int32_t)(PlayHeight-BirdHeight)/2*FixedOne;state->bird_velocity_q8=0;
    state->accumulator_ms=0U;state->difficulty=selected;state->phase=FlappyBirdPhase::Ready;
    uint16_t gap_top=(uint16_t)((PlayHeight-config.gap_height)/2U);
    for(uint8_t i=0U;i<PipeCount;i++){
        if(i!=0U){gap_top=next_gap_top(selected,gap_top);}
        state->pipes[i].x_q8=(int32_t)(260U+(uint16_t)i*config.spacing)*FixedOne;
        state->pipes[i].gap_top=gap_top;state->pipes[i].scored=0U;state->pipes[i].reserved=0U;
    }
}

void FlappyBirdRules::flap(FlappyBirdState* state){
    if(state==NULL||state->phase==FlappyBirdPhase::GameOver){return;}
    state->phase=FlappyBirdPhase::Running;state->bird_velocity_q8=FlapVelocityQ8;
}

bool FlappyBirdRules::collides(const FlappyBirdState& state){
    int32_t bird_top=state.bird_y_q8+(int32_t)HitboxInset*FixedOne;
    int32_t bird_bottom=state.bird_y_q8+(int32_t)(BirdHeight-HitboxInset)*FixedOne;
    if(bird_top<0||bird_bottom>(int32_t)PlayHeight*FixedOne){return true;}
    for(uint8_t i=0U;i<PipeCount;i++){if(!hitbox_inside_gap(state,state.pipes[i])){return true;}}
    return false;
}

bool FlappyBirdRules::step(FlappyBirdState* state,bool* scored){
    if(scored!=NULL){*scored=false;}
    if(state==NULL||state->phase!=FlappyBirdPhase::Running||state->difficulty>=DifficultyCount){return false;}
    const FlappyBirdDifficulty& config=difficulty(state->difficulty);
    state->bird_velocity_q8+=GravityQ8PerStep;
    if(state->bird_velocity_q8>MaxFallVelocityQ8){state->bird_velocity_q8=MaxFallVelocityQ8;}
    state->bird_y_q8+=(state->bird_velocity_q8*(int32_t)StepMs)/1000;
    int32_t movement=pipe_step_q8(state->difficulty);
    for(uint8_t i=0U;i<PipeCount;i++){state->pipes[i].x_q8-=movement;}
    if(collides(*state)){state->phase=FlappyBirdPhase::GameOver;return false;}
    int32_t bird_left=(int32_t)(BirdX+HitboxInset)*FixedOne;
    for(uint8_t i=0U;i<PipeCount;i++){
        FlappyBirdPipeState& pipe=state->pipes[i];
        if(pipe.scored==0U&&pipe.x_q8+(int32_t)PipeWidth*FixedOne<bird_left){pipe.scored=1U;state->score++;if(scored!=NULL){*scored=true;}}
    }
    for(uint8_t i=0U;i<PipeCount;i++){
        FlappyBirdPipeState& pipe=state->pipes[i];
        if(pipe.x_q8+(int32_t)PipeWidth*FixedOne>=0){continue;}
        int32_t farthest=state->pipes[0].x_q8;uint16_t previous_gap=state->pipes[0].gap_top;
        for(uint8_t j=1U;j<PipeCount;j++){if(state->pipes[j].x_q8>farthest){farthest=state->pipes[j].x_q8;previous_gap=state->pipes[j].gap_top;}}
        pipe.x_q8=farthest+(int32_t)config.spacing*FixedOne;
        pipe.gap_top=next_gap_top(state->difficulty,previous_gap);pipe.scored=0U;pipe.reserved=0U;
    }
    return true;
}

bool FlappyBirdRules::state_valid(const FlappyBirdState& state){
    if(state.difficulty>=DifficultyCount||state.phase==FlappyBirdPhase::GameOver||state.accumulator_ms>=StepMs){return false;}
    const FlappyBirdDifficulty& config=difficulty(state.difficulty);
    if(state.bird_velocity_q8<FlapVelocityQ8||state.bird_velocity_q8>MaxFallVelocityQ8){return false;}
    for(uint8_t i=0U;i<PipeCount;i++){
        const FlappyBirdPipeState& pipe=state.pipes[i];
        if(pipe.x_q8<-(int32_t)(PipeWidth+1U)*FixedOne||pipe.x_q8>(int32_t)700*FixedOne||pipe.gap_top<EdgeMargin||
           pipe.gap_top+config.gap_height>PlayHeight-EdgeMargin||pipe.scored>1U||pipe.reserved!=0U){return false;}
        for(uint8_t j=0U;j<i;j++){if(abs_i32(pipe.x_q8-state.pipes[j].x_q8)<(int32_t)config.spacing*FixedOne){return false;}}
    }
    if(state.phase==FlappyBirdPhase::Ready){
        if(state.score!=0U||state.bird_y_q8!=(int32_t)(PlayHeight-BirdHeight)/2*FixedOne||state.bird_velocity_q8!=0){return false;}
    }
    else if(collides(state)){return false;}
    return true;
}

bool FlappyBirdRules::make_snapshot(const FlappyBirdState& state,FlappyBirdSnapshot* snapshot){
    if(snapshot==NULL||!state_valid(state)){return false;}
    snapshot->magic=SnapshotMagic;snapshot->version=SnapshotVersion;snapshot->difficulty=state.difficulty;snapshot->phase=(uint8_t)state.phase;
    snapshot->score=state.score;snapshot->reserved_random=0U;snapshot->accumulator_ms=state.accumulator_ms;snapshot->reserved=0U;
    snapshot->bird_y_q8=state.bird_y_q8;snapshot->bird_velocity_q8=state.bird_velocity_q8;
    for(uint8_t i=0U;i<PipeCount;i++){snapshot->pipes[i]=state.pipes[i];}
    return true;
}

bool FlappyBirdRules::restore_snapshot(const FlappyBirdSnapshot& snapshot,FlappyBirdState* state){
    if(state==NULL||snapshot.magic!=SnapshotMagic||snapshot.version!=SnapshotVersion||snapshot.reserved_random!=0U||snapshot.reserved!=0U||snapshot.phase>(uint8_t)FlappyBirdPhase::Running){return false;}
    FlappyBirdState restored;
    restored.score=snapshot.score;restored.accumulator_ms=snapshot.accumulator_ms;
    restored.bird_y_q8=snapshot.bird_y_q8;restored.bird_velocity_q8=snapshot.bird_velocity_q8;
    restored.difficulty=snapshot.difficulty;restored.phase=(FlappyBirdPhase)snapshot.phase;
    for(uint8_t i=0U;i<PipeCount;i++){restored.pipes[i]=snapshot.pipes[i];}
    if(!state_valid(restored)){return false;}
    *state=restored;return true;
}
