#include "minesweeper_game.hpp"
#include "basic.h"
#include "lcd.h"
#include "save.hpp"
#include "ui.hpp"
#include "ui_sound.hpp"

#define MINESWEEPER_DATA_VERSION 1U

namespace {

#pragma pack(push,1)
struct MinesweeperPersistentData {uint32_t fastest_seconds[MinesweeperRules::DifficultyCount];};
#pragma pack(pop)

static_assert(sizeof(MinesweeperPersistentData)==16U,"Minesweeper persistent format changed");
static_assert(sizeof(MinesweeperPersistentData)<=SAVE_GAME_DATA_CAPACITY,"Minesweeper data exceeds game slot");
static_assert(sizeof(MinesweeperSnapshot)<=SAVE_SNAPSHOT_CAPACITY,"Minesweeper snapshot exceeds save capacity");

static MinesweeperSnapshot snapshot_work;

static constexpr uint8_t StatusDifficulty=1U<<0;
static constexpr uint8_t StatusFlags=1U<<1;
static constexpr uint8_t StatusTime=1U<<2;
static constexpr uint8_t StatusProbes=1U<<3;
static constexpr uint8_t StatusHint=1U<<4;
static constexpr uint8_t StatusAll=StatusDifficulty|StatusFlags|StatusTime|StatusProbes|StatusHint;
static constexpr uint16_t BoardGridColor=0x4208U;
static constexpr uint16_t ClosedColor=0x7BEFU;
static constexpr uint16_t OpenColor=0xD69AU;
static constexpr uint16_t SelectedColor=0xFEA0U;
static constexpr uint16_t RevealedMineColor=0xBDF7U;
static constexpr uint16_t WrongFlagColor=0xFBAEU;
static constexpr uint16_t DetonatedColor=0xF800U;
static constexpr uint16_t FlagColor=0xEBAEU;

}

MinesweeperGame::MinesweeperGame():repeat_{0U,0U},last_update_ms_(0U),drawn_seconds_(0U),status_dirty_(StatusAll),time_anchor_pending_(true),full_dirty_(true),draw_requested_(false),victory_pending_(false),finish_requested_(false){
    state_.elapsed_ms=0U;state_.difficulty=0U;state_.phase=MinesweeperPhase::Running;state_.selected_cell=112U;state_.remaining_flags=25U;state_.probes_remaining=1U;state_.detonated_cell=MinesweeperRules::NoCell;
    for(uint16_t i=0U;i<MinesweeperRules::CellCount;i++){state_.contents[i]=0U;state_.states[i]=(uint8_t)MinesweeperCellState::Hidden;changes_.cells[i]=0U;}
    changes_.count=0U;persistent_defaults();
}

GameId MinesweeperGame::id() const{return GameId::Minesweeper;}
const char* MinesweeperGame::title() const{return "Minesweeper";}
uint8_t MinesweeperGame::variant_count() const{return MinesweeperRules::DifficultyCount;}
uint8_t MinesweeperGame::default_variant() const{return 0U;}
const char* MinesweeperGame::variant_page_title() const{return "Difficulty";}
UiTextId MinesweeperGame::variant_action_text() const{return UiTextId::AdjustDifficulty;}
uint16_t MinesweeperGame::persistent_data_version() const{return MINESWEEPER_DATA_VERSION;}

void MinesweeperGame::persistent_defaults(){for(uint8_t i=0U;i<MinesweeperRules::DifficultyCount;i++){fastest_seconds_[i]=0U;}}

bool MinesweeperGame::persistent_data_valid(const uint8_t* data,uint16_t length,uint16_t version) const{
    if(data==NULL||length!=sizeof(MinesweeperPersistentData)||version!=MINESWEEPER_DATA_VERSION){return false;}
    const MinesweeperPersistentData* value=(const MinesweeperPersistentData*)data;
    for(uint8_t i=0U;i<MinesweeperRules::DifficultyCount;i++){if(value->fastest_seconds[i]>UINT32_MAX/1000U){return false;}}
    return true;
}

