#include "tetris_game.hpp"
#include "basic.h"
#include "save.hpp"
#include "ui.hpp"
#include "ui_sound.hpp"

#define TETRIS_DATA_VERSION 1U

namespace {

#pragma pack(push,1)
struct TetrisPersistentData {uint32_t high_score[TetrisRules::DifficultyCount];};
#pragma pack(pop)

static_assert(sizeof(TetrisPersistentData)==12U,"Tetris persistent format changed");
static_assert(sizeof(TetrisPersistentData)<=SAVE_GAME_DATA_CAPACITY,"Tetris data exceeds game slot");
static_assert(sizeof(TetrisSnapshot)<=SAVE_SNAPSHOT_CAPACITY,"Tetris snapshot exceeds save capacity");

static TetrisSnapshot snapshot_work;

}

TetrisGame::TetrisGame():last_update_ms_(0U),drawn_score_(0U),drawn_lines_(0U),drawn_next_piece_(TetrisPiece::I),drawn_piece_count_(0U),drawn_level_(0U),full_dirty_(true),draw_requested_(false),game_over_pending_(false){
    for(uint8_t y=0U;y<TetrisRules::Height;y++){for(uint8_t x=0U;x<TetrisRules::Width;x++){state_.board[y][x]=0U;drawn_board_[y][x]=0xFFU;}}
    state_.score=0U;state_.lines=0U;state_.fall_elapsed_ms=0U;state_.lock_elapsed_ms=0U;state_.piece_x=7;state_.piece_y=0;
    state_.current_piece=TetrisPiece::I;state_.next_piece=TetrisPiece::O;state_.rotation=0U;state_.difficulty=default_variant();state_.lock_resets=0U;state_.grounded=0U;state_.game_over=0U;
    for(uint8_t i=0U;i<4U;i++){drawn_piece_cells_[i]=-1;}Input_repeat_reset(&horizontal_repeat_);Input_repeat_reset(&soft_drop_repeat_);persistent_defaults();
}

GameId TetrisGame::id() const{return GameId::Tetris;}
const char* TetrisGame::title() const{return "Tetris";}
uint8_t TetrisGame::variant_count() const{return TetrisRules::DifficultyCount;}
uint8_t TetrisGame::default_variant() const{return 0U;}
const char* TetrisGame::variant_page_title() const{return "Difficulty";}
UiTextId TetrisGame::variant_action_text() const{return UiTextId::AdjustDifficulty;}
uint16_t TetrisGame::persistent_data_version() const{return TETRIS_DATA_VERSION;}

void TetrisGame::persistent_defaults(){for(uint8_t i=0U;i<TetrisRules::DifficultyCount;i++){high_scores_[i]=0U;}}

bool TetrisGame::persistent_data_valid(const uint8_t* data,uint16_t length,uint16_t version) const{return data!=NULL&&length==sizeof(TetrisPersistentData)&&version==TETRIS_DATA_VERSION;}

void TetrisGame::load_persistent_data(const uint8_t* data,uint16_t length){
    if(!persistent_data_valid(data,length,TETRIS_DATA_VERSION)){return;}
    for(uint8_t i=0U;i<TetrisRules::DifficultyCount;i++){uint8_t offset=(uint8_t)(i*4U);high_scores_[i]=(uint32_t)data[offset]|((uint32_t)data[offset+1U]<<8)|((uint32_t)data[offset+2U]<<16)|((uint32_t)data[offset+3U]<<24);}
}

uint16_t TetrisGame::make_persistent_data(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(TetrisPersistentData)){return 0U;}
    for(uint8_t i=0U;i<TetrisRules::DifficultyCount;i++){uint8_t offset=(uint8_t)(i*4U);uint32_t value=high_scores_[i];data[offset]=(uint8_t)value;data[offset+1U]=(uint8_t)(value>>8);data[offset+2U]=(uint8_t)(value>>16);data[offset+3U]=(uint8_t)(value>>24);}
    return sizeof(TetrisPersistentData);
}

