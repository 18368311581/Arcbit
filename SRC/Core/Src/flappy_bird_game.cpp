#include "flappy_bird_game.hpp"
#include "basic.h"
#include "save.hpp"
#include "ui.hpp"
#include "ui_sound.hpp"

#define FLAPPY_BIRD_DATA_VERSION 1U

namespace {

#pragma pack(push,1)
struct FlappyBirdPersistentData {uint32_t high_score[FlappyBirdRules::DifficultyCount];};
#pragma pack(pop)

static_assert(sizeof(FlappyBirdPersistentData)==16U,"Flappy Bird persistent format changed");
static_assert(sizeof(FlappyBirdPersistentData)<=SAVE_GAME_DATA_CAPACITY,"Flappy Bird data exceeds game slot");
static_assert(sizeof(FlappyBirdSnapshot)<=SAVE_SNAPSHOT_CAPACITY,"Flappy Bird snapshot exceeds save capacity");

static FlappyBirdSnapshot snapshot_work;

static void fill_clipped(Ui& ui,int16_t x,int16_t y,int16_t width,int16_t height,uint16_t color){
    int16_t right=(int16_t)(x+width),bottom=(int16_t)(y+height);
    if(x<0){x=0;}if(y<0){y=0;}if(right>(int16_t)FlappyBirdRules::PlayWidth){right=(int16_t)FlappyBirdRules::PlayWidth;}if(bottom>(int16_t)FlappyBirdRules::PlayHeight){bottom=(int16_t)FlappyBirdRules::PlayHeight;}
    if(right>x&&bottom>y){ui.fill((uint16_t)x,(uint16_t)y,(uint16_t)(right-x),(uint16_t)(bottom-y),color);}
}

}

FlappyBirdGame::FlappyBirdGame():last_update_ms_(0U),drawn_bird_y_(0),full_dirty_(true),status_dirty_(true),draw_requested_(false),game_over_pending_(false),prompt_dirty_(false){
    for(uint8_t i=0U;i<FlappyBirdRules::PipeCount;i++){drawn_pipe_x_[i]=0;}
    const uint8_t selected=default_variant();const FlappyBirdDifficulty& config=FlappyBirdRules::difficulty(selected);
    const uint16_t gap_top=(uint16_t)((FlappyBirdRules::PlayHeight-config.gap_height)/2U);
    state_.score=0U;state_.bird_y_q8=(int32_t)(FlappyBirdRules::PlayHeight-FlappyBirdRules::BirdHeight)/2*FlappyBirdRules::FixedOne;state_.bird_velocity_q8=0;
    state_.accumulator_ms=0U;state_.difficulty=selected;state_.phase=FlappyBirdPhase::Ready;
    for(uint8_t i=0U;i<FlappyBirdRules::PipeCount;i++){
        state_.pipes[i].x_q8=(int32_t)(260U+(uint16_t)i*config.spacing)*FlappyBirdRules::FixedOne;
        state_.pipes[i].gap_top=gap_top;state_.pipes[i].scored=0U;state_.pipes[i].reserved=0U;
    }
    persistent_defaults();
}

GameId FlappyBirdGame::id() const{return GameId::FlappyBird;}
const char* FlappyBirdGame::title() const{return "Flappy Bird";}
uint8_t FlappyBirdGame::variant_count() const{return FlappyBirdRules::DifficultyCount;}
uint8_t FlappyBirdGame::default_variant() const{return 1U;}
const char* FlappyBirdGame::variant_page_title() const{return "Difficulty";}
UiTextId FlappyBirdGame::variant_action_text() const{return UiTextId::AdjustDifficulty;}
uint16_t FlappyBirdGame::persistent_data_version() const{return FLAPPY_BIRD_DATA_VERSION;}

void FlappyBirdGame::persistent_defaults(){for(uint8_t i=0U;i<FlappyBirdRules::DifficultyCount;i++){high_scores_[i]=0U;}}