void MinesweeperGame::load_persistent_data(const uint8_t* data,uint16_t length){
    if(!persistent_data_valid(data,length,MINESWEEPER_DATA_VERSION)){return;}
    for(uint8_t difficulty=0U;difficulty<MinesweeperRules::DifficultyCount;difficulty++){
        uint8_t offset=(uint8_t)(difficulty*4U);fastest_seconds_[difficulty]=(uint32_t)data[offset]|((uint32_t)data[offset+1U]<<8)|((uint32_t)data[offset+2U]<<16)|((uint32_t)data[offset+3U]<<24);
    }
}

uint16_t MinesweeperGame::make_persistent_data(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(MinesweeperPersistentData)){return 0U;}
    for(uint8_t difficulty=0U;difficulty<MinesweeperRules::DifficultyCount;difficulty++){
        uint8_t offset=(uint8_t)(difficulty*4U);uint32_t value=fastest_seconds_[difficulty];data[offset]=(uint8_t)value;data[offset+1U]=(uint8_t)(value>>8);data[offset+2U]=(uint8_t)(value>>16);data[offset+3U]=(uint8_t)(value>>24);
    }
    return sizeof(MinesweeperPersistentData);
}

GameOverview MinesweeperGame::persistent_overview(uint8_t selected_variant) const{
    uint8_t difficulty=selected_variant<MinesweeperRules::DifficultyCount?selected_variant:default_variant();
    return {fastest_seconds_[difficulty],0U,MinesweeperRules::DifficultyCount,MinesweeperRules::DifficultyCount,difficulty,{0U,0U,0U}};
}

GameRecordSummary MinesweeperGame::record_summary(const uint8_t* data,uint16_t length,uint16_t version) const{
    GameRecordSummary summary={0U,0U,0U,1U,0U};if(!persistent_data_valid(data,length,version)){return summary;}
    const MinesweeperPersistentData& value=*(const MinesweeperPersistentData*)data;
    for(uint8_t i=0U;i<MinesweeperRules::DifficultyCount;i++){uint32_t seconds=value.fastest_seconds[i];if(seconds!=0U&&(summary.valid==0U||seconds<summary.value)){summary.value=seconds;summary.variant=i;summary.valid=1U;}}
    return summary;
}

void MinesweeperGame::format_record_variant(uint8_t variant,char* text,uint8_t capacity) const{if(text==NULL||capacity==0U){return;}char value[12];difficulty_text(variant,value);copy_ascii(value,text,capacity);}

void MinesweeperGame::apply_result(const GameResult& value,bool* new_record,bool* new_unlock){
    bool record=MinesweeperRules::update_fastest_time(fastest_seconds_,value.variant,value.score,value.outcome==GameOutcome::PlayerVictory);
    if(new_record!=NULL){*new_record=record;}if(new_unlock!=NULL){*new_unlock=false;}
}

void MinesweeperGame::start_new(uint8_t variant){
    MinesweeperRules::start(&state_,variant);MinesweeperRules::clear_changes(&changes_);Input_repeat_reset(&repeat_);last_update_ms_=0U;drawn_seconds_=0U;time_anchor_pending_=true;
    status_dirty_=StatusAll;full_dirty_=true;draw_requested_=false;victory_pending_=false;finish_requested_=false;
}

void MinesweeperGame::accumulate_time(uint32_t now_ms){
    if(state_.phase!=MinesweeperPhase::Running||victory_pending_){return;}
    if(time_anchor_pending_){last_update_ms_=now_ms;time_anchor_pending_=false;return;}
    uint32_t previous_seconds=state_.elapsed_ms/1000U;MinesweeperRules::add_elapsed_ms(&state_,now_ms-last_update_ms_);last_update_ms_=now_ms;
    if(state_.elapsed_ms/1000U!=previous_seconds){status_dirty_|=StatusTime;draw_requested_=true;}
}