GameOverview TetrisGame::persistent_overview(uint8_t selected_variant) const{
    uint8_t variant=selected_variant<TetrisRules::DifficultyCount?selected_variant:default_variant();return {high_scores_[variant],0U,TetrisRules::DifficultyCount,TetrisRules::DifficultyCount,variant,{0U,0U,0U}};
}

GameRecordSummary TetrisGame::record_summary(const uint8_t* data,uint16_t length,uint16_t version) const{
    GameRecordSummary summary={0U,0U,0U,0U,0U};if(!persistent_data_valid(data,length,version)){return summary;}
    const TetrisPersistentData& value=*(const TetrisPersistentData*)data;
    for(uint8_t i=0U;i<TetrisRules::DifficultyCount;i++){if(value.high_score[i]>summary.value){summary.value=value.high_score[i];summary.variant=i;}}
    summary.valid=summary.value!=0U?1U:0U;return summary;
}

void TetrisGame::format_record_variant(uint8_t variant,char* text,uint8_t capacity) const{if(text==NULL||capacity==0U){return;}char value[12];difficulty_text(variant,value);copy_ascii(value,text,capacity);}

void TetrisGame::apply_result(const GameResult& value,bool* new_record,bool* new_unlock){
    bool record=false;if(value.variant<TetrisRules::DifficultyCount&&value.score>high_scores_[value.variant]){high_scores_[value.variant]=value.score;record=true;}
    if(new_record!=NULL){*new_record=record;}if(new_unlock!=NULL){*new_unlock=false;}
}

void TetrisGame::start_new(uint8_t variant){
    TetrisRules::start(&state_,variant);last_update_ms_=HAL_GetTick();Input_repeat_reset(&horizontal_repeat_);Input_repeat_reset(&soft_drop_repeat_);
    full_dirty_=true;draw_requested_=false;game_over_pending_=false;drawn_piece_count_=0U;
}

void TetrisGame::process_event(const TetrisEvent& event,bool hard_drop){
    if(event.lines_cleared!=0U){UiSound_play(UiSoundId::Score);}
    else if(event.locked!=0U){UiSound_play(UiSoundId::TetrisLand);}
    else if(hard_drop){UiSound_play(UiSoundId::GameAction);}
    if(event.moved!=0U||event.locked!=0U){draw_requested_=true;}
    if(event.game_over!=0U){game_over_pending_=true;}
}

void TetrisGame::handle_input(const InputEvent& input){
    if(state_.game_over!=0U){Input_repeat_reset(&horizontal_repeat_);Input_repeat_reset(&soft_drop_repeat_);return;}
    uint32_t now=HAL_GetTick();uint16_t horizontal=Input_repeat_update(&horizontal_repeat_,input.state,input.pressed,(uint16_t)(KEY_LEFT|KEY_RIGHT),now,170U,50U);
    if(horizontal==KEY_LEFT){if(TetrisRules::move_horizontal(&state_,-1)){draw_requested_=true;}}
    else if(horizontal==KEY_RIGHT){if(TetrisRules::move_horizontal(&state_,1)){draw_requested_=true;}}
    uint16_t down=Input_repeat_update(&soft_drop_repeat_,input.state,input.pressed,KEY_DOWN,now,40U,40U);
    if(down==KEY_DOWN&&TetrisRules::soft_drop(&state_)){draw_requested_=true;}
    if(Input_is_pressed(input,InputAction::Confirm)&&TetrisRules::rotate_clockwise(&state_)){draw_requested_=true;UiSound_play(UiSoundId::GameAction);}
    if(Input_is_pressed(input,InputAction::Cancel)){
        TetrisEvent event;TetrisRules::hard_drop(&state_,&event);last_update_ms_=now;process_event(event,true);
    }
}

GameSignal TetrisGame::update(uint32_t now_ms){
    if(game_over_pending_){game_over_pending_=false;draw_requested_=false;return GameSignal::GameOver;}
    if(state_.game_over!=0U){return GameSignal::GameOver;}
    uint32_t elapsed=now_ms-last_update_ms_;last_update_ms_=now_ms;TetrisEvent event;TetrisRules::advance(&state_,elapsed,&event);process_event(event,false);
    if(game_over_pending_){game_over_pending_=false;draw_requested_=false;return GameSignal::GameOver;}
    if(draw_requested_){draw_requested_=false;return GameSignal::NeedsDraw;}return GameSignal::None;
}

