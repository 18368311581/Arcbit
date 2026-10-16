#include "layer_elimination_game.hpp"
#include "basic.h"
#include "lcd.h"
#include "save.hpp"
#include "ui.hpp"
#include "ui_sound.hpp"

#define LAYER_ELIMINATION_DATA_VERSION 1U

namespace {

#pragma pack(push,1)
struct LayerEliminationPersistentData {uint32_t fastest_seconds[LayerEliminationRules::VariantCount];};
#pragma pack(pop)

static_assert(sizeof(LayerEliminationPersistentData)==24U,"Layer elimination persistent format changed");
static_assert(sizeof(LayerEliminationPersistentData)<=SAVE_GAME_DATA_CAPACITY,"Layer elimination data exceeds game slot");
static_assert(sizeof(LayerEliminationSnapshot)<=SAVE_SNAPSHOT_CAPACITY,"Layer elimination snapshot exceeds save capacity");

static LayerEliminationSnapshot snapshot_work;
static constexpr uint16_t GridColor=0x8410U;
static constexpr uint16_t BlackColor=0x0000U;
static constexpr uint16_t WhiteColor=0xFFFFU;

}

LayerEliminationGame::LayerEliminationGame():repeat_{0U,0U},last_update_ms_(0U),drawn_seconds_(0U),select_focus_(0U),time_anchor_pending_(true),full_dirty_(true),status_dirty_(true),modal_dirty_(false),viewing_target_(false),draw_requested_(false),victory_pending_(false),finish_requested_(false){
    for(uint8_t i=0U;i<LayerEliminationRules::BitsetBytes;i++){state_.current[i]=0U;state_.target[i]=0U;state_.answer[i]=0U;dirty_[i]=0U;}
    state_.elapsed_ms=0U;state_.variant=default_variant();state_.selected_cell=27U;state_.phase=LayerEliminationPhase::Playing;state_.reset_focus=0U;persistent_defaults();
}

GameId LayerEliminationGame::id() const{return GameId::LayerElimination;}
const char* LayerEliminationGame::title() const{return "Layer Clear";}
uint8_t LayerEliminationGame::variant_count() const{return LayerEliminationRules::VariantCount;}
uint8_t LayerEliminationGame::default_variant() const{return 1U;}
const char* LayerEliminationGame::variant_page_title() const{return "Layer Settings";}
UiTextId LayerEliminationGame::variant_action_text() const{return UiTextId::AdjustDifficulty;}
uint16_t LayerEliminationGame::persistent_data_version() const{return LAYER_ELIMINATION_DATA_VERSION;}

void LayerEliminationGame::persistent_defaults(){for(uint8_t i=0U;i<LayerEliminationRules::VariantCount;i++){fastest_seconds_[i]=0U;}}

bool LayerEliminationGame::persistent_data_valid(const uint8_t* data,uint16_t length,uint16_t version) const{
    if(data==NULL||length!=sizeof(LayerEliminationPersistentData)||version!=LAYER_ELIMINATION_DATA_VERSION){return false;}
    const LayerEliminationPersistentData* value=(const LayerEliminationPersistentData*)data;
    for(uint8_t i=0U;i<LayerEliminationRules::VariantCount;i++){if(value->fastest_seconds[i]>UINT32_MAX/1000U){return false;}}
    return true;
}

void LayerEliminationGame::load_persistent_data(const uint8_t* data,uint16_t length){
    if(!persistent_data_valid(data,length,LAYER_ELIMINATION_DATA_VERSION)){return;}
    for(uint8_t variant=0U;variant<LayerEliminationRules::VariantCount;variant++){uint8_t offset=(uint8_t)(variant*4U);fastest_seconds_[variant]=(uint32_t)data[offset]|((uint32_t)data[offset+1U]<<8)|((uint32_t)data[offset+2U]<<16)|((uint32_t)data[offset+3U]<<24);}
}