void MinesweeperGame::move_selection(int8_t row_delta,int8_t column_delta){
    int16_t row=(int16_t)MinesweeperRules::cell_row(state_.selected_cell)+row_delta,column=(int16_t)MinesweeperRules::cell_column(state_.selected_cell)+column_delta;
    if(row<0||row>=MinesweeperRules::Height||column<0||column>=MinesweeperRules::Width){return;}
    uint8_t previous=state_.selected_cell;state_.selected_cell=MinesweeperRules::cell_index((uint8_t)column,(uint8_t)row);MinesweeperRules::add_change(&changes_,previous);MinesweeperRules::add_change(&changes_,state_.selected_cell);
    draw_requested_=true;UiSound_play(UiSoundId::Move);
}

void MinesweeperGame::mark_loss_cells(){
    for(uint16_t cell=0U;cell<MinesweeperRules::CellCount;cell++){
        bool mine=state_.contents[cell]==MinesweeperRules::Mine,flagged=state_.states[cell]==(uint8_t)MinesweeperCellState::Flagged;
        if(mine||(flagged&&!mine)){MinesweeperRules::add_change(&changes_,(uint8_t)cell);}
    }
}

void MinesweeperGame::process_action(MinesweeperActionResult action,UiSoundId success_sound,uint8_t old_flags,uint8_t old_probes){
    if(state_.remaining_flags!=old_flags){status_dirty_|=StatusFlags;}
    if(state_.probes_remaining!=old_probes){status_dirty_|=StatusProbes;}
    if(action==MinesweeperActionResult::None){return;}
    if(action==MinesweeperActionResult::Error){UiSound_play(UiSoundId::Error);return;}
    if(action==MinesweeperActionResult::Victory){victory_pending_=true;return;}
    if(action==MinesweeperActionResult::Loss){mark_loss_cells();status_dirty_|=StatusHint;draw_requested_=true;UiSound_play(UiSoundId::LifeLost);return;}
    draw_requested_=true;UiSound_play(state_.remaining_flags<old_flags?UiSoundId::MinesweeperFlag:success_sound);
}

void MinesweeperGame::handle_input(const InputEvent& input){
    if(state_.phase==MinesweeperPhase::LossReview){
        Input_repeat_reset(&repeat_);if(Input_is_pressed(input,InputAction::Confirm)){finish_requested_=true;}return;
    }
    if(victory_pending_){Input_repeat_reset(&repeat_);return;}
    uint32_t now=HAL_GetTick();uint16_t repeated=Input_repeat_update(&repeat_,input.state,input.pressed,(uint16_t)(KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT),now,300U,85U);
    if(repeated==KEY_UP){move_selection(-1,0);return;}if(repeated==KEY_DOWN){move_selection(1,0);return;}if(repeated==KEY_LEFT){move_selection(0,-1);return;}if(repeated==KEY_RIGHT){move_selection(0,1);return;}
    uint8_t old_flags=state_.remaining_flags,old_probes=state_.probes_remaining;MinesweeperActionResult action=MinesweeperActionResult::None;
    if(Input_is_pressed(input,InputAction::Confirm)){action=MinesweeperRules::reveal(&state_,state_.selected_cell,&changes_);process_action(action,UiSoundId::GameAction,old_flags,old_probes);}
    else if(Input_is_pressed(input,InputAction::Cancel)){action=MinesweeperRules::toggle_flag(&state_,state_.selected_cell,&changes_);process_action(action,UiSoundId::GameAction,old_flags,old_probes);}
    else if(Input_is_pressed(input,InputAction::Action3)){action=MinesweeperRules::probe(&state_,state_.selected_cell,&changes_);process_action(action,UiSoundId::GameAction,old_flags,old_probes);}
    else if(Input_is_pressed(input,InputAction::Action4)){action=MinesweeperRules::quick_action(&state_,state_.selected_cell,&changes_);process_action(action,UiSoundId::GameAction,old_flags,old_probes);}
}

GameSignal MinesweeperGame::update(uint32_t now_ms){
    if(victory_pending_||finish_requested_){return GameSignal::GameOver;}
    accumulate_time(now_ms);if(draw_requested_){draw_requested_=false;return GameSignal::NeedsDraw;}return GameSignal::None;
}

uint16_t MinesweeperGame::number_color(uint8_t number){
    static const uint16_t colors[8]={0x001FU,0x0400U,0xF800U,0x0010U,0x8000U,0x07FFU,0x0000U,0x8410U};return number>=1U&&number<=8U?colors[number-1U]:0x0000U;
}

