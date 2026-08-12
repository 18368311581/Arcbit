#include "tetris_rules.hpp"
#include "rng.h"
#include <stddef.h>

namespace {

struct Kick {int8_t x;int8_t y;};

static const uint16_t shapes[TetrisRules::PieceCount][TetrisRules::RotationCount]={
    {0x00F0U,0x4444U,0x0F00U,0x2222U},
    {0x0066U,0x0066U,0x0066U,0x0066U},
    {0x0072U,0x0262U,0x0270U,0x0232U},
    {0x0036U,0x0462U,0x0360U,0x0231U},
    {0x0063U,0x0264U,0x0630U,0x0132U},
    {0x0071U,0x0226U,0x0470U,0x0322U},
    {0x0074U,0x0622U,0x0170U,0x0223U}
};

static const Kick jlstz_kicks[4][5]={
    {{0,0},{-1,0},{-1,-1},{0,2},{-1,2}},
    {{0,0},{1,0},{1,1},{0,-2},{1,-2}},
    {{0,0},{1,0},{1,-1},{0,2},{1,2}},
    {{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}}
};

static const Kick i_kicks[4][5]={
    {{0,0},{-2,0},{1,0},{-2,1},{1,-2}},
    {{0,0},{-1,0},{2,0},{-1,-2},{2,1}},
    {{0,0},{2,0},{-1,0},{2,-1},{-1,2}},
    {{0,0},{1,0},{-2,0},{1,2},{-2,-1}}
};

static const uint16_t gravity_periods[20]={
    800U,720U,630U,550U,470U,380U,300U,220U,150U,100U,
    90U,80U,70U,60U,55U,50U,45U,40U,35U,30U
};

static TetrisState restore_state_work;

static void clear_event(TetrisEvent* event){
    if(event!=NULL){event->moved=0U;event->locked=0U;event->lines_cleared=0U;event->game_over=0U;}
}

static uint32_t saturating_add(uint32_t value,uint32_t addition){
    return addition>0xFFFFFFFFUL-value?0xFFFFFFFFUL:value+addition;
}

static TetrisPiece random_piece(){return (TetrisPiece)Rng_get_range_int(0,(int)TetrisRules::PieceCount-1);}

static bool row_full(const TetrisState& state,uint8_t y){
    for(uint8_t x=0U;x<TetrisRules::Width;x++){if(state.board[y][x]==0U){return false;}}
    return true;
}

static uint8_t clear_lines(TetrisState* state){
    int8_t write=(int8_t)(TetrisRules::Height-1U);uint8_t cleared=0U;
    for(int8_t read=(int8_t)(TetrisRules::Height-1U);read>=0;read--){
        if(row_full(*state,(uint8_t)read)){cleared++;continue;}
        if(write!=read){for(uint8_t x=0U;x<TetrisRules::Width;x++){state->board[(uint8_t)write][x]=state->board[(uint8_t)read][x];}}
        write--;
    }
    while(write>=0){for(uint8_t x=0U;x<TetrisRules::Width;x++){state->board[(uint8_t)write][x]=0U;}write--;}
    return cleared;
}

static void refresh_grounded(TetrisState* state,bool reset_lock,bool was_grounded){
    bool now_grounded=TetrisRules::collides(*state,state->current_piece,state->rotation,state->piece_x,(int8_t)(state->piece_y+1));
    state->grounded=now_grounded?1U:0U;
    if(reset_lock&&was_grounded&&state->lock_resets<TetrisRules::LockResetLimit){state->lock_elapsed_ms=0U;state->lock_resets++;}
    if(!now_grounded){state->lock_elapsed_ms=0U;}
}

static void spawn_next(TetrisState* state){
    state->current_piece=state->next_piece;state->next_piece=random_piece();state->rotation=0U;state->piece_x=7;state->piece_y=0;
    state->fall_elapsed_ms=0U;state->lock_elapsed_ms=0U;state->lock_resets=0U;state->grounded=0U;
    if(TetrisRules::collides(*state,state->current_piece,state->rotation,state->piece_x,state->piece_y)){state->game_over=1U;return;}
    refresh_grounded(state,false,false);
}

static void lock_piece(TetrisState* state,TetrisEvent* event){
    uint8_t value=(uint8_t)state->current_piece+1U;
    for(uint8_t y=0U;y<4U;y++){
        for(uint8_t x=0U;x<4U;x++){
            if(!TetrisRules::shape_cell(state->current_piece,state->rotation,x,y)){continue;}
            int16_t board_x=(int16_t)state->piece_x+x,board_y=(int16_t)state->piece_y+y;
            if(board_x>=0&&board_x<TetrisRules::Width&&board_y>=0&&board_y<TetrisRules::Height){state->board[board_y][board_x]=value;}
        }
    }
    uint8_t cleared=clear_lines(state);uint8_t score_level=TetrisRules::level(*state);
    if(cleared!=0U){
        static const uint16_t line_scores[4]={100U,300U,500U,800U};
        state->score=saturating_add(state->score,(uint32_t)line_scores[cleared-1U]*score_level);
        uint32_t total=(uint32_t)state->lines+cleared;state->lines=total>0xFFFFU?0xFFFFU:(uint16_t)total;
    }
    if(event!=NULL){event->locked=1U;event->lines_cleared=cleared;}
    spawn_next(state);
    if(state->game_over!=0U&&event!=NULL){event->game_over=1U;}
}

}