uint16_t LayerEliminationGame::make_persistent_data(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(LayerEliminationPersistentData)){return 0U;}
    for(uint8_t variant=0U;variant<LayerEliminationRules::VariantCount;variant++){uint8_t offset=(uint8_t)(variant*4U);uint32_t value=fastest_seconds_[variant];data[offset]=(uint8_t)value;data[offset+1U]=(uint8_t)(value>>8);data[offset+2U]=(uint8_t)(value>>16);data[offset+3U]=(uint8_t)(value>>24);}
    return sizeof(LayerEliminationPersistentData);
}

GameOverview LayerEliminationGame::persistent_overview(uint8_t selected_variant) const{
    uint8_t variant=selected_variant<LayerEliminationRules::VariantCount?selected_variant:default_variant();return {fastest_seconds_[variant],0U,LayerEliminationRules::VariantCount,LayerEliminationRules::VariantCount,variant,{0U,0U,0U}};
}

GameRecordSummary LayerEliminationGame::record_summary(const uint8_t* data,uint16_t length,uint16_t version) const{
    GameRecordSummary summary={0U,0U,0U,1U,0U};if(!persistent_data_valid(data,length,version)){return summary;}const LayerEliminationPersistentData& value=*(const LayerEliminationPersistentData*)data;
    for(uint8_t i=0U;i<LayerEliminationRules::VariantCount;i++){uint32_t seconds=value.fastest_seconds[i];if(seconds!=0U&&(summary.valid==0U||seconds<summary.value)){summary.value=seconds;summary.variant=i;summary.valid=1U;}}
    return summary;
}

void LayerEliminationGame::format_record_variant(uint8_t variant,char* text,uint8_t capacity) const{mode_text(variant,text,capacity);}

void LayerEliminationGame::apply_result(const GameResult& value,bool* new_record,bool* new_unlock){
    bool record=LayerEliminationRules::update_fastest_time(fastest_seconds_,value.variant,value.score,value.outcome==GameOutcome::PlayerVictory);if(new_record!=NULL){*new_record=record;}if(new_unlock!=NULL){*new_unlock=false;}
}

void LayerEliminationGame::clear_dirty(){for(uint8_t i=0U;i<sizeof(dirty_);i++){dirty_[i]=0U;}}
void LayerEliminationGame::mark_dirty(uint8_t cell){if(cell<LayerEliminationRules::MaxCells){LayerEliminationRules::bit_set(dirty_,cell,true);}}

void LayerEliminationGame::mark_cross(uint8_t cell){
    uint8_t size=LayerEliminationRules::grid_size(state_.variant),row=LayerEliminationRules::cell_row(cell,size),column=LayerEliminationRules::cell_column(cell,size);mark_dirty(cell);
    if(row>0U){mark_dirty(LayerEliminationRules::cell_index(column,(uint8_t)(row-1U),size));}if(row+1U<size){mark_dirty(LayerEliminationRules::cell_index(column,(uint8_t)(row+1U),size));}
    if(column>0U){mark_dirty(LayerEliminationRules::cell_index((uint8_t)(column-1U),row,size));}if(column+1U<size){mark_dirty(LayerEliminationRules::cell_index((uint8_t)(column+1U),row,size));}
}

void LayerEliminationGame::mark_selection(uint8_t cell){mark_dirty(cell);}

void LayerEliminationGame::start_new(uint8_t variant){
    LayerEliminationRules::start(&state_,variant);Input_repeat_reset(&repeat_);clear_dirty();last_update_ms_=0U;drawn_seconds_=0U;time_anchor_pending_=true;full_dirty_=true;status_dirty_=true;modal_dirty_=false;viewing_target_=false;draw_requested_=false;victory_pending_=LayerEliminationRules::complete(state_);finish_requested_=false;
}

void LayerEliminationGame::accumulate_time(uint32_t now_ms){
    if(time_anchor_pending_){last_update_ms_=now_ms;time_anchor_pending_=false;return;}uint32_t previous=state_.elapsed_ms/1000U;LayerEliminationRules::add_elapsed_ms(&state_,now_ms-last_update_ms_);last_update_ms_=now_ms;
    if(state_.elapsed_ms/1000U!=previous){status_dirty_=true;draw_requested_=true;}
}