bool FlappyBirdGame::persistent_data_valid(const uint8_t* data,uint16_t length,uint16_t version) const{return data!=NULL&&length==sizeof(FlappyBirdPersistentData)&&version==FLAPPY_BIRD_DATA_VERSION;}

void FlappyBirdGame::load_persistent_data(const uint8_t* data,uint16_t length){
    if(!persistent_data_valid(data,length,FLAPPY_BIRD_DATA_VERSION)){return;}
    for(uint8_t i=0U;i<FlappyBirdRules::DifficultyCount;i++){uint8_t offset=(uint8_t)(i*4U);high_scores_[i]=(uint32_t)data[offset]|((uint32_t)data[offset+1U]<<8)|((uint32_t)data[offset+2U]<<16)|((uint32_t)data[offset+3U]<<24);}
}

uint16_t FlappyBirdGame::make_persistent_data(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(FlappyBirdPersistentData)){return 0U;}
    for(uint8_t i=0U;i<FlappyBirdRules::DifficultyCount;i++){uint8_t offset=(uint8_t)(i*4U);uint32_t value=high_scores_[i];data[offset]=(uint8_t)value;data[offset+1U]=(uint8_t)(value>>8);data[offset+2U]=(uint8_t)(value>>16);data[offset+3U]=(uint8_t)(value>>24);}
    return sizeof(FlappyBirdPersistentData);
}

GameOverview FlappyBirdGame::persistent_overview(uint8_t selected_variant) const{
    uint8_t variant=selected_variant<FlappyBirdRules::DifficultyCount?selected_variant:default_variant();
    return {high_scores_[variant],0U,FlappyBirdRules::DifficultyCount,FlappyBirdRules::DifficultyCount,variant,{0U,0U,0U}};
}

GameRecordSummary FlappyBirdGame::record_summary(const uint8_t* data,uint16_t length,uint16_t version) const{
    GameRecordSummary summary={0U,0U,0U,0U,0U};if(!persistent_data_valid(data,length,version)){return summary;}
    const FlappyBirdPersistentData& value=*(const FlappyBirdPersistentData*)data;
    for(uint8_t i=0U;i<FlappyBirdRules::DifficultyCount;i++){if(value.high_score[i]>summary.value){summary.value=value.high_score[i];summary.variant=i;}}
    summary.valid=summary.value!=0U?1U:0U;return summary;
}

void FlappyBirdGame::format_record_variant(uint8_t variant,char* text,uint8_t capacity) const{if(text==NULL||capacity==0U){return;}char value[12];difficulty_text(variant,value);copy_ascii(value,text,capacity);}

void FlappyBirdGame::apply_result(const GameResult& value,bool* new_record,bool* new_unlock){
    bool record=false;if(value.variant<FlappyBirdRules::DifficultyCount&&value.score>high_scores_[value.variant]){high_scores_[value.variant]=value.score;record=true;}
    if(new_record!=NULL){*new_record=record;}if(new_unlock!=NULL){*new_unlock=false;}
}

void FlappyBirdGame::start_new(uint8_t variant){
    FlappyBirdRules::start(&state_,variant);last_update_ms_=HAL_GetTick();full_dirty_=true;status_dirty_=true;draw_requested_=false;game_over_pending_=false;prompt_dirty_=false;
}

void FlappyBirdGame::handle_input(const InputEvent& input){
    if(state_.phase==FlappyBirdPhase::GameOver||!Input_is_pressed(input,InputAction::Confirm)){return;}
    bool was_ready=state_.phase==FlappyBirdPhase::Ready;FlappyBirdRules::flap(&state_);
    if(was_ready){last_update_ms_=HAL_GetTick();state_.accumulator_ms=0U;draw_requested_=true;prompt_dirty_=true;}
    UiSound_play(UiSoundId::Turn);
}