uint8_t TetrisRules::starting_level(uint8_t difficulty){
    static const uint8_t levels[DifficultyCount]={4U,7U,9U};return levels[difficulty<DifficultyCount?difficulty:0U];
}

uint8_t TetrisRules::level(const TetrisState& state){
    uint16_t value=(uint16_t)starting_level(state.difficulty)+(uint16_t)(state.lines/4U);return value>20U?20U:(uint8_t)value;
}

uint16_t TetrisRules::gravity_period_ms(uint8_t value){
    if(value<1U){value=1U;}else if(value>20U){value=20U;}return gravity_periods[value-1U];
}

bool TetrisRules::shape_cell(TetrisPiece piece,uint8_t rotation,uint8_t x,uint8_t y){
    uint8_t piece_index=(uint8_t)piece;if(piece_index>=PieceCount||rotation>=RotationCount||x>=4U||y>=4U){return false;}
    return (shapes[piece_index][rotation]&(uint16_t)(1U<<((uint8_t)(y*4U+x))))!=0U;
}

bool TetrisRules::collides(const TetrisState& state,TetrisPiece piece,uint8_t rotation,int8_t x,int8_t y){
    for(uint8_t row=0U;row<4U;row++){
        for(uint8_t column=0U;column<4U;column++){
            if(!shape_cell(piece,rotation,column,row)){continue;}
            int16_t board_x=(int16_t)x+column,board_y=(int16_t)y+row;
            if(board_x<0||board_x>=Width||board_y<0||board_y>=Height||state.board[board_y][board_x]!=0U){return true;}
        }
    }
    return false;
}

void TetrisRules::start(TetrisState* state,uint8_t difficulty){
    if(state==NULL){return;}
    for(uint8_t y=0U;y<Height;y++){for(uint8_t x=0U;x<Width;x++){state->board[y][x]=0U;}}
    state->score=0U;state->lines=0U;state->fall_elapsed_ms=0U;state->lock_elapsed_ms=0U;
    state->piece_x=7;state->piece_y=0;state->current_piece=random_piece();state->next_piece=random_piece();state->rotation=0U;
    state->difficulty=difficulty<DifficultyCount?difficulty:0U;state->lock_resets=0U;state->grounded=0U;state->game_over=0U;
    refresh_grounded(state,false,false);
}

bool TetrisRules::move_horizontal(TetrisState* state,int8_t delta_x){
    if(state==NULL||state->game_over!=0U||(delta_x!=-1&&delta_x!=1)){return false;}
    int8_t target=(int8_t)(state->piece_x+delta_x);if(collides(*state,state->current_piece,state->rotation,target,state->piece_y)){return false;}
    bool was_grounded=state->grounded!=0U;state->piece_x=target;refresh_grounded(state,true,was_grounded);return true;
}