void LayerEliminationGame::move_selection(int8_t row_delta,int8_t column_delta){
    uint8_t size=LayerEliminationRules::grid_size(state_.variant);int16_t row=(int16_t)LayerEliminationRules::cell_row(state_.selected_cell,size)+row_delta,column=(int16_t)LayerEliminationRules::cell_column(state_.selected_cell,size)+column_delta;
    if(row<0||row>=size||column<0||column>=size){return;}uint8_t previous=state_.selected_cell;mark_selection(previous);state_.selected_cell=LayerEliminationRules::cell_index((uint8_t)column,(uint8_t)row,size);mark_selection(state_.selected_cell);draw_requested_=true;UiSound_play(UiSoundId::Move);
}

void LayerEliminationGame::handle_input(const InputEvent& input){
    if(state_.phase==LayerEliminationPhase::AnswerReview){Input_repeat_reset(&repeat_);if(Input_is_pressed(input,InputAction::Confirm)){finish_requested_=true;}return;}
    if(state_.phase==LayerEliminationPhase::ResetConfirm||state_.phase==LayerEliminationPhase::AnswerConfirm){
        Input_repeat_reset(&repeat_);if(Input_is_pressed(input,InputAction::Cancel)){state_.phase=LayerEliminationPhase::Playing;state_.reset_focus=0U;full_dirty_=true;draw_requested_=true;UiSound_play(UiSoundId::Back);return;}
        if(Input_is_pressed(input,InputAction::Up)||Input_is_pressed(input,InputAction::Down)||Input_is_pressed(input,InputAction::Left)||Input_is_pressed(input,InputAction::Right)){state_.reset_focus=state_.reset_focus==0U?1U:0U;modal_dirty_=true;draw_requested_=true;UiSound_play(UiSoundId::Move);return;}
        if(Input_is_pressed(input,InputAction::Confirm)){if(state_.reset_focus==0U){state_.phase=LayerEliminationPhase::Playing;state_.reset_focus=0U;full_dirty_=true;draw_requested_=true;UiSound_play(UiSoundId::Back);}else if(state_.phase==LayerEliminationPhase::ResetConfirm){LayerEliminationRules::reset_current(&state_);full_dirty_=true;draw_requested_=true;UiSound_play(UiSoundId::Saved);}else{state_.phase=LayerEliminationPhase::AnswerReview;state_.reset_focus=0U;full_dirty_=true;status_dirty_=true;draw_requested_=true;UiSound_play(UiSoundId::LifeLost);}return;}
        return;
    }
    if(Input_is_pressed(input,InputAction::Cancel)){Input_repeat_reset(&repeat_);viewing_target_=true;full_dirty_=true;draw_requested_=true;UiSound_play(UiSoundId::Confirm);return;}
    if(Input_is_released(input,InputAction::Cancel)&&viewing_target_){viewing_target_=false;full_dirty_=true;draw_requested_=true;UiSound_play(UiSoundId::Back);return;}
    if(viewing_target_){Input_repeat_reset(&repeat_);return;}
    uint16_t repeated=Input_repeat_update(&repeat_,input.state,input.pressed,(uint16_t)(KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT),HAL_GetTick(),300U,85U);
    if(repeated==KEY_UP){move_selection(-1,0);return;}if(repeated==KEY_DOWN){move_selection(1,0);return;}if(repeated==KEY_LEFT){move_selection(0,-1);return;}if(repeated==KEY_RIGHT){move_selection(0,1);return;}
    if(Input_is_pressed(input,InputAction::Confirm)){mark_cross(state_.selected_cell);LayerEliminationRules::toggle_cross(state_.current,LayerEliminationRules::grid_size(state_.variant),state_.selected_cell);victory_pending_=LayerEliminationRules::complete(state_);draw_requested_=true;UiSound_play(UiSoundId::GameAction);return;}
    if(Input_is_pressed(input,InputAction::Action3)&&!LayerEliminationRules::all_white(state_)){state_.phase=LayerEliminationPhase::ResetConfirm;state_.reset_focus=0U;modal_dirty_=true;draw_requested_=true;UiSound_play(UiSoundId::Confirm);}
    else if(Input_is_pressed(input,InputAction::Action4)){state_.phase=LayerEliminationPhase::AnswerConfirm;state_.reset_focus=0U;modal_dirty_=true;draw_requested_=true;UiSound_play(UiSoundId::Confirm);}
}