GameSignal FlappyBirdGame::update(uint32_t now_ms){
    if(game_over_pending_){game_over_pending_=false;draw_requested_=false;return GameSignal::GameOver;}
    if(state_.phase==FlappyBirdPhase::Ready){last_update_ms_=now_ms;if(draw_requested_){draw_requested_=false;return GameSignal::NeedsDraw;}return GameSignal::None;}
    if(state_.phase==FlappyBirdPhase::GameOver){return GameSignal::GameOver;}
    uint32_t elapsed=now_ms-last_update_ms_;last_update_ms_=now_ms;
    uint32_t maximum=(uint32_t)FlappyBirdRules::StepMs*FlappyBirdRules::MaxCatchUpSteps;if(elapsed>maximum){elapsed=maximum;}
    state_.accumulator_ms=(uint16_t)(state_.accumulator_ms+elapsed);uint8_t steps=0U;bool any_step=false;
    while(state_.accumulator_ms>=FlappyBirdRules::StepMs&&steps<FlappyBirdRules::MaxCatchUpSteps){
        state_.accumulator_ms=(uint16_t)(state_.accumulator_ms-FlappyBirdRules::StepMs);bool scored=false;any_step=true;steps++;
        if(!FlappyBirdRules::step(&state_,&scored)){game_over_pending_=true;break;}
        if(scored){status_dirty_=true;}
    }
    if(game_over_pending_){game_over_pending_=false;return GameSignal::GameOver;}
    if(any_step||draw_requested_){draw_requested_=false;return GameSignal::NeedsDraw;}
    return GameSignal::None;
}

int16_t FlappyBirdGame::pixel_position(int32_t value_q8){return (int16_t)(value_q8>=0?(value_q8+128)/256:-((-value_q8+128)/256));}

void FlappyBirdGame::difficulty_text(uint8_t difficulty,char* text){
    static const char* labels[FlappyBirdRules::DifficultyCount]={"Easy","Normal","Hard","Ultra"};const char* label=labels[difficulty<FlappyBirdRules::DifficultyCount?difficulty:1U];uint8_t i=0U;
    while(label[i]!='\0'){text[i]=label[i];i++;}text[i]='\0';
}

void FlappyBirdGame::draw_bird(Ui& ui,int16_t y) const{
    static const uint8_t mask[5]={0x18U,0x7CU,0xFEU,0x7FU,0x1CU};
    for(uint8_t row=0U;row<5U;row++){for(uint8_t column=0U;column<7U;column++){if((mask[row]&(uint8_t)(1U<<(6U-column)))!=0U){uint16_t color=column==6U?UI_COLOR_GOLD:(row==1U&&column==4U?UI_COLOR_TEXT:UI_COLOR_FOCUS);fill_clipped(ui,(int16_t)(FlappyBirdRules::BirdX+column*2U),(int16_t)(y+row*2U),2,2,color);}}}
}

void FlappyBirdGame::draw_pipe_slice(Ui& ui,uint8_t index,int16_t x,uint16_t width) const{
    if(index>=FlappyBirdRules::PipeCount||width==0U){return;}const FlappyBirdPipeState& pipe=state_.pipes[index];const FlappyBirdDifficulty& config=FlappyBirdRules::difficulty(state_.difficulty);
    fill_clipped(ui,x,0,(int16_t)width,(int16_t)pipe.gap_top,UI_COLOR_PRIMARY_DARK);
    int16_t bottom=(int16_t)(pipe.gap_top+config.gap_height);fill_clipped(ui,x,bottom,(int16_t)width,(int16_t)(FlappyBirdRules::PlayHeight-bottom),UI_COLOR_PRIMARY_DARK);
}

void FlappyBirdGame::draw_full_pipe(Ui& ui,uint8_t index,int16_t x) const{draw_pipe_slice(ui,index,x,FlappyBirdRules::PipeWidth);}