bool TetrisRules::rotate_clockwise(TetrisState* state){
    if(state==NULL||state->game_over!=0U){return false;}
    uint8_t old_rotation=state->rotation,new_rotation=(uint8_t)((old_rotation+1U)%RotationCount);bool was_grounded=state->grounded!=0U;
    if(state->current_piece==TetrisPiece::O){state->rotation=new_rotation;refresh_grounded(state,true,was_grounded);return true;}
    const Kick (*table)[5]=state->current_piece==TetrisPiece::I?i_kicks:jlstz_kicks;
    for(uint8_t i=0U;i<5U;i++){
        int8_t target_x=(int8_t)(state->piece_x+table[old_rotation][i].x),target_y=(int8_t)(state->piece_y+table[old_rotation][i].y);
        if(collides(*state,state->current_piece,new_rotation,target_x,target_y)){continue;}
        state->piece_x=target_x;state->piece_y=target_y;state->rotation=new_rotation;refresh_grounded(state,true,was_grounded);return true;
    }
    return false;
}

bool TetrisRules::soft_drop(TetrisState* state){
    if(state==NULL||state->game_over!=0U){return false;}
    int8_t target_y=(int8_t)(state->piece_y+1);
    if(collides(*state,state->current_piece,state->rotation,state->piece_x,target_y)){
        if(state->grounded==0U){state->grounded=1U;state->lock_elapsed_ms=0U;}return false;
    }
    state->piece_y=target_y;state->fall_elapsed_ms=0U;state->score=saturating_add(state->score,1U);refresh_grounded(state,false,false);return true;
}

uint8_t TetrisRules::hard_drop(TetrisState* state,TetrisEvent* event){
    clear_event(event);if(state==NULL||state->game_over!=0U){return 0U;}uint8_t distance=0U;
    while(distance<Height&&!collides(*state,state->current_piece,state->rotation,state->piece_x,(int8_t)(state->piece_y+1))){state->piece_y++;distance++;}
    state->score=saturating_add(state->score,(uint32_t)distance*2U);if(event!=NULL){event->moved=distance!=0U?1U:0U;}lock_piece(state,event);return distance;
}

void TetrisRules::advance(TetrisState* state,uint32_t elapsed_ms,TetrisEvent* event){
    clear_event(event);if(state==NULL||state->game_over!=0U){return;}if(elapsed_ms>MaximumElapsedMs){elapsed_ms=MaximumElapsedMs;}
    uint8_t steps=0U;
    while(elapsed_ms!=0U&&steps<Height&&state->game_over==0U){
        if(state->grounded!=0U){
            uint16_t remaining=state->lock_elapsed_ms<LockDelayMs?(uint16_t)(LockDelayMs-state->lock_elapsed_ms):0U;
            if(elapsed_ms<remaining){state->lock_elapsed_ms=(uint16_t)(state->lock_elapsed_ms+elapsed_ms);elapsed_ms=0U;}
            else{lock_piece(state,event);elapsed_ms=0U;}
            continue;
        }
        uint16_t period=gravity_period_ms(level(*state));uint16_t remaining=state->fall_elapsed_ms<period?(uint16_t)(period-state->fall_elapsed_ms):0U;
        if(elapsed_ms<remaining){state->fall_elapsed_ms=(uint16_t)(state->fall_elapsed_ms+elapsed_ms);elapsed_ms=0U;continue;}
        elapsed_ms-=remaining;state->fall_elapsed_ms=0U;steps++;
        int8_t target_y=(int8_t)(state->piece_y+1);
        if(collides(*state,state->current_piece,state->rotation,state->piece_x,target_y)){state->grounded=1U;state->lock_elapsed_ms=0U;continue;}
        state->piece_y=target_y;if(event!=NULL){event->moved=1U;}refresh_grounded(state,false,false);
    }
}