uint16_t TetrisGame::piece_color(uint8_t value){
    static const uint16_t colors[TetrisRules::PieceCount]={0x07FFU,0xFFE0U,0xF81FU,0x07E0U,0xF800U,0x001FU,0xFD20U};
    return value>=1U&&value<=TetrisRules::PieceCount?colors[value-1U]:UI_COLOR_BACKGROUND;
}

uint8_t TetrisGame::collect_piece_cells(const TetrisState& state,int16_t* cells){
    if(cells==NULL){return 0U;}uint8_t count=0U;
    for(uint8_t y=0U;y<4U;y++){for(uint8_t x=0U;x<4U;x++){
        if(!TetrisRules::shape_cell(state.current_piece,state.rotation,x,y)){continue;}int16_t board_x=(int16_t)state.piece_x+x,board_y=(int16_t)state.piece_y+y;
        if(board_x>=0&&board_x<TetrisRules::Width&&board_y>=0&&board_y<TetrisRules::Height&&count<4U){cells[count++]=(int16_t)(board_y*TetrisRules::Width+board_x);}
    }}
    return count;
}

void TetrisGame::draw_board_cell(Ui& ui,uint8_t x,uint8_t y) const{
    uint16_t pixel_x=(uint16_t)x*10U,pixel_y=(uint16_t)y*10U;ui.fill(pixel_x,pixel_y,10U,10U,UI_COLOR_BACKGROUND);
    uint8_t value=state_.board[y][x];if(value!=0U){ui.fill((uint16_t)(pixel_x+1U),(uint16_t)(pixel_y+1U),8U,8U,piece_color(value));}
}

void TetrisGame::draw_active_piece(Ui& ui){
    int16_t cells[4];uint8_t count=collect_piece_cells(state_,cells);uint16_t color=piece_color((uint8_t)state_.current_piece+1U);
    for(uint8_t i=0U;i<count;i++){uint8_t x=(uint8_t)(cells[i]%TetrisRules::Width),y=(uint8_t)(cells[i]/TetrisRules::Width);ui.fill((uint16_t)(x*10U+1U),(uint16_t)(y*10U+1U),8U,8U,color);drawn_piece_cells_[i]=cells[i];}
    for(uint8_t i=count;i<4U;i++){drawn_piece_cells_[i]=-1;}drawn_piece_count_=count;
}

void TetrisGame::draw_next_piece(Ui& ui) const{
    ui.fill(218U,28U,72U,50U,UI_COLOR_PANEL);int8_t min_x=4,max_x=-1,min_y=4,max_y=-1;
    for(uint8_t y=0U;y<4U;y++){for(uint8_t x=0U;x<4U;x++){if(TetrisRules::shape_cell(state_.next_piece,0U,x,y)){if((int8_t)x<min_x){min_x=(int8_t)x;}if((int8_t)x>max_x){max_x=(int8_t)x;}if((int8_t)y<min_y){min_y=(int8_t)y;}if((int8_t)y>max_y){max_y=(int8_t)y;}}}}
    uint8_t width=(uint8_t)(max_x-min_x+1),height=(uint8_t)(max_y-min_y+1);uint16_t start_x=(uint16_t)(254U-(uint16_t)width*9U/2U),start_y=(uint16_t)(53U-(uint16_t)height*9U/2U),color=piece_color((uint8_t)state_.next_piece+1U);
    for(uint8_t y=0U;y<4U;y++){for(uint8_t x=0U;x<4U;x++){if(TetrisRules::shape_cell(state_.next_piece,0U,x,y)){ui.fill((uint16_t)(start_x+(x-(uint8_t)min_x)*9U),(uint16_t)(start_y+(y-(uint8_t)min_y)*9U),8U,8U,color);}}}
}