GameSignal LayerEliminationGame::update(uint32_t now_ms){
    if(state_.phase!=LayerEliminationPhase::AnswerReview){accumulate_time(now_ms);}if(victory_pending_||finish_requested_){return GameSignal::GameOver;}if(draw_requested_){draw_requested_=false;return GameSignal::NeedsDraw;}return GameSignal::None;
}

void LayerEliminationGame::draw_cell(Ui& ui,uint8_t cell) const{
    uint8_t size=LayerEliminationRules::grid_size(state_.variant),cell_size=(uint8_t)(240U/size),row=LayerEliminationRules::cell_row(cell,size),column=LayerEliminationRules::cell_column(cell,size);uint16_t x=(uint16_t)column*cell_size,y=(uint16_t)row*cell_size;
    uint16_t color=LayerEliminationRules::bit_get(state_.current,cell)?BlackColor:WhiteColor;ui.fill(x,y,cell_size,cell_size,GridColor);ui.fill((uint16_t)(x+1U),(uint16_t)(y+1U),(uint16_t)(cell_size-2U),(uint16_t)(cell_size-2U),color);
    if(state_.phase==LayerEliminationPhase::AnswerReview&&LayerEliminationRules::bit_get(state_.answer,cell)){Lcd_draw_line((int16_t)(x+4U),(int16_t)(y+4U),(int16_t)(x+cell_size-5U),(int16_t)(y+cell_size-5U),UI_COLOR_DANGER);Lcd_draw_line((int16_t)(x+cell_size-5U),(int16_t)(y+4U),(int16_t)(x+4U),(int16_t)(y+cell_size-5U),UI_COLOR_DANGER);}
    else if(state_.phase!=LayerEliminationPhase::AnswerReview&&cell==state_.selected_cell){Lcd_draw_rect((uint16_t)(x+2U),(uint16_t)(y+2U),(uint16_t)(cell_size-4U),(uint16_t)(cell_size-4U),UI_COLOR_FOCUS);Lcd_draw_rect((uint16_t)(x+3U),(uint16_t)(y+3U),(uint16_t)(cell_size-6U),(uint16_t)(cell_size-6U),UI_COLOR_FOCUS);}
}

void LayerEliminationGame::draw_current_grid(Ui& ui) const{uint8_t size=LayerEliminationRules::grid_size(state_.variant);for(uint8_t cell=0U;cell<(uint8_t)(size*size);cell++){draw_cell(ui,cell);}}

void LayerEliminationGame::draw_target_grid(Ui& ui,uint16_t x,uint16_t y,uint16_t span) const{
    uint8_t size=LayerEliminationRules::grid_size(state_.variant);uint16_t cell_size=(uint16_t)(span/size);for(uint8_t row=0U;row<size;row++){for(uint8_t column=0U;column<size;column++){uint8_t cell=LayerEliminationRules::cell_index(column,row,size);ui.fill((uint16_t)(x+column*cell_size),(uint16_t)(y+row*cell_size),cell_size,cell_size,LayerEliminationRules::bit_get(state_.target,cell)?BlackColor:WhiteColor);}}
}

