#include "minesweeper_rules.hpp"
#include "rng.h"
#include <limits.h>

namespace {

static uint8_t flood_queue[MinesweeperRules::CellCount];
static MinesweeperState restore_candidate;

static bool valid_cell(uint8_t cell){return cell<MinesweeperRules::CellCount;}

static uint8_t adjacent_mines(const uint8_t* contents,uint8_t cell){
    uint8_t row=MinesweeperRules::cell_row(cell),column=MinesweeperRules::cell_column(cell),count=0U;
    for(int8_t row_delta=-1;row_delta<=1;row_delta++){
        for(int8_t column_delta=-1;column_delta<=1;column_delta++){
            if(row_delta==0&&column_delta==0){continue;}
            int16_t next_row=(int16_t)row+row_delta,next_column=(int16_t)column+column_delta;
            if(next_row<0||next_row>=MinesweeperRules::Height||next_column<0||next_column>=MinesweeperRules::Width){continue;}
            if(contents[MinesweeperRules::cell_index((uint8_t)next_column,(uint8_t)next_row)]==MinesweeperRules::Mine){count++;}
        }
    }
    return count;
}

static void lose(MinesweeperState* state,uint8_t detonated){
    state->phase=MinesweeperPhase::LossReview;state->detonated_cell=detonated;
}

static bool snapshot_fields_valid(const MinesweeperSnapshot& snapshot){
    if(snapshot.magic!=MinesweeperRules::SnapshotMagic||snapshot.version!=MinesweeperRules::SnapshotVersion||snapshot.payload_size!=sizeof(MinesweeperSnapshot)){
        return false;
    }
    for(uint8_t i=0U;i<sizeof(snapshot.reserved);i++){if(snapshot.reserved[i]!=0U){return false;}}
    restore_candidate.elapsed_ms=snapshot.elapsed_ms;restore_candidate.difficulty=snapshot.difficulty;restore_candidate.phase=(MinesweeperPhase)snapshot.phase;
    restore_candidate.selected_cell=snapshot.selected_cell;restore_candidate.remaining_flags=snapshot.remaining_flags;restore_candidate.probes_remaining=snapshot.probes_remaining;restore_candidate.detonated_cell=snapshot.detonated_cell;
    for(uint16_t i=0U;i<MinesweeperRules::CellCount;i++){restore_candidate.contents[i]=snapshot.contents[i];restore_candidate.states[i]=snapshot.states[i];}
    return MinesweeperRules::state_valid(restore_candidate);
}

static void reveal_safe(MinesweeperState* state,uint8_t first,MinesweeperChangeList* changes){
    if(state->contents[first]==MinesweeperRules::Mine||state->states[first]!=static_cast<uint8_t>(MinesweeperCellState::Hidden)){return;}
    uint16_t read=0U,write=0U;state->states[first]=static_cast<uint8_t>(MinesweeperCellState::Open);MinesweeperRules::add_change(changes,first);
    if(state->contents[first]==0U){flood_queue[write++]=first;}
    while(read<write){
        uint8_t cell=flood_queue[read++],row=MinesweeperRules::cell_row(cell),column=MinesweeperRules::cell_column(cell);
        for(int8_t row_delta=-1;row_delta<=1;row_delta++){
            for(int8_t column_delta=-1;column_delta<=1;column_delta++){
                if(row_delta==0&&column_delta==0){continue;}
                int16_t next_row=(int16_t)row+row_delta,next_column=(int16_t)column+column_delta;
                if(next_row<0||next_row>=MinesweeperRules::Height||next_column<0||next_column>=MinesweeperRules::Width){continue;}
                uint8_t next=MinesweeperRules::cell_index((uint8_t)next_column,(uint8_t)next_row);
                if(state->states[next]!=static_cast<uint8_t>(MinesweeperCellState::Hidden)||state->contents[next]==MinesweeperRules::Mine){continue;}
                state->states[next]=static_cast<uint8_t>(MinesweeperCellState::Open);MinesweeperRules::add_change(changes,next);
                if(state->contents[next]==0U&&write<MinesweeperRules::CellCount){flood_queue[write++]=next;}
            }
        }
    }
}

static MinesweeperActionResult verify_finished_flags(MinesweeperState* state){
    if(state->remaining_flags!=0U){return MinesweeperActionResult::Changed;}
    if(MinesweeperRules::flags_correct(*state)){return MinesweeperActionResult::Victory;}
    lose(state,MinesweeperRules::NoCell);return MinesweeperActionResult::Loss;
}

static uint8_t choose_initial_cell(const uint8_t* contents){
    uint16_t candidates=0U;
    for(uint16_t i=0U;i<MinesweeperRules::CellCount;i++){if(contents[i]==0U){candidates++;}}
    bool require_blank=candidates!=0U;
    if(!require_blank){for(uint16_t i=0U;i<MinesweeperRules::CellCount;i++){if(contents[i]!=MinesweeperRules::Mine){candidates++;}}}
    uint16_t selected=(uint16_t)Rng_get_range_int(0,(int)candidates-1);
    for(uint16_t i=0U;i<MinesweeperRules::CellCount;i++){
        bool candidate=require_blank?contents[i]==0U:contents[i]!=MinesweeperRules::Mine;
        if(candidate){if(selected==0U){return (uint8_t)i;}selected--;}
    }
    return 0U;
}

}