void FlappyBirdGame::restore_pipes_in_rect(Ui& ui,int16_t x,int16_t y,uint16_t width,uint16_t height) const{
    int16_t right=(int16_t)(x+width),bottom=(int16_t)(y+height);const FlappyBirdDifficulty& config=FlappyBirdRules::difficulty(state_.difficulty);
    for(uint8_t i=0U;i<FlappyBirdRules::PipeCount;i++){
        int16_t pipe_x=pixel_position(state_.pipes[i].x_q8),pipe_right=(int16_t)(pipe_x+FlappyBirdRules::PipeWidth);int16_t left=x>pipe_x?x:pipe_x;int16_t clipped_right=right<pipe_right?right:pipe_right;
        if(clipped_right<=left){continue;}int16_t gap_top=(int16_t)state_.pipes[i].gap_top,gap_bottom=(int16_t)(gap_top+config.gap_height);
        int16_t top_bottom=bottom<gap_top?bottom:gap_top;if(top_bottom>y){fill_clipped(ui,left,y,(int16_t)(clipped_right-left),(int16_t)(top_bottom-y),UI_COLOR_PRIMARY_DARK);}
        int16_t lower_top=y>gap_bottom?y:gap_bottom;if(bottom>lower_top){fill_clipped(ui,left,lower_top,(int16_t)(clipped_right-left),(int16_t)(bottom-lower_top),UI_COLOR_PRIMARY_DARK);}
    }
}