void LayerEliminationGame::draw_status(Ui& ui,bool full_redraw){
    char value[16];if(full_redraw){ui.fill(240U,0U,80U,240U,UI_COLOR_PANEL);ui.fill(239U,0U,1U,240U,UI_COLOR_LINE);draw_target_grid(ui,244U,4U,72U);Lcd_draw_rect(243U,3U,74U,74U,UI_COLOR_LINE);ui.text(246U,84U,UiTextId::GridSize,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);ui.text(246U,128U,UiTextId::Difficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);ui.text(246U,172U,UiTextId::ElapsedSeconds,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);size_text(state_.variant,value);ui.ascii_fit(246U,105U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);ui.ascii_fit(246U,149U,70U,difficulty_text(state_.variant),UI_COLOR_PRIMARY,UI_COLOR_PANEL);status_dirty_=true;}
    if(status_dirty_){ui.fill(244U,193U,74U,45U,UI_COLOR_PANEL);drawn_seconds_=state_.elapsed_ms/1000U;format_u32(drawn_seconds_,value);ui.ascii_fit(246U,193U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);if(state_.phase==LayerEliminationPhase::AnswerReview){ui.text(246U,217U,UiTextId::PressYContinue,UI_COLOR_FOCUS,UI_COLOR_PANEL,16U);}status_dirty_=false;}
}

void LayerEliminationGame::draw(Ui& ui,bool full_redraw){
    if(full_redraw||full_dirty_){if(viewing_target_){draw_target_grid(ui,0U,0U,240U);}else{draw_current_grid(ui);}draw_status(ui,true);clear_dirty();full_dirty_=false;}
    else{if(!viewing_target_){uint8_t size=LayerEliminationRules::grid_size(state_.variant);for(uint8_t cell=0U;cell<(uint8_t)(size*size);cell++){if(LayerEliminationRules::bit_get(dirty_,cell)){draw_cell(ui,cell);}}clear_dirty();}draw_status(ui,false);}
    if((state_.phase==LayerEliminationPhase::ResetConfirm||state_.phase==LayerEliminationPhase::AnswerConfirm)&&(full_redraw||modal_dirty_)){ui.dialog(state_.phase==LayerEliminationPhase::ResetConfirm?UiTextId::ResetLayerPrompt:UiTextId::RevealLayerPrompt,state_.reset_focus);modal_dirty_=false;}
}

uint16_t LayerEliminationGame::make_snapshot(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(LayerEliminationSnapshot)||victory_pending_||!LayerEliminationRules::make_snapshot(state_,&snapshot_work)){return 0U;}const uint8_t* source=(const uint8_t*)&snapshot_work;for(uint16_t i=0U;i<sizeof(snapshot_work);i++){data[i]=source[i];}return sizeof(snapshot_work);
}

bool LayerEliminationGame::restore(const uint8_t* data,uint16_t length){
    if(data==NULL||length!=sizeof(LayerEliminationSnapshot)){return false;}uint8_t* target=(uint8_t*)&snapshot_work;for(uint16_t i=0U;i<sizeof(snapshot_work);i++){target[i]=data[i];}if(!LayerEliminationRules::restore_snapshot(snapshot_work,&state_)){return false;}
    Input_repeat_reset(&repeat_);clear_dirty();last_update_ms_=0U;drawn_seconds_=state_.elapsed_ms/1000U;time_anchor_pending_=true;full_dirty_=true;status_dirty_=true;modal_dirty_=state_.phase==LayerEliminationPhase::ResetConfirm||state_.phase==LayerEliminationPhase::AnswerConfirm;viewing_target_=false;draw_requested_=false;victory_pending_=false;finish_requested_=false;return true;
}

GameResult LayerEliminationGame::result() const{uint32_t seconds=state_.elapsed_ms/1000U;GameOutcome outcome=victory_pending_?GameOutcome::PlayerVictory:(state_.phase==LayerEliminationPhase::AnswerReview?GameOutcome::Failure:GameOutcome::InProgress);if(outcome!=GameOutcome::InProgress&&seconds==0U){seconds=1U;}return {seconds,0U,0U,state_.variant,0U,outcome,UiTextId::ElapsedSeconds,UiTextId::ElapsedSeconds,0U};}
UiTextId LayerEliminationGame::display_name() const{return UiTextId::LayerElimination;}
UiTextId LayerEliminationGame::menu_goal_text() const{return UiTextId::GoalLayerElimination;}