namespace MinesweeperRules {

uint8_t mine_count(uint8_t difficulty){
    static const uint8_t counts[DifficultyCount]={25U,34U,47U,61U};
    return counts[difficulty<DifficultyCount?difficulty:0U];
}

uint8_t probe_count(uint8_t difficulty){
    static const uint8_t counts[DifficultyCount]={1U,2U,3U,9U};
    return counts[difficulty<DifficultyCount?difficulty:0U];
}

uint8_t cell_index(uint8_t column,uint8_t row){return (uint8_t)(row*Width+column);}
uint8_t cell_column(uint8_t cell){return (uint8_t)(cell%Width);}
uint8_t cell_row(uint8_t cell){return (uint8_t)(cell/Width);}

void clear_changes(MinesweeperChangeList* changes){if(changes!=0){changes->count=0U;}}

void add_change(MinesweeperChangeList* changes,uint8_t cell){
    if(changes==0||!valid_cell(cell)){return;}
    for(uint16_t i=0U;i<changes->count;i++){if(changes->cells[i]==cell){return;}}
    if(changes->count<CellCount){changes->cells[changes->count++]=cell;}
}

bool flags_correct(const MinesweeperState& state){
    for(uint16_t i=0U;i<CellCount;i++){
        bool mine=state.contents[i]==Mine,flagged=state.states[i]==static_cast<uint8_t>(MinesweeperCellState::Flagged);
        if(mine!=flagged){return false;}
    }
    return true;
}

bool state_valid(const MinesweeperState& state){
    if(state.difficulty>=DifficultyCount||state.selected_cell>=CellCount||state.probes_remaining>probe_count(state.difficulty)||state.remaining_flags>mine_count(state.difficulty)){
        return false;
    }
    if(state.phase!=MinesweeperPhase::Running&&state.phase!=MinesweeperPhase::LossReview){return false;}
    uint8_t mines=0U,flags=0U;
    for(uint16_t i=0U;i<CellCount;i++){
        if(state.contents[i]==Mine){mines++;}
        else if(state.contents[i]>8U||state.contents[i]!=adjacent_mines(state.contents,(uint8_t)i)){return false;}
        if(state.states[i]>static_cast<uint8_t>(MinesweeperCellState::Flagged)){return false;}
        if(state.states[i]==static_cast<uint8_t>(MinesweeperCellState::Open)&&state.contents[i]==Mine){return false;}
        if(state.states[i]==static_cast<uint8_t>(MinesweeperCellState::Flagged)){flags++;}
    }
    if(mines!=mine_count(state.difficulty)||flags!=(uint8_t)(mines-state.remaining_flags)){return false;}
    if(state.phase==MinesweeperPhase::Running){return state.detonated_cell==NoCell&&state.remaining_flags!=0U;}
    if(state.detonated_cell!=NoCell&&(state.detonated_cell>=CellCount||state.contents[state.detonated_cell]!=Mine)){return false;}
    if(state.detonated_cell==NoCell&&(state.remaining_flags!=0U||flags_correct(state))){return false;}
    return true;
}

bool update_fastest_time(uint32_t* fastest_times,uint8_t difficulty,uint32_t seconds,bool victory){
    if(fastest_times==0||difficulty>=DifficultyCount||seconds==0U||!victory){return false;}
    if(fastest_times[difficulty]!=0U&&seconds>=fastest_times[difficulty]){return false;}
    fastest_times[difficulty]=seconds;return true;
}

void add_elapsed_ms(MinesweeperState* state,uint32_t elapsed_ms){
    if(state==0||state->phase!=MinesweeperPhase::Running){return;}
    state->elapsed_ms=UINT32_MAX-state->elapsed_ms<elapsed_ms?UINT32_MAX:state->elapsed_ms+elapsed_ms;
}

void start(MinesweeperState* state,uint8_t difficulty){
    if(state==0){return;}
    state->elapsed_ms=0U;state->difficulty=difficulty<DifficultyCount?difficulty:0U;state->phase=MinesweeperPhase::Running;
    state->selected_cell=NoCell;state->remaining_flags=mine_count(state->difficulty);state->probes_remaining=probe_count(state->difficulty);state->detonated_cell=NoCell;
    for(uint16_t i=0U;i<CellCount;i++){state->contents[i]=0U;state->states[i]=static_cast<uint8_t>(MinesweeperCellState::Hidden);}
    uint8_t placed=0U,target=mine_count(state->difficulty);
    while(placed<target){uint8_t cell=(uint8_t)Rng_get_range_int(0,(int)CellCount-1);if(state->contents[cell]!=Mine){state->contents[cell]=Mine;placed++;}}
    for(uint16_t i=0U;i<CellCount;i++){if(state->contents[i]!=Mine){state->contents[i]=adjacent_mines(state->contents,(uint8_t)i);}}
    state->selected_cell=choose_initial_cell(state->contents);
}

MinesweeperActionResult reveal(MinesweeperState* state,uint8_t cell,MinesweeperChangeList* changes){
    if(state==0||state->phase!=MinesweeperPhase::Running||!valid_cell(cell)||state->states[cell]!=static_cast<uint8_t>(MinesweeperCellState::Hidden)){return MinesweeperActionResult::None;}
    if(state->contents[cell]==Mine){lose(state,cell);return MinesweeperActionResult::Loss;}
    reveal_safe(state,cell,changes);return MinesweeperActionResult::Changed;
}

MinesweeperActionResult toggle_flag(MinesweeperState* state,uint8_t cell,MinesweeperChangeList* changes){
    if(state==0||state->phase!=MinesweeperPhase::Running||!valid_cell(cell)){return MinesweeperActionResult::None;}
    if(state->states[cell]==static_cast<uint8_t>(MinesweeperCellState::Flagged)){
        state->states[cell]=static_cast<uint8_t>(MinesweeperCellState::Hidden);state->remaining_flags++;add_change(changes,cell);return MinesweeperActionResult::Changed;
    }
    if(state->states[cell]!=static_cast<uint8_t>(MinesweeperCellState::Hidden)||state->remaining_flags==0U){return MinesweeperActionResult::None;}
    state->states[cell]=static_cast<uint8_t>(MinesweeperCellState::Flagged);state->remaining_flags--;add_change(changes,cell);return verify_finished_flags(state);
}

MinesweeperActionResult probe(MinesweeperState* state,uint8_t cell,MinesweeperChangeList* changes){
    if(state==0||state->phase!=MinesweeperPhase::Running||!valid_cell(cell)||state->states[cell]!=static_cast<uint8_t>(MinesweeperCellState::Hidden)){return MinesweeperActionResult::None;}
    if(state->probes_remaining==0U){return MinesweeperActionResult::Error;}
    state->probes_remaining--;
    if(state->contents[cell]!=Mine){reveal_safe(state,cell,changes);return MinesweeperActionResult::Changed;}
    state->states[cell]=static_cast<uint8_t>(MinesweeperCellState::Flagged);state->remaining_flags--;add_change(changes,cell);return verify_finished_flags(state);
}

MinesweeperActionResult quick_action(MinesweeperState* state,uint8_t cell,MinesweeperChangeList* changes){
    if(state==0||state->phase!=MinesweeperPhase::Running||!valid_cell(cell)||state->states[cell]!=static_cast<uint8_t>(MinesweeperCellState::Open)||state->contents[cell]==Mine){return MinesweeperActionResult::None;}
    uint8_t row=cell_row(cell),column=cell_column(cell),flags=0U,hidden=0U;
    for(int8_t row_delta=-1;row_delta<=1;row_delta++){
        for(int8_t column_delta=-1;column_delta<=1;column_delta++){
            if(row_delta==0&&column_delta==0){continue;}int16_t next_row=(int16_t)row+row_delta,next_column=(int16_t)column+column_delta;
            if(next_row<0||next_row>=Height||next_column<0||next_column>=Width){continue;}uint8_t next=cell_index((uint8_t)next_column,(uint8_t)next_row);
            if(state->states[next]==static_cast<uint8_t>(MinesweeperCellState::Flagged)){flags++;}
            else if(state->states[next]==static_cast<uint8_t>(MinesweeperCellState::Hidden)){hidden++;}
        }
    }
    if(hidden==0U){return MinesweeperActionResult::None;}
    if(flags==state->contents[cell]){
        bool changed=false;
        for(int8_t row_delta=-1;row_delta<=1;row_delta++){
            for(int8_t column_delta=-1;column_delta<=1;column_delta++){
                if(row_delta==0&&column_delta==0){continue;}int16_t next_row=(int16_t)row+row_delta,next_column=(int16_t)column+column_delta;
                if(next_row<0||next_row>=Height||next_column<0||next_column>=Width){continue;}uint8_t next=cell_index((uint8_t)next_column,(uint8_t)next_row);
                if(state->states[next]!=static_cast<uint8_t>(MinesweeperCellState::Hidden)){continue;}
                if(state->contents[next]==Mine){lose(state,next);return MinesweeperActionResult::Loss;}
                reveal_safe(state,next,changes);changed=true;
            }
        }
        return changed?MinesweeperActionResult::Changed:MinesweeperActionResult::None;
    }
    if(state->contents[cell]>=flags&&(uint8_t)(state->contents[cell]-flags)==hidden){
        if(hidden>state->remaining_flags){return MinesweeperActionResult::Error;}
        for(int8_t row_delta=-1;row_delta<=1;row_delta++){
            for(int8_t column_delta=-1;column_delta<=1;column_delta++){
                if(row_delta==0&&column_delta==0){continue;}int16_t next_row=(int16_t)row+row_delta,next_column=(int16_t)column+column_delta;
                if(next_row<0||next_row>=Height||next_column<0||next_column>=Width){continue;}uint8_t next=cell_index((uint8_t)next_column,(uint8_t)next_row);
                if(state->states[next]==static_cast<uint8_t>(MinesweeperCellState::Hidden)){state->states[next]=static_cast<uint8_t>(MinesweeperCellState::Flagged);add_change(changes,next);}
            }
        }
        state->remaining_flags=(uint8_t)(state->remaining_flags-hidden);return verify_finished_flags(state);
    }
    return MinesweeperActionResult::None;
}

bool make_snapshot(const MinesweeperState& state,MinesweeperSnapshot* snapshot){
    if(snapshot==0||!state_valid(state)){return false;}
    snapshot->magic=SnapshotMagic;snapshot->version=SnapshotVersion;snapshot->payload_size=sizeof(MinesweeperSnapshot);snapshot->elapsed_ms=state.elapsed_ms;
    snapshot->difficulty=state.difficulty;snapshot->phase=(uint8_t)state.phase;snapshot->selected_cell=state.selected_cell;snapshot->remaining_flags=state.remaining_flags;
    snapshot->probes_remaining=state.probes_remaining;snapshot->detonated_cell=state.detonated_cell;
    for(uint8_t i=0U;i<sizeof(snapshot->reserved);i++){snapshot->reserved[i]=0U;}
    for(uint16_t i=0U;i<CellCount;i++){snapshot->contents[i]=state.contents[i];snapshot->states[i]=state.states[i];}
    return true;
}

bool restore_snapshot(const MinesweeperSnapshot& snapshot,MinesweeperState* state){
    if(state==0||!snapshot_fields_valid(snapshot)){return false;}
    state->elapsed_ms=snapshot.elapsed_ms;state->difficulty=snapshot.difficulty;state->phase=(MinesweeperPhase)snapshot.phase;state->selected_cell=snapshot.selected_cell;
    state->remaining_flags=snapshot.remaining_flags;state->probes_remaining=snapshot.probes_remaining;state->detonated_cell=snapshot.detonated_cell;
    for(uint16_t i=0U;i<CellCount;i++){state->contents[i]=snapshot.contents[i];state->states[i]=snapshot.states[i];}
    return true;
}

}