void FlappyBirdGame::draw_status(Ui& ui) const{
    char value[12];ui.text(246U,8U,UiTextId::Difficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);difficulty_text(state_.difficulty,value);ui.fill(244U,30U,74U,28U,UI_COLOR_PANEL);ui.ascii_fit(246U,36U,70U,value,UI_COLOR_PRIMARY,UI_COLOR_PANEL);
    ui.text(246U,76U,UiTextId::Score,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);format_u32(state_.score,value);ui.fill(244U,98U,74U,28U,UI_COLOR_PANEL);ui.ascii_fit(246U,104U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(246U,144U,UiTextId::HighScore,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);uint32_t high=high_scores_[state_.difficulty]>state_.score?high_scores_[state_.difficulty]:state_.score;format_u32(high,value);ui.fill(244U,166U,74U,28U,UI_COLOR_PANEL);ui.ascii_fit(246U,172U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
}

void FlappyBirdGame::draw(Ui& ui,bool full_redraw){
    int16_t bird_y=pixel_position(state_.bird_y_q8);
    if(full_redraw||full_dirty_){
        ui.fill(0U,0U,240U,240U,UI_COLOR_BACKGROUND);ui.fill(240U,0U,80U,240U,UI_COLOR_PANEL);ui.fill(239U,0U,1U,240U,UI_COLOR_LINE);
        for(uint8_t i=0U;i<FlappyBirdRules::PipeCount;i++){int16_t x=pixel_position(state_.pipes[i].x_q8);draw_full_pipe(ui,i,x);drawn_pipe_x_[i]=x;}
        draw_bird(ui,bird_y);drawn_bird_y_=bird_y;draw_status(ui);
        if(state_.phase==FlappyBirdPhase::Ready){ui.text(82U,110U,UiTextId::PressYToFly,UI_COLOR_TEXT,UI_COLOR_BACKGROUND,16U);}
        full_dirty_=false;status_dirty_=false;prompt_dirty_=false;return;
    }
    fill_clipped(ui,(int16_t)FlappyBirdRules::BirdX,drawn_bird_y_,FlappyBirdRules::BirdWidth,FlappyBirdRules::BirdHeight,UI_COLOR_BACKGROUND);
    for(uint8_t i=0U;i<FlappyBirdRules::PipeCount;i++){
        int16_t old_x=drawn_pipe_x_[i],new_x=pixel_position(state_.pipes[i].x_q8);
        if(new_x<=old_x&&(int16_t)(old_x-new_x)<=5){
            int16_t moved=(int16_t)(old_x-new_x);if(moved>0){fill_clipped(ui,(int16_t)(new_x+FlappyBirdRules::PipeWidth),0,moved,FlappyBirdRules::PlayHeight,UI_COLOR_BACKGROUND);draw_pipe_slice(ui,i,new_x,(uint16_t)moved);}
        }
        else if(new_x!=old_x){fill_clipped(ui,old_x,0,FlappyBirdRules::PipeWidth,FlappyBirdRules::PlayHeight,UI_COLOR_BACKGROUND);draw_full_pipe(ui,i,new_x);}
        drawn_pipe_x_[i]=new_x;
    }
    restore_pipes_in_rect(ui,(int16_t)FlappyBirdRules::BirdX,drawn_bird_y_,FlappyBirdRules::BirdWidth,FlappyBirdRules::BirdHeight);
    if(prompt_dirty_){fill_clipped(ui,80,108,80,20,UI_COLOR_BACKGROUND);prompt_dirty_=false;}
    draw_bird(ui,bird_y);drawn_bird_y_=bird_y;if(status_dirty_){draw_status(ui);status_dirty_=false;}
}

uint16_t FlappyBirdGame::make_snapshot(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(FlappyBirdSnapshot)||!FlappyBirdRules::make_snapshot(state_,&snapshot_work)){return 0U;}const uint8_t* source=(const uint8_t*)&snapshot_work;
    for(uint16_t i=0U;i<sizeof(snapshot_work);i++){data[i]=source[i];}return sizeof(snapshot_work);
}

bool FlappyBirdGame::restore(const uint8_t* data,uint16_t length){
    if(data==NULL||length!=sizeof(FlappyBirdSnapshot)){return false;}uint8_t* target=(uint8_t*)&snapshot_work;for(uint16_t i=0U;i<sizeof(snapshot_work);i++){target[i]=data[i];}
    FlappyBirdState restored;if(!FlappyBirdRules::restore_snapshot(snapshot_work,&restored)){return false;}state_=restored;last_update_ms_=HAL_GetTick();full_dirty_=true;status_dirty_=true;draw_requested_=false;game_over_pending_=false;prompt_dirty_=false;return true;
}

GameResult FlappyBirdGame::result() const{return {state_.score,0U,0U,state_.difficulty,0U,state_.phase==FlappyBirdPhase::GameOver?GameOutcome::Failure:GameOutcome::InProgress,UiTextId::Score,UiTextId::Score,1U};}
UiTextId FlappyBirdGame::display_name() const{return UiTextId::FlappyBird;}
UiTextId FlappyBirdGame::menu_goal_text() const{return UiTextId::GoalFlappy;}
void FlappyBirdGame::format_menu_mode(char* text,uint8_t capacity) const{if(text==NULL||capacity==0U){return;}char value[12];difficulty_text(state_.difficulty,value);copy_ascii(value,text,capacity);}
uint8_t FlappyBirdGame::menu_control_count() const{return 1U;}
UiTextId FlappyBirdGame::menu_control_text(uint8_t) const{return UiTextId::ControlFlappyFly;}

void FlappyBirdGame::draw_home_thumbnail(Ui& ui,uint16_t x,uint16_t y,uint16_t width,uint16_t height) const{
    if(width<64U||height<64U){return;}uint16_t center_y=(uint16_t)(y+height/2U);ui.fill((uint16_t)(x+8U),center_y,18U,8U,UI_COLOR_FOCUS);ui.fill((uint16_t)(x+22U),(uint16_t)(center_y+2U),8U,4U,UI_COLOR_GOLD);
    ui.fill((uint16_t)(x+width-24U),(uint16_t)(y+6U),14U,20U,UI_COLOR_PRIMARY_DARK);ui.fill((uint16_t)(x+width-24U),(uint16_t)(y+height-26U),14U,20U,UI_COLOR_PRIMARY_DARK);
}

void FlappyBirdGame::draw_entry_page(Ui& ui,const GameActivity& activity,const GameOverview& overview) const{
    char value[20];ui.fill(16U,38U,288U,126U,UI_COLOR_PANEL);Lcd_draw_rect(16U,38U,288U,126U,UI_COLOR_LINE);ui.fill(27U,49U,88U,88U,UI_COLOR_BACKGROUND);Lcd_draw_rect(27U,49U,88U,88U,UI_COLOR_LINE);
    ui.fill(40U,91U,24U,10U,UI_COLOR_FOCUS);ui.fill(60U,94U,9U,4U,UI_COLOR_GOLD);ui.fill(88U,57U,14U,25U,UI_COLOR_PRIMARY_DARK);ui.fill(88U,105U,14U,25U,UI_COLOR_PRIMARY_DARK);
    ui.text(132U,45U,UiTextId::FlappyBird,UI_COLOR_PRIMARY,UI_COLOR_PANEL,24U);ui.text(132U,78U,UiTextId::Difficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);difficulty_text(overview.last_variant,value);ui.ascii_fit(224U,78U,68U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(132U,101U,UiTextId::HighScore,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);format_u32(overview.high_score,value);ui.ascii_fit(224U,101U,68U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(28U,143U,UiTextId::RecentPlay,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);format_activity(activity,value);ui.ascii_fit(100U,143U,166U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
}

void FlappyBirdGame::draw_entry_control(Ui& ui,uint8_t control,bool selected,bool snapshot_available) const{
    if(control==0U){ui.large_button(16U,180U,140U,snapshot_available?UiTextId::ContinueGame:UiTextId::StartGame,selected);}else if(control==1U){ui.large_button(164U,180U,140U,UiTextId::AdjustDifficulty,selected);}
}

bool FlappyBirdGame::variant_unlocked(const GameProgressView&,uint8_t variant) const{return variant<FlappyBirdRules::DifficultyCount;}

void FlappyBirdGame::draw_variant_select(Ui& ui,uint8_t selected,uint8_t previous,const GameProgressView&,bool page_redraw) const{
    auto draw_choice=[&ui](uint8_t variant,bool active){
        uint16_t x=(uint16_t)(64U+variant*64U),background=active?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL,border=active?UI_COLOR_FOCUS:UI_COLOR_LINE;
        char value[10];difficulty_text(variant,value);
        ui.fill((uint16_t)(x-25U),98U,50U,48U,background);
        Lcd_draw_rect((uint16_t)(x-25U),98U,50U,48U,border);
        uint16_t text_width=Ui::text_width(value,16U);
        ui.ascii((uint16_t)(x-text_width/2U),112U,value,active?UI_COLOR_FOCUS:UI_COLOR_TEXT,background,16U);
    };
    if(page_redraw){
        ui.fill(16U,38U,288U,164U,UI_COLOR_PANEL);
        Lcd_draw_rect(16U,38U,288U,164U,UI_COLOR_LINE);
        ui.text(112U,52U,UiTextId::AdjustDifficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,24U);
        ui.lr_hint(25U,114U,270U,UI_COLOR_PANEL);
        for(uint8_t i=0U;i<FlappyBirdRules::DifficultyCount;i++){
            draw_choice(i,i==selected);
        }
        ui.text(104U,176U,UiTextId::PromptSaveCancel,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    }
    else{
        draw_choice(previous,false);draw_choice(selected,true);
    }
}

GameSelectSignal FlappyBirdGame::handle_variant_select(const InputEvent& input,uint8_t* variant) const{
    if(variant==NULL||*variant>=FlappyBirdRules::DifficultyCount){return GameSelectSignal::None;}
    if(Input_is_pressed(input,InputAction::Action1)){*variant=*variant==0U?(FlappyBirdRules::DifficultyCount-1U):(uint8_t)(*variant-1U);return GameSelectSignal::SelectionChanged;}
    if(Input_is_pressed(input,InputAction::Action2)){*variant=(uint8_t)((*variant+1U)%FlappyBirdRules::DifficultyCount);return GameSelectSignal::SelectionChanged;}
    if(Input_is_pressed(input,InputAction::Confirm)){return GameSelectSignal::Confirmed;}return GameSelectSignal::None;
}