void TetrisGame::draw_status(Ui& ui,bool full_redraw){
    char value[12];uint8_t current_level=TetrisRules::level(state_);
    if(full_redraw){ui.text(188U,7U,UiTextId::NextPiece,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);ui.text(188U,84U,UiTextId::Score,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);ui.ascii(188U,136U,"Level",UI_COLOR_MUTED,UI_COLOR_PANEL,16U);ui.text(188U,188U,UiTextId::ClearedLines,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);}
    if(full_redraw||drawn_next_piece_!=state_.next_piece){draw_next_piece(ui);drawn_next_piece_=state_.next_piece;}
    if(full_redraw||drawn_score_!=state_.score){ui.fill(188U,105U,124U,24U,UI_COLOR_PANEL);format_u32(state_.score,value);ui.ascii_fit(188U,109U,124U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);drawn_score_=state_.score;}
    if(full_redraw||drawn_level_!=current_level){ui.fill(188U,157U,124U,24U,UI_COLOR_PANEL);format_u32(current_level,value);ui.ascii_fit(188U,161U,124U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);drawn_level_=current_level;}
    if(full_redraw||drawn_lines_!=state_.lines){ui.fill(188U,209U,124U,24U,UI_COLOR_PANEL);format_u32(state_.lines,value);ui.ascii_fit(188U,213U,124U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);drawn_lines_=state_.lines;}
}

void TetrisGame::draw(Ui& ui,bool full_redraw){
    if(full_redraw||full_dirty_){
        ui.fill(0U,0U,180U,240U,UI_COLOR_BACKGROUND);ui.fill(180U,0U,140U,240U,UI_COLOR_PANEL);ui.fill(180U,0U,1U,240U,UI_COLOR_LINE);
        for(uint8_t y=0U;y<TetrisRules::Height;y++){for(uint8_t x=0U;x<TetrisRules::Width;x++){draw_board_cell(ui,x,y);drawn_board_[y][x]=state_.board[y][x];}}
        drawn_piece_count_=0U;draw_active_piece(ui);draw_status(ui,true);full_dirty_=false;return;
    }
    for(uint8_t i=0U;i<drawn_piece_count_;i++){
        if(drawn_piece_cells_[i]<0){continue;}uint8_t x=(uint8_t)(drawn_piece_cells_[i]%TetrisRules::Width),y=(uint8_t)(drawn_piece_cells_[i]/TetrisRules::Width);draw_board_cell(ui,x,y);
    }
    for(uint8_t y=0U;y<TetrisRules::Height;y++){for(uint8_t x=0U;x<TetrisRules::Width;x++){if(drawn_board_[y][x]!=state_.board[y][x]){draw_board_cell(ui,x,y);drawn_board_[y][x]=state_.board[y][x];}}}
    draw_active_piece(ui);draw_status(ui,false);
}

uint16_t TetrisGame::make_snapshot(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(TetrisSnapshot)||!TetrisRules::make_snapshot(state_,&snapshot_work)){return 0U;}const uint8_t* source=(const uint8_t*)&snapshot_work;
    for(uint16_t i=0U;i<sizeof(snapshot_work);i++){data[i]=source[i];}return sizeof(snapshot_work);
}

bool TetrisGame::restore(const uint8_t* data,uint16_t length){
    if(data==NULL||length!=sizeof(TetrisSnapshot)){return false;}uint8_t* target=(uint8_t*)&snapshot_work;for(uint16_t i=0U;i<sizeof(snapshot_work);i++){target[i]=data[i];}
    if(!TetrisRules::restore_snapshot(snapshot_work,&state_)){return false;}last_update_ms_=HAL_GetTick();Input_repeat_reset(&horizontal_repeat_);Input_repeat_reset(&soft_drop_repeat_);
    full_dirty_=true;draw_requested_=false;game_over_pending_=false;drawn_piece_count_=0U;return true;
}