void MinesweeperGame::draw_flag(Ui& ui,uint16_t x,uint16_t y,uint16_t background){
    ui.fill((uint16_t)(x+6U),(uint16_t)(y+3U),2U,1U,FlagColor);ui.fill((uint16_t)(x+5U),(uint16_t)(y+4U),3U,1U,FlagColor);
    ui.fill((uint16_t)(x+4U),(uint16_t)(y+5U),4U,1U,FlagColor);ui.fill((uint16_t)(x+3U),(uint16_t)(y+6U),5U,1U,FlagColor);
    ui.fill((uint16_t)(x+4U),(uint16_t)(y+7U),4U,1U,FlagColor);ui.fill((uint16_t)(x+6U),(uint16_t)(y+8U),2U,1U,FlagColor);
    ui.fill((uint16_t)(x+7U),(uint16_t)(y+3U),2U,9U,0x0000U);ui.fill((uint16_t)(x+4U),(uint16_t)(y+11U),8U,2U,0x0000U);
    (void)background;
}

void MinesweeperGame::draw_mine(Ui& ui,uint16_t x,uint16_t y,uint16_t background){
    ui.filled_circle((int16_t)(x+8U),(int16_t)(y+8U),4U,0x0000U);Lcd_draw_line((int16_t)(x+2U),(int16_t)(y+8U),(int16_t)(x+14U),(int16_t)(y+8U),0x0000U);
    Lcd_draw_line((int16_t)(x+8U),(int16_t)(y+2U),(int16_t)(x+8U),(int16_t)(y+14U),0x0000U);ui.fill((uint16_t)(x+7U),(uint16_t)(y+6U),2U,2U,background==DetonatedColor?0xFFFFU:UI_COLOR_DANGER);
}

void MinesweeperGame::draw_cell(Ui& ui,uint8_t cell) const{
    uint8_t column=MinesweeperRules::cell_column(cell),row=MinesweeperRules::cell_row(cell);uint16_t x=(uint16_t)column*16U,y=(uint16_t)row*16U;bool selected=cell==state_.selected_cell;
    uint8_t cell_state=state_.states[cell];bool loss=state_.phase==MinesweeperPhase::LossReview,mine=state_.contents[cell]==MinesweeperRules::Mine,wrong=loss&&cell_state==(uint8_t)MinesweeperCellState::Flagged&&!mine;
    uint16_t background=cell_state==(uint8_t)MinesweeperCellState::Open?OpenColor:ClosedColor;
    if(loss&&mine){background=cell==state_.detonated_cell?DetonatedColor:RevealedMineColor;}else if(wrong){background=WrongFlagColor;}else if(selected){background=SelectedColor;}
    uint16_t inner_width=column+1U==MinesweeperRules::Width?14U:15U,inner_height=row+1U==MinesweeperRules::Height?14U:15U;
    ui.fill(x,y,16U,16U,BoardGridColor);ui.fill((uint16_t)(x+1U),(uint16_t)(y+1U),inner_width,inner_height,background);
    if(column+1U==MinesweeperRules::Width){ui.fill(239U,y,1U,16U,UI_COLOR_LINE);}
    if(loss&&mine){draw_mine(ui,x,y,background);return;}
    if(wrong){draw_flag(ui,x,y,background);Lcd_draw_line((int16_t)(x+3U),(int16_t)(y+3U),(int16_t)(x+13U),(int16_t)(y+13U),UI_COLOR_DANGER);Lcd_draw_line((int16_t)(x+13U),(int16_t)(y+3U),(int16_t)(x+3U),(int16_t)(y+13U),UI_COLOR_DANGER);return;}
    if(cell_state==(uint8_t)MinesweeperCellState::Flagged){draw_flag(ui,x,y,background);return;}
    if(cell_state==(uint8_t)MinesweeperCellState::Open&&state_.contents[cell]!=0U){char value[2]={(char)('0'+state_.contents[cell]),'\0'};ui.ascii((uint16_t)(x+5U),(uint16_t)(y+2U),value,number_color(state_.contents[cell]),background,12U);}
}