void LayerEliminationGame::size_text(uint8_t variant,char* text){uint8_t size=LayerEliminationRules::grid_size(variant);uint8_t used=0U;if(size>=10U){text[used++]=(char)('0'+size/10U);}text[used++]=(char)('0'+size%10U);text[used++]='x';if(size>=10U){text[used++]=(char)('0'+size/10U);}text[used++]=(char)('0'+size%10U);text[used]='\0';}
const char* LayerEliminationGame::difficulty_text(uint8_t variant){static const char* const values[LayerEliminationRules::DifficultyCount]={"Easy","Normal","Hard"};return values[LayerEliminationRules::difficulty(variant)];}

void LayerEliminationGame::mode_text(uint8_t variant,char* text,uint8_t capacity){
    if(text==NULL||capacity==0U){return;}char size[8];size_text(variant,size);copy_ascii(size,text,capacity);uint8_t used=0U;while(used+1U<capacity&&text[used]!='\0'){used++;}if(used+1U<capacity){text[used++]=' ';}const char* difficulty=difficulty_text(variant);uint8_t index=0U;while(used+1U<capacity&&difficulty[index]!='\0'){text[used++]=difficulty[index++];}text[used]='\0';
}

void LayerEliminationGame::format_menu_mode(char* text,uint8_t capacity) const{mode_text(state_.variant,text,capacity);}
uint8_t LayerEliminationGame::menu_control_count() const{return 5U;}
UiTextId LayerEliminationGame::menu_control_text(uint8_t index) const{return index==0U?UiTextId::ControlLayerSelect:(index==1U?UiTextId::ControlLayerFlip:(index==2U?UiTextId::ControlLayerReset:(index==3U?UiTextId::ControlLayerView:UiTextId::ControlLayerReveal)));}

void LayerEliminationGame::draw_home_thumbnail(Ui& ui,uint16_t x,uint16_t y,uint16_t width,uint16_t height) const{
    uint16_t size=width<height?width:height;if(size<48U){return;}uint16_t cell=(uint16_t)(size/6U),span=(uint16_t)(cell*6U),left=(uint16_t)(x+(width-span)/2U),top=(uint16_t)(y+(height-span)/2U);
    static const uint8_t pattern[36]={0U,1U,0U,0U,1U,0U,1U,1U,1U,0U,1U,1U,0U,1U,0U,1U,1U,0U,0U,0U,1U,1U,0U,0U,1U,0U,1U,0U,1U,1U,0U,1U,0U,0U,1U,0U};
    for(uint8_t row=0U;row<6U;row++){for(uint8_t column=0U;column<6U;column++){uint16_t px=(uint16_t)(left+column*cell),py=(uint16_t)(top+row*cell);ui.fill(px,py,cell,cell,GridColor);ui.fill((uint16_t)(px+1U),(uint16_t)(py+1U),(uint16_t)(cell-2U),(uint16_t)(cell-2U),pattern[row*6U+column]!=0U?BlackColor:WhiteColor);}}
}