GameResult TetrisGame::result() const{return {state_.score,state_.lines,0U,state_.difficulty,0U,state_.game_over!=0U?GameOutcome::Failure:GameOutcome::InProgress,UiTextId::Score,UiTextId::ClearedLines,0U};}
UiTextId TetrisGame::display_name() const{return UiTextId::Tetris;}
UiTextId TetrisGame::menu_goal_text() const{return UiTextId::GoalTetris;}
void TetrisGame::format_menu_mode(char* text,uint8_t capacity) const{if(text==NULL||capacity==0U){return;}char value[12];difficulty_text(state_.difficulty,value);copy_ascii(value,text,capacity);}
uint8_t TetrisGame::menu_control_count() const{return 4U;}
UiTextId TetrisGame::menu_control_text(uint8_t index) const{static const UiTextId controls[4]={UiTextId::ControlTetrisMove,UiTextId::ControlTetrisDrop,UiTextId::ControlTetrisRotate,UiTextId::ControlTetrisHardDrop};return controls[index<4U?index:0U];}

void TetrisGame::draw_thumbnail_block(Ui& ui,uint16_t x,uint16_t y,uint8_t size,uint16_t color){
    if(size<4U){ui.fill(x,y,size,size,color);return;}
    ui.fill((uint16_t)(x+1U),(uint16_t)(y+1U),(uint16_t)(size-1U),(uint16_t)(size-1U),UI_COLOR_LINE);
    ui.fill(x,y,(uint16_t)(size-2U),(uint16_t)(size-2U),color);
    ui.fill((uint16_t)(x+1U),(uint16_t)(y+1U),(uint16_t)(size-4U),1U,UI_COLOR_TEXT);
}

void TetrisGame::draw_thumbnail_piece(Ui& ui,uint16_t x,uint16_t y,uint8_t cell_size,TetrisPiece piece,uint8_t rotation){
    uint16_t color=piece_color((uint8_t)piece+1U);
    for(uint8_t row=0U;row<4U;row++){for(uint8_t column=0U;column<4U;column++){
        if(TetrisRules::shape_cell(piece,rotation,column,row)){draw_thumbnail_block(ui,(uint16_t)(x+column*cell_size),(uint16_t)(y+row*cell_size),(uint8_t)(cell_size-1U),color);}
    }}
}

void TetrisGame::draw_home_thumbnail(Ui& ui,uint16_t x,uint16_t y,uint16_t width,uint16_t height) const{
    if(width<64U||height<80U){return;}
    draw_thumbnail_piece(ui,(uint16_t)(x+1U),(uint16_t)(y+15U),7U,TetrisPiece::I,1U);
    draw_thumbnail_piece(ui,(uint16_t)(x+24U),(uint16_t)(y+16U),9U,TetrisPiece::T,0U);
    draw_thumbnail_piece(ui,(uint16_t)(x+37U),(uint16_t)(y+40U),9U,TetrisPiece::L,1U);
    draw_thumbnail_piece(ui,(uint16_t)(x+4U),(uint16_t)(y+66U),9U,TetrisPiece::S,0U);
    draw_thumbnail_piece(ui,(uint16_t)(x+43U),(uint16_t)(y+68U),8U,TetrisPiece::J,2U);
    ui.fill((uint16_t)(x+71U),(uint16_t)(y+14U),3U,9U,UI_COLOR_GOLD);ui.fill((uint16_t)(x+68U),(uint16_t)(y+17U),9U,3U,UI_COLOR_GOLD);
    ui.fill((uint16_t)(x+66U),(uint16_t)(y+34U),3U,3U,UI_COLOR_FOCUS);
}

void TetrisGame::difficulty_text(uint8_t difficulty,char* text){
    static const char* labels[TetrisRules::DifficultyCount]={"Easy","Normal","Hard"};const char* label=labels[difficulty<TetrisRules::DifficultyCount?difficulty:0U];uint8_t i=0U;
    while(label[i]!='\0'){text[i]=label[i];i++;}text[i]='\0';
}