bool TetrisRules::state_valid(const TetrisState& state){
    if(state.game_over!=0U||state.difficulty>=DifficultyCount||(uint8_t)state.current_piece>=PieceCount||(uint8_t)state.next_piece>=PieceCount||state.rotation>=RotationCount||state.lock_resets>LockResetLimit){return false;}
    if(state.piece_x<-3||state.piece_x>17||state.piece_y<-3||state.piece_y>23||collides(state,state.current_piece,state.rotation,state.piece_x,state.piece_y)){return false;}
    for(uint8_t y=0U;y<Height;y++){
        bool full=true;
        for(uint8_t x=0U;x<Width;x++){if(state.board[y][x]>PieceCount){return false;}if(state.board[y][x]==0U){full=false;}}
        if(full){return false;}
    }
    uint16_t period=gravity_period_ms(level(state));if(state.fall_elapsed_ms>=period){return false;}
    bool grounded=collides(state,state.current_piece,state.rotation,state.piece_x,(int8_t)(state.piece_y+1));
    if((state.grounded!=0U)!=grounded){return false;}
    if(grounded){if(state.lock_elapsed_ms>=LockDelayMs){return false;}}
    else if(state.lock_elapsed_ms!=0U){return false;}
    return true;
}

bool TetrisRules::make_snapshot(const TetrisState& state,TetrisSnapshot* snapshot){
    if(snapshot==NULL||!state_valid(state)){return false;}snapshot->magic=SnapshotMagic;snapshot->version=SnapshotVersion;snapshot->payload_size=sizeof(TetrisSnapshot);
    snapshot->score=state.score;snapshot->lines=state.lines;uint16_t period=gravity_period_ms(level(state));snapshot->fall_remaining_ms=(uint16_t)(period-state.fall_elapsed_ms);
    snapshot->lock_remaining_ms=state.grounded!=0U?(uint16_t)(LockDelayMs-state.lock_elapsed_ms):0U;snapshot->difficulty=state.difficulty;
    snapshot->current_piece=(uint8_t)state.current_piece;snapshot->next_piece=(uint8_t)state.next_piece;snapshot->piece_x=state.piece_x;snapshot->piece_y=state.piece_y;
    snapshot->rotation=state.rotation;snapshot->grounded=state.grounded;snapshot->lock_resets=state.lock_resets;
    for(uint8_t i=0U;i<sizeof(snapshot->reserved);i++){snapshot->reserved[i]=0U;}
    for(uint8_t y=0U;y<Height;y++){for(uint8_t x=0U;x<Width;x++){snapshot->board[y][x]=state.board[y][x];}}
    return true;
}

bool TetrisRules::restore_snapshot(const TetrisSnapshot& snapshot,TetrisState* state){
    if(state==NULL||snapshot.magic!=SnapshotMagic||snapshot.version!=SnapshotVersion||snapshot.payload_size!=sizeof(TetrisSnapshot)||snapshot.difficulty>=DifficultyCount||snapshot.current_piece>=PieceCount||snapshot.next_piece>=PieceCount||snapshot.rotation>=RotationCount||snapshot.grounded>1U||snapshot.lock_resets>LockResetLimit){return false;}
    for(uint8_t i=0U;i<sizeof(snapshot.reserved);i++){if(snapshot.reserved[i]!=0U){return false;}}
    TetrisState& restored=restore_state_work;restored.score=snapshot.score;restored.lines=snapshot.lines;restored.difficulty=snapshot.difficulty;restored.current_piece=(TetrisPiece)snapshot.current_piece;restored.next_piece=(TetrisPiece)snapshot.next_piece;
    restored.piece_x=snapshot.piece_x;restored.piece_y=snapshot.piece_y;restored.rotation=snapshot.rotation;restored.grounded=snapshot.grounded;restored.lock_resets=snapshot.lock_resets;restored.game_over=0U;
    for(uint8_t y=0U;y<Height;y++){for(uint8_t x=0U;x<Width;x++){restored.board[y][x]=snapshot.board[y][x];}}
    uint16_t period=gravity_period_ms(level(restored));if(snapshot.fall_remaining_ms==0U||snapshot.fall_remaining_ms>800U){return false;}
    restored.fall_elapsed_ms=snapshot.fall_remaining_ms>period?0U:(uint16_t)(period-snapshot.fall_remaining_ms);
    if(restored.grounded!=0U){if(snapshot.lock_remaining_ms==0U||snapshot.lock_remaining_ms>LockDelayMs){return false;}restored.lock_elapsed_ms=(uint16_t)(LockDelayMs-snapshot.lock_remaining_ms);}
    else{if(snapshot.lock_remaining_ms!=0U){return false;}restored.lock_elapsed_ms=0U;}
    if(!state_valid(restored)){return false;}*state=restored;return true;
}