void LayerEliminationGame::draw_entry_page(Ui& ui,const GameActivity& activity,const GameOverview& overview) const{
    char value[24];ui.fill(16U,38U,288U,126U,UI_COLOR_PANEL);Lcd_draw_rect(16U,38U,288U,126U,UI_COLOR_LINE);ui.fill(27U,49U,88U,88U,UI_COLOR_PANEL);draw_home_thumbnail(ui,27U,49U,88U,88U);Lcd_draw_rect(27U,49U,88U,88U,UI_COLOR_LINE);
    ui.text(132U,45U,UiTextId::LayerElimination,UI_COLOR_PRIMARY,UI_COLOR_PANEL,24U);ui.text(132U,78U,UiTextId::Mode,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);mode_text(overview.last_variant,value,sizeof(value));ui.ascii_fit(208U,78U,102U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(132U,101U,UiTextId::FastestSeconds,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);if(overview.high_score==0U){value[0]='-';value[1]='-';value[2]='\0';}else{format_u32(overview.high_score,value);}ui.ascii_fit(238U,101U,54U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);ui.text(28U,143U,UiTextId::RecentPlay,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);format_activity(activity,value);ui.ascii_fit(100U,143U,166U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
}

void LayerEliminationGame::draw_entry_control(Ui& ui,uint8_t control,bool selected,bool snapshot_available) const{if(control==0U){ui.large_button(16U,180U,140U,snapshot_available?UiTextId::ContinueGame:UiTextId::StartGame,selected);}else if(control==1U){ui.large_button(164U,180U,140U,UiTextId::AdjustDifficulty,selected);}}
bool LayerEliminationGame::variant_unlocked(const GameProgressView&,uint8_t variant) const{return variant<LayerEliminationRules::VariantCount;}

void LayerEliminationGame::draw_variant_select(Ui& ui,uint8_t selected,uint8_t previous,const GameProgressView&,bool page_redraw) const{
    if(selected>=LayerEliminationRules::VariantCount){selected=default_variant();}bool redraw_both=page_redraw||selected==previous;if(page_redraw){ui.fill(16U,38U,288U,164U,UI_COLOR_PANEL);Lcd_draw_rect(16U,38U,288U,164U,UI_COLOR_LINE);ui.text(112U,52U,UiTextId::AdjustDifficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,24U);ui.text(104U,176U,UiTextId::PromptSaveCancel,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);}
    uint16_t first_bg=select_focus_==0U?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL,second_bg=select_focus_==1U?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL;
    if(redraw_both||select_focus_==0U){char value[8];size_text(selected,value);ui.fill(18U,88U,284U,32U,first_bg);ui.text(28U,96U,UiTextId::GridSize,select_focus_==0U?UI_COLOR_FOCUS:UI_COLOR_TEXT,first_bg,16U);if(select_focus_==0U){ui.lr_hint(216U,96U,78U,first_bg);}ui.ascii(238U,96U,value,UI_COLOR_MUTED,first_bg,16U);}
    if(redraw_both||select_focus_==1U){const char* value=difficulty_text(selected);ui.fill(18U,126U,284U,32U,second_bg);ui.text(28U,134U,UiTextId::Difficulty,select_focus_==1U?UI_COLOR_FOCUS:UI_COLOR_TEXT,second_bg,16U);if(select_focus_==1U){ui.lr_hint(216U,134U,78U,second_bg);}ui.ascii(238U,134U,value,UI_COLOR_MUTED,second_bg,16U);}
}

GameSelectSignal LayerEliminationGame::handle_variant_select(const InputEvent& input,uint8_t* variant) const{
    if(variant==NULL||*variant>=LayerEliminationRules::VariantCount){return GameSelectSignal::None;}if(Input_is_pressed(input,InputAction::Up)||Input_is_pressed(input,InputAction::Down)){select_focus_=select_focus_==0U?1U:0U;return GameSelectSignal::SelectionChanged;}
    int8_t delta=Input_is_pressed(input,InputAction::Action1)?-1:(Input_is_pressed(input,InputAction::Action2)?1:0);if(delta!=0){uint8_t size=(uint8_t)(*variant/3U),difficulty=(uint8_t)(*variant%3U);if(select_focus_==0U){size^=1U;}else{difficulty=delta<0?(difficulty==0U?2U:(uint8_t)(difficulty-1U)):(uint8_t)((difficulty+1U)%3U);}*variant=(uint8_t)(size*3U+difficulty);return GameSelectSignal::SelectionChanged;}if(Input_is_pressed(input,InputAction::Confirm)){return GameSelectSignal::Confirmed;}return GameSelectSignal::None;
}