void MinesweeperGame::difficulty_text(uint8_t difficulty,char* text){
    static const char* labels[MinesweeperRules::DifficultyCount]={"Easy","Normal","Hard","Ultra"};const char* source=labels[difficulty<MinesweeperRules::DifficultyCount?difficulty:0U];uint8_t i=0U;
    while(source[i]!='\0'){text[i]=source[i];i++;}text[i]='\0';
}

void MinesweeperGame::draw_status(Ui& ui,bool full_redraw){
    char value[16];if(full_redraw){
        ui.fill(240U,0U,80U,240U,UI_COLOR_PANEL);ui.fill(239U,0U,1U,240U,UI_COLOR_LINE);
        ui.text(245U,4U,UiTextId::Difficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);ui.text(245U,58U,UiTextId::MinesMarkedTotal,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
        ui.text(245U,112U,UiTextId::ElapsedSeconds,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);ui.text(245U,166U,UiTextId::ProbeCount,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);status_dirty_=StatusAll;
    }
    if((status_dirty_&StatusDifficulty)!=0U){ui.fill(244U,25U,74U,20U,UI_COLOR_PANEL);difficulty_text(state_.difficulty,value);ui.ascii_fit(245U,27U,72U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);}
    if((status_dirty_&StatusFlags)!=0U){
        ui.fill(244U,79U,74U,20U,UI_COLOR_PANEL);uint8_t flags=(uint8_t)(MinesweeperRules::mine_count(state_.difficulty)-state_.remaining_flags);format_u32(flags,value);uint8_t end=0U;while(value[end]!='\0'){end++;}value[end++]='/';char total[4];format_u32(MinesweeperRules::mine_count(state_.difficulty),total);uint8_t i=0U;while(total[i]!='\0'){value[end++]=total[i++];}value[end]='\0';ui.ascii_fit(245U,81U,72U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    }
    if((status_dirty_&StatusTime)!=0U){ui.fill(244U,133U,74U,20U,UI_COLOR_PANEL);drawn_seconds_=state_.elapsed_ms/1000U;format_u32(drawn_seconds_,value);ui.ascii_fit(245U,135U,72U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);}
    if((status_dirty_&StatusProbes)!=0U){
        ui.fill(244U,184U,74U,54U,UI_COLOR_PANEL);uint8_t total=MinesweeperRules::probe_count(state_.difficulty);
        for(uint8_t i=0U;i<total;i++){int16_t x=(int16_t)(253U+(i%3U)*22U),y=(int16_t)(194U+(i/3U)*19U);if(i<state_.probes_remaining){ui.filled_circle(x,y,5U,UI_COLOR_FOCUS);}else{Lcd_draw_circle(x,y,5U,UI_COLOR_MUTED);}}
    }
    if((status_dirty_&StatusHint)!=0U&&state_.phase==MinesweeperPhase::LossReview){ui.fill(244U,184U,74U,54U,UI_COLOR_PANEL);ui.text(247U,205U,UiTextId::PressYContinue,UI_COLOR_FOCUS,UI_COLOR_PANEL,16U);}
}

void MinesweeperGame::draw(Ui& ui,bool full_redraw){
    if(full_redraw||full_dirty_){ui.fill(0U,0U,240U,240U,BoardGridColor);for(uint16_t cell=0U;cell<MinesweeperRules::CellCount;cell++){draw_cell(ui,(uint8_t)cell);}draw_status(ui,true);}
    else{for(uint16_t i=0U;i<changes_.count;i++){draw_cell(ui,changes_.cells[i]);}draw_status(ui,false);}
    MinesweeperRules::clear_changes(&changes_);status_dirty_=0U;full_dirty_=false;
}

uint16_t MinesweeperGame::make_snapshot(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(MinesweeperSnapshot)||victory_pending_||!MinesweeperRules::make_snapshot(state_,&snapshot_work)){return 0U;}
    const uint8_t* source=(const uint8_t*)&snapshot_work;for(uint16_t i=0U;i<sizeof(snapshot_work);i++){data[i]=source[i];}return sizeof(snapshot_work);
}

bool MinesweeperGame::restore(const uint8_t* data,uint16_t length){
    if(data==NULL||length!=sizeof(MinesweeperSnapshot)){return false;}uint8_t* target=(uint8_t*)&snapshot_work;for(uint16_t i=0U;i<sizeof(snapshot_work);i++){target[i]=data[i];}
    if(!MinesweeperRules::restore_snapshot(snapshot_work,&state_)){return false;}MinesweeperRules::clear_changes(&changes_);Input_repeat_reset(&repeat_);last_update_ms_=0U;drawn_seconds_=state_.elapsed_ms/1000U;time_anchor_pending_=true;
    status_dirty_=StatusAll;full_dirty_=true;draw_requested_=false;victory_pending_=false;finish_requested_=false;return true;
}

GameResult MinesweeperGame::result() const{
    uint32_t seconds=state_.elapsed_ms/1000U;GameOutcome outcome=GameOutcome::InProgress;if(victory_pending_){outcome=GameOutcome::PlayerVictory;if(seconds==0U){seconds=1U;}}else if(state_.phase==MinesweeperPhase::LossReview){outcome=GameOutcome::Failure;if(seconds==0U){seconds=1U;}}
    return {seconds,0U,0U,state_.difficulty,0U,outcome,UiTextId::ElapsedSeconds,UiTextId::ElapsedSeconds,0U};
}

UiTextId MinesweeperGame::display_name() const{return UiTextId::Minesweeper;}
UiTextId MinesweeperGame::menu_goal_text() const{return UiTextId::GoalMinesweeper;}
void MinesweeperGame::format_menu_mode(char* text,uint8_t capacity) const{if(text==NULL||capacity==0U){return;}char value[12];difficulty_text(state_.difficulty,value);copy_ascii(value,text,capacity);}
uint8_t MinesweeperGame::menu_control_count() const{return 5U;}
UiTextId MinesweeperGame::menu_control_text(uint8_t index) const{static const UiTextId controls[5]={UiTextId::ControlMinesSelect,UiTextId::ControlMinesOpen,UiTextId::ControlMinesFlag,UiTextId::ControlMinesProbe,UiTextId::ControlMinesQuick};return controls[index<5U?index:0U];}

void MinesweeperGame::draw_home_thumbnail(Ui& ui,uint16_t x,uint16_t y,uint16_t width,uint16_t height) const{
    static constexpr uint16_t thumbnail_size=72U;if(width<thumbnail_size||height<thumbnail_size){return;}uint16_t start_x=(uint16_t)(x+(width-thumbnail_size)/2U),start_y=(uint16_t)(y+(height-thumbnail_size)/2U);
    for(uint8_t row=0U;row<5U;row++){for(uint8_t column=0U;column<5U;column++){
        uint16_t left=(uint16_t)(column*thumbnail_size/5U),right=(uint16_t)((column+1U)*thumbnail_size/5U),top=(uint16_t)(row*thumbnail_size/5U),bottom=(uint16_t)((row+1U)*thumbnail_size/5U);
        uint16_t px=(uint16_t)(start_x+left),py=(uint16_t)(start_y+top),cell_width=(uint16_t)(right-left-1U),cell_height=(uint16_t)(bottom-top-1U);bool open=(row+column)%3U==0U;ui.fill(px,py,cell_width,cell_height,open?OpenColor:ClosedColor);
        if(open){uint8_t number=(uint8_t)((row+column)%4U+1U);char text[2]={(char)('0'+number),'\0'};ui.ascii((uint16_t)(px+(cell_width-6U)/2U),(uint16_t)(py+(cell_height-12U)/2U),text,number_color(number),OpenColor,12U);}
    }}
}

void MinesweeperGame::draw_entry_page(Ui& ui,const GameActivity& activity,const GameOverview& overview) const{
    char value[20];ui.fill(16U,38U,288U,126U,UI_COLOR_PANEL);Lcd_draw_rect(16U,38U,288U,126U,UI_COLOR_LINE);ui.fill(27U,49U,88U,88U,BoardGridColor);Lcd_draw_rect(27U,49U,88U,88U,UI_COLOR_LINE);
    for(uint8_t row=0U;row<5U;row++){for(uint8_t column=0U;column<5U;column++){uint16_t px=(uint16_t)(34U+column*15U),py=(uint16_t)(56U+row*15U);bool open=(row+column)%3U==0U;ui.fill(px,py,14U,14U,open?OpenColor:ClosedColor);if(open){uint8_t number=(uint8_t)((row+column)%4U+1U);char text[2]={(char)('0'+number),'\0'};ui.ascii((uint16_t)(px+4U),(uint16_t)(py+1U),text,number_color(number),OpenColor,12U);}}}
    ui.text(132U,45U,UiTextId::Minesweeper,UI_COLOR_PRIMARY,UI_COLOR_PANEL,24U);
    ui.text(132U,78U,UiTextId::Difficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);difficulty_text(overview.last_variant,value);ui.ascii_fit(224U,78U,68U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(132U,101U,UiTextId::FastestSeconds,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);if(overview.high_score==0U){value[0]='-';value[1]='-';value[2]='\0';}else{format_u32(overview.high_score,value);}ui.ascii_fit(224U,101U,68U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(28U,143U,UiTextId::RecentPlay,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);format_activity(activity,value);ui.ascii_fit(100U,143U,166U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
}

void MinesweeperGame::draw_entry_control(Ui& ui,uint8_t control,bool selected,bool snapshot_available) const{
    if(control==0U){ui.large_button(16U,180U,140U,snapshot_available?UiTextId::ContinueGame:UiTextId::StartGame,selected);}else if(control==1U){ui.large_button(164U,180U,140U,UiTextId::AdjustDifficulty,selected);}
}

bool MinesweeperGame::variant_unlocked(const GameProgressView&,uint8_t variant) const{return variant<MinesweeperRules::DifficultyCount;}

void MinesweeperGame::draw_variant_select(Ui& ui,uint8_t selected,uint8_t previous,const GameProgressView&,bool page_redraw) const{
    auto draw_choice=[&ui](uint8_t difficulty,bool active){uint16_t x=(uint16_t)(64U+difficulty*64U),background=active?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL,border=active?UI_COLOR_FOCUS:UI_COLOR_LINE;char value[8];difficulty_text(difficulty,value);
        ui.fill((uint16_t)(x-25U),98U,50U,48U,background);Lcd_draw_rect((uint16_t)(x-25U),98U,50U,48U,border);uint16_t width=Ui::text_width(value,16U);ui.ascii((uint16_t)(x-width/2U),112U,value,active?UI_COLOR_FOCUS:UI_COLOR_TEXT,background,16U);};
    if(page_redraw){ui.fill(16U,38U,288U,164U,UI_COLOR_PANEL);Lcd_draw_rect(16U,38U,288U,164U,UI_COLOR_LINE);ui.text(112U,52U,UiTextId::AdjustDifficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,24U);ui.lr_hint(25U,114U,270U,UI_COLOR_PANEL);
        for(uint8_t i=0U;i<MinesweeperRules::DifficultyCount;i++){draw_choice(i,i==selected);}ui.text(104U,176U,UiTextId::PromptSaveCancel,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);}else{draw_choice(previous,false);draw_choice(selected,true);}
}

GameSelectSignal MinesweeperGame::handle_variant_select(const InputEvent& input,uint8_t* variant) const{
    if(variant==NULL||*variant>=MinesweeperRules::DifficultyCount){return GameSelectSignal::None;}
    if(Input_is_pressed(input,InputAction::Action1)){*variant=*variant==0U?(MinesweeperRules::DifficultyCount-1U):(uint8_t)(*variant-1U);return GameSelectSignal::SelectionChanged;}
    if(Input_is_pressed(input,InputAction::Action2)){*variant=(uint8_t)((*variant+1U)%MinesweeperRules::DifficultyCount);return GameSelectSignal::SelectionChanged;}
    if(Input_is_pressed(input,InputAction::Confirm)){return GameSelectSignal::Confirmed;}return GameSelectSignal::None;
}