void TetrisGame::draw_entry_page(Ui& ui,const GameActivity& activity,const GameOverview& overview) const{
    char value[20];ui.fill(16U,38U,288U,126U,UI_COLOR_PANEL);Lcd_draw_rect(16U,38U,288U,126U,UI_COLOR_LINE);ui.fill(27U,49U,88U,88U,UI_COLOR_BACKGROUND);Lcd_draw_rect(27U,49U,88U,88U,UI_COLOR_LINE);
    draw_thumbnail_piece(ui,35U,54U,7U,TetrisPiece::I,1U);draw_thumbnail_piece(ui,57U,55U,8U,TetrisPiece::T,0U);
    draw_thumbnail_piece(ui,36U,102U,8U,TetrisPiece::S,0U);draw_thumbnail_piece(ui,59U,93U,8U,TetrisPiece::L,1U);draw_thumbnail_piece(ui,70U,108U,7U,TetrisPiece::J,2U);
    ui.fill(101U,59U,3U,9U,UI_COLOR_GOLD);ui.fill(98U,62U,9U,3U,UI_COLOR_GOLD);ui.fill(43U,87U,3U,3U,UI_COLOR_FOCUS);
    ui.text(132U,45U,UiTextId::Tetris,UI_COLOR_PRIMARY,UI_COLOR_PANEL,24U);ui.text(132U,78U,UiTextId::Difficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);difficulty_text(overview.last_variant,value);ui.ascii_fit(224U,78U,68U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(132U,101U,UiTextId::HighScore,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);format_u32(overview.high_score,value);ui.ascii_fit(224U,101U,68U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(28U,143U,UiTextId::RecentPlay,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);format_activity(activity,value);ui.ascii_fit(100U,143U,166U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
}

void TetrisGame::draw_entry_control(Ui& ui,uint8_t control,bool selected,bool snapshot_available) const{
    if(control==0U){ui.large_button(16U,180U,140U,snapshot_available?UiTextId::ContinueGame:UiTextId::StartGame,selected);}else if(control==1U){ui.large_button(164U,180U,140U,UiTextId::AdjustDifficulty,selected);}
}

bool TetrisGame::variant_unlocked(const GameProgressView&,uint8_t variant) const{return variant<TetrisRules::DifficultyCount;}

void TetrisGame::draw_variant_select(Ui& ui,uint8_t selected,uint8_t previous,const GameProgressView&,bool page_redraw) const{
    auto draw_choice=[&ui](uint8_t variant,bool active){
        uint16_t x=(uint16_t)(96U+variant*64U),background=active?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL,border=active?UI_COLOR_FOCUS:UI_COLOR_LINE;char value[8];difficulty_text(variant,value);
        ui.fill((uint16_t)(x-26U),98U,52U,48U,background);Lcd_draw_rect((uint16_t)(x-26U),98U,52U,48U,border);uint16_t width=Ui::text_width(value,16U);ui.ascii((uint16_t)(x-width/2U),112U,value,active?UI_COLOR_FOCUS:UI_COLOR_TEXT,background,16U);
    };
    if(page_redraw){
        ui.fill(16U,38U,288U,164U,UI_COLOR_PANEL);Lcd_draw_rect(16U,38U,288U,164U,UI_COLOR_LINE);ui.text(112U,52U,UiTextId::AdjustDifficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,24U);ui.lr_hint(25U,114U,270U,UI_COLOR_PANEL);
        for(uint8_t i=0U;i<TetrisRules::DifficultyCount;i++){draw_choice(i,i==selected);}ui.text(104U,176U,UiTextId::PromptSaveCancel,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    }
    else{draw_choice(previous,false);draw_choice(selected,true);}
}

GameSelectSignal TetrisGame::handle_variant_select(const InputEvent& input,uint8_t* variant) const{
    if(variant==NULL||*variant>=TetrisRules::DifficultyCount){return GameSelectSignal::None;}
    if(Input_is_pressed(input,InputAction::Action1)){*variant=*variant==0U?(TetrisRules::DifficultyCount-1U):(uint8_t)(*variant-1U);return GameSelectSignal::SelectionChanged;}
    if(Input_is_pressed(input,InputAction::Action2)){*variant=(uint8_t)((*variant+1U)%TetrisRules::DifficultyCount);return GameSelectSignal::SelectionChanged;}
    if(Input_is_pressed(input,InputAction::Confirm)){return GameSelectSignal::Confirmed;}return GameSelectSignal::None;
}
