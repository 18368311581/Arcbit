#include "gobang_game.hpp"
#include "gobang_ai.hpp"
#include "save.hpp"
#include "ui.hpp"
#include "ui_sound.hpp"

#define GOBANG_SNAPSHOT_MAGIC   0x474F4D31UL
#define GOBANG_SNAPSHOT_VERSION 3U
#define GOBANG_DATA_VERSION     3U

namespace {

static constexpr uint16_t BoardColor=0xD5A5U;
static constexpr uint16_t GridColor=0x4228U;
static constexpr uint16_t BlackStone=0x0000U;
static constexpr uint16_t WhiteStone=0xFFFFU;
static constexpr uint16_t WhiteOutline=0x7BEFU;
static constexpr uint16_t AiLastMoveColor=0xF800U;

#pragma pack(push,1)
struct GobangSnapshot {
    uint32_t magic;
    uint16_t version;
    uint8_t role;
    uint8_t phase;
    uint8_t outcome;
    uint8_t selected_cell;
    uint8_t undo_used;
    uint8_t undo_focus;
    uint8_t history_count;
    uint8_t last_ai_cell;
    uint8_t board[GobangGame::CellCount];
    uint8_t history[GobangGame::CellCount];
};
#pragma pack(pop)

struct GobangPersistentData {
    uint16_t fastest_total_moves[GobangGame::RoleCount];
};

static_assert(sizeof(GobangSnapshot)<=SAVE_SNAPSHOT_CAPACITY,"Gomoku snapshot exceeds save capacity");
static_assert(sizeof(GobangSnapshot)==464U,"Gomoku snapshot format changed");
static_assert(sizeof(GobangPersistentData)==4U,"Gomoku data format changed");
static_assert(sizeof(GobangPersistentData)<=SAVE_GAME_DATA_CAPACITY,"Gomoku data exceeds game slot");
static_assert(sizeof(GobangGame)<=768U,"Gomoku game object exceeds RAM budget");

}

GobangGame::GobangGame():repeat_{0U,0U},fastest_total_moves_{0U,0U},history_count_(0U),selected_cell_(112U),role_(0U),phase_(Phase::InitialDraw),outcome_(GameOutcome::InProgress),dirty_count_(0U),undo_focus_(0U),last_ai_cell_(0xFFU),undo_used_(false),full_dirty_(true),status_dirty_(true),modal_dirty_(false),dialog_restore_dirty_(false),draw_requested_(false){
    for(uint16_t i=0U;i<CellCount;i++){board_[i]=0U;history_[i]=0xFFU;}
    for(uint8_t i=0U;i<sizeof(dirty_);i++){dirty_[i]=0U;}
}

GameId GobangGame::id() const{return GameId::Gomoku;}
const char* GobangGame::title() const{return "Gomoku";}
uint8_t GobangGame::variant_count() const{return RoleCount;}
uint8_t GobangGame::default_variant() const{return 0U;}
const char* GobangGame::variant_page_title() const{return "Player Order";}
UiTextId GobangGame::variant_action_text() const{return UiTextId::AdjustOrder;}
uint16_t GobangGame::persistent_data_version() const{return GOBANG_DATA_VERSION;}
uint8_t GobangGame::row_of(uint8_t cell){return (uint8_t)(cell/BoardSize);}
uint8_t GobangGame::column_of(uint8_t cell){return (uint8_t)(cell%BoardSize);}
uint8_t GobangGame::make_cell(uint8_t row,uint8_t column){return (uint8_t)(row*BoardSize+column);}
uint8_t GobangGame::history_stone(uint8_t role,uint8_t move_index){return ((move_index&1U)==role)?1U:2U;}
bool GobangGame::star_point(uint8_t row,uint8_t column){return (row==3U||row==11U)&&(column==3U||column==11U)||(row==7U&&column==7U);}

void GobangGame::persistent_defaults(){for(uint8_t role=0U;role<RoleCount;role++){fastest_total_moves_[role]=0U;}}

bool GobangGame::persistent_data_valid(const uint8_t* data,uint16_t length,uint16_t version) const{
    if(data==NULL||length!=sizeof(GobangPersistentData)||version!=GOBANG_DATA_VERSION){return false;}
    const GobangPersistentData* value=(const GobangPersistentData*)data;
    for(uint8_t role=0U;role<RoleCount;role++){if(value->fastest_total_moves[role]>CellCount){return false;}}
    return true;
}

void GobangGame::load_persistent_data(const uint8_t* data,uint16_t length){
    if(!persistent_data_valid(data,length,GOBANG_DATA_VERSION)){return;}
    const GobangPersistentData* value=(const GobangPersistentData*)data;
    for(uint8_t role=0U;role<RoleCount;role++){fastest_total_moves_[role]=value->fastest_total_moves[role];}
}

uint16_t GobangGame::make_persistent_data(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(GobangPersistentData)){return 0U;}
    GobangPersistentData* value=(GobangPersistentData*)data;
    for(uint8_t role=0U;role<RoleCount;role++){value->fastest_total_moves[role]=fastest_total_moves_[role];}
    return sizeof(GobangPersistentData);
}

GameOverview GobangGame::persistent_overview(uint8_t selected_variant) const{
    uint8_t role=selected_variant<RoleCount?selected_variant:default_variant();
    return {fastest_total_moves_[role],0U,RoleCount,RoleCount,role,{0U,0U,0U}};
}

GameRecordSummary GobangGame::record_summary(const uint8_t* data,uint16_t length,uint16_t version) const{
    GameRecordSummary summary={0U,0U,0U,1U,0U};if(!persistent_data_valid(data,length,version)){return summary;}
    const GobangPersistentData& value=*(const GobangPersistentData*)data;
    for(uint8_t i=0U;i<RoleCount;i++){uint16_t moves=value.fastest_total_moves[i];if(moves!=0U&&(summary.valid==0U||moves<summary.value)){summary.value=moves;summary.variant=i;summary.valid=1U;}}
    return summary;
}

void GobangGame::format_record_variant(uint8_t variant,char* text,uint8_t capacity) const{copy_ascii(variant==0U?"BLACK":"WHITE",text,capacity);}

void GobangGame::apply_result(const GameResult& value,bool* new_record,bool* new_unlock){
    bool record=false;
    uint8_t role=value.variant<RoleCount?value.variant:default_variant();
    if(value.outcome==GameOutcome::PlayerVictory&&!undo_used_&&value.score!=0U&&value.score<=CellCount&&(fastest_total_moves_[role]==0U||value.score<fastest_total_moves_[role])){
        fastest_total_moves_[role]=(uint16_t)value.score;record=true;
    }
    if(new_record!=NULL){*new_record=record;}if(new_unlock!=NULL){*new_unlock=false;}
}

void GobangGame::start_new(uint8_t variant){
    role_=variant<RoleCount?variant:default_variant();history_count_=0U;selected_cell_=112U;Input_repeat_reset(&repeat_);outcome_=GameOutcome::InProgress;
    phase_=Phase::InitialDraw;dirty_count_=0U;undo_focus_=0U;last_ai_cell_=0xFFU;undo_used_=false;full_dirty_=true;status_dirty_=true;modal_dirty_=false;dialog_restore_dirty_=false;draw_requested_=false;
    for(uint16_t i=0U;i<CellCount;i++){board_[i]=0U;history_[i]=0xFFU;}
}

void GobangGame::add_dirty(uint8_t cell){
    if(cell>=CellCount){return;}for(uint8_t i=0U;i<dirty_count_;i++){if(dirty_[i]==cell){return;}}
    if(dirty_count_<sizeof(dirty_)){dirty_[dirty_count_++]=cell;}else{full_dirty_=true;}
}

void GobangGame::move_selection(int8_t row_delta,int8_t column_delta){
    int16_t row=(int16_t)row_of(selected_cell_)+row_delta,column=(int16_t)column_of(selected_cell_)+column_delta;
    if(row<0||row>=BoardSize||column<0||column>=BoardSize){return;}
    uint8_t previous=selected_cell_;selected_cell_=make_cell((uint8_t)row,(uint8_t)column);add_dirty(previous);add_dirty(selected_cell_);draw_requested_=true;UiSound_play(UiSoundId::Move);
}

bool GobangGame::can_undo() const{
    return phase_==Phase::HumanTurn&&history_count_>=2U&&board_[history_[history_count_-2U]]==1U&&board_[history_[history_count_-1U]]==2U;
}

uint8_t GobangGame::latest_ai_cell() const{
    for(uint8_t i=history_count_;i>0U;i--){uint8_t cell=history_[i-1U];if(cell<CellCount&&board_[cell]==2U){return cell;}}
    return 0xFFU;
}

void GobangGame::undo_pair(){
    if(!can_undo()){return;}
    uint8_t old_selected=selected_cell_,player_cell=history_[history_count_-2U],ai_cell=history_[history_count_-1U];
    board_[player_cell]=0U;board_[ai_cell]=0U;history_[history_count_-2U]=0xFFU;history_[history_count_-1U]=0xFFU;history_count_=(uint8_t)(history_count_-2U);
    last_ai_cell_=latest_ai_cell();
    selected_cell_=player_cell;outcome_=GameOutcome::InProgress;phase_=Phase::HumanTurn;undo_used_=true;undo_focus_=0U;
    add_dirty(old_selected);add_dirty(player_cell);add_dirty(ai_cell);add_dirty(last_ai_cell_);status_dirty_=true;draw_requested_=true;UiSound_play(UiSoundId::Back);
}

bool GobangGame::board_full() const{return history_count_>=CellCount;}

void GobangGame::perform_ai(){
    GobangAiMove move={0U,0U};bool valid=GobangAi_choose(board_,&move);
    if(!valid){outcome_=GameOutcome::Draw;phase_=Phase::AiMoveDraw;status_dirty_=true;return;}
    uint8_t target=make_cell(move.row,move.column);
    if(target>=CellCount||board_[target]!=0U){outcome_=GameOutcome::Draw;phase_=Phase::AiMoveDraw;status_dirty_=true;return;}
    add_dirty(last_ai_cell_);board_[target]=2U;history_[history_count_++]=target;last_ai_cell_=target;add_dirty(target);
    if(GobangAi_has_five(board_,move.row,move.column,2U)){outcome_=GameOutcome::AiVictory;}
    else if(board_full()){outcome_=GameOutcome::Draw;}
    phase_=Phase::AiMoveDraw;status_dirty_=true;UiSound_play(UiSoundId::Confirm);
}

void GobangGame::handle_input(const InputEvent& input){
    if(phase_==Phase::UndoConfirm){
        Input_repeat_reset(&repeat_);
        if(Input_is_pressed(input,InputAction::Cancel)){
            phase_=Phase::HumanTurn;undo_focus_=0U;dialog_restore_dirty_=true;draw_requested_=true;UiSound_play(UiSoundId::Back);return;
        }
        if(Input_is_pressed(input,InputAction::Up)||Input_is_pressed(input,InputAction::Down)||Input_is_pressed(input,InputAction::Left)||Input_is_pressed(input,InputAction::Right)){
            undo_focus_=undo_focus_==0U?1U:0U;modal_dirty_=true;draw_requested_=true;UiSound_play(UiSoundId::Move);return;
        }
        if(Input_is_pressed(input,InputAction::Confirm)){
            dialog_restore_dirty_=true;
            if(undo_focus_==0U){phase_=Phase::HumanTurn;undo_focus_=0U;draw_requested_=true;UiSound_play(UiSoundId::Back);}
            else{phase_=Phase::HumanTurn;undo_pair();}
        }
        return;
    }
    if(phase_!=Phase::HumanTurn){Input_repeat_reset(&repeat_);return;}
    uint16_t repeated=Input_repeat_update(&repeat_,input.state,input.pressed,(uint16_t)(KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT),HAL_GetTick(),300U,85U);
    if(repeated==KEY_UP){move_selection(-1,0);return;}
    if(repeated==KEY_DOWN){move_selection(1,0);return;}
    if(repeated==KEY_LEFT){move_selection(0,-1);return;}
    if(repeated==KEY_RIGHT){move_selection(0,1);return;}
    if(Input_is_pressed(input,InputAction::Action3)){
        if(!can_undo()){UiSound_play(UiSoundId::Error);return;}
        if(!undo_used_){phase_=Phase::UndoConfirm;undo_focus_=0U;modal_dirty_=true;draw_requested_=true;UiSound_play(UiSoundId::Confirm);}
        else{undo_pair();}
        return;
    }
    if(!Input_is_pressed(input,InputAction::Confirm)){return;}
    if(board_[selected_cell_]!=0U){UiSound_play(UiSoundId::Error);return;}
    board_[selected_cell_]=1U;history_[history_count_++]=selected_cell_;add_dirty(selected_cell_);status_dirty_=true;
    if(GobangAi_has_five(board_,row_of(selected_cell_),column_of(selected_cell_),1U)){outcome_=GameOutcome::PlayerVictory;}
    else if(board_full()){outcome_=GameOutcome::Draw;}
    phase_=Phase::HumanMoveDraw;UiSound_play(UiSoundId::GameAction);
}

GameSignal GobangGame::update(uint32_t now_ms){
    (void)now_ms;
    if(draw_requested_){draw_requested_=false;return GameSignal::NeedsDraw;}
    if(phase_==Phase::InitialDraw){phase_=role_==0U?Phase::HumanTurn:Phase::AiPending;status_dirty_=true;return GameSignal::NeedsDraw;}
    if(phase_==Phase::HumanMoveDraw){phase_=outcome_==GameOutcome::InProgress?Phase::AiPending:Phase::TerminalPending;status_dirty_=true;return GameSignal::NeedsDraw;}
    if(phase_==Phase::AiPending){perform_ai();return GameSignal::NeedsDraw;}
    if(phase_==Phase::AiMoveDraw){phase_=outcome_==GameOutcome::InProgress?Phase::HumanTurn:Phase::TerminalPending;return GameSignal::None;}
    if(phase_==Phase::TerminalPending){return GameSignal::GameOver;}
    return GameSignal::None;
}

UiTextId GobangGame::turn_text() const{
    if(phase_==Phase::InitialDraw){return role_==0U?UiTextId::PlayerTurn:UiTextId::AiTurn;}
    if(phase_==Phase::HumanMoveDraw||phase_==Phase::AiPending){return UiTextId::AiTurn;}
    return UiTextId::PlayerTurn;
}

uint16_t GobangGame::stone_color(uint8_t stone) const{
    bool player_black=role_==0U;bool black=(stone==1U&&player_black)||(stone==2U&&!player_black);return black?BlackStone:WhiteStone;
}

void GobangGame::draw_corner_marker(Ui& ui,int16_t x,int16_t y,uint16_t color){
    ui.fill((uint16_t)(x-7),(uint16_t)(y-7),4U,2U,color);ui.fill((uint16_t)(x-7),(uint16_t)(y-7),2U,4U,color);
    ui.fill((uint16_t)(x+4),(uint16_t)(y-7),4U,2U,color);ui.fill((uint16_t)(x+6),(uint16_t)(y-7),2U,4U,color);
    ui.fill((uint16_t)(x-7),(uint16_t)(y+6),4U,2U,color);ui.fill((uint16_t)(x-7),(uint16_t)(y+4),2U,4U,color);
    ui.fill((uint16_t)(x+4),(uint16_t)(y+6),4U,2U,color);ui.fill((uint16_t)(x+6),(uint16_t)(y+4),2U,4U,color);
}

void GobangGame::draw_stone(Ui& ui,uint8_t cell_value) const{
    uint8_t stone=board_[cell_value];if(stone==0U){return;}
    int16_t x=(int16_t)(15U+column_of(cell_value)*15U),y=(int16_t)(15U+row_of(cell_value)*15U);uint16_t color=stone_color(stone);
    ui.filled_circle(x,y,6U,color);Lcd_draw_circle(x,y,6U,color==WhiteStone?WhiteOutline:BlackStone);
    if(cell_value==last_ai_cell_){ui.filled_circle(x,y,2U,AiLastMoveColor);}
}

void GobangGame::draw_intersection(Ui& ui,uint8_t cell_value,bool selected) const{
    uint8_t row=row_of(cell_value),column=column_of(cell_value);uint16_t x=(uint16_t)(15U+column*15U),y=(uint16_t)(15U+row*15U);
    ui.fill((uint16_t)(x-7U),(uint16_t)(y-7U),15U,15U,BoardColor);
    uint16_t left=x>15U?(uint16_t)(x-7U):15U,right=x<225U?(uint16_t)(x+7U):225U;
    uint16_t top=y>15U?(uint16_t)(y-7U):15U,bottom=y<225U?(uint16_t)(y+7U):225U;
    Lcd_draw_line((int16_t)left,(int16_t)y,(int16_t)right,(int16_t)y,GridColor);Lcd_draw_line((int16_t)x,(int16_t)top,(int16_t)x,(int16_t)bottom,GridColor);
    if(star_point(row,column)){ui.filled_circle((int16_t)x,(int16_t)y,2U,GridColor);}
    draw_stone(ui,cell_value);if(selected){draw_corner_marker(ui,(int16_t)x,(int16_t)y,UI_COLOR_FOCUS);}
}

void GobangGame::draw_board(Ui& ui) const{
    ui.fill(0U,0U,240U,240U,BoardColor);
    for(uint8_t line=0U;line<BoardSize;line++){
        int16_t position=(int16_t)(15U+line*15U);Lcd_draw_line(15,position,225,position,GridColor);Lcd_draw_line(position,15,position,225,GridColor);
    }
    static const uint8_t stars[5][2]={{3U,3U},{3U,11U},{7U,7U},{11U,3U},{11U,11U}};
    for(uint8_t i=0U;i<5U;i++){ui.filled_circle((int16_t)(15U+stars[i][1]*15U),(int16_t)(15U+stars[i][0]*15U),2U,GridColor);}
    for(uint16_t cell_value=0U;cell_value<CellCount;cell_value++){if(board_[cell_value]!=0U){draw_stone(ui,(uint8_t)cell_value);}}
    draw_corner_marker(ui,(int16_t)(15U+column_of(selected_cell_)*15U),(int16_t)(15U+row_of(selected_cell_)*15U),UI_COLOR_FOCUS);
}

void GobangGame::draw_status(Ui& ui,bool full) const{
    char value[12];
    if(full){
        ui.fill(240U,0U,80U,240U,UI_COLOR_PANEL);ui.fill(239U,0U,1U,240U,UI_COLOR_LINE);
        ui.text(246U,8U,UiTextId::PlayerRole,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
        ui.text(246U,56U,UiTextId::CurrentTurn,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
        ui.text(246U,104U,UiTextId::TotalMoves,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    }
    ui.fill(244U,28U,74U,20U,UI_COLOR_PANEL);ui.text(246U,30U,role_==0U?UiTextId::BlackFirst:UiTextId::WhiteSecond,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    ui.fill(244U,76U,74U,20U,UI_COLOR_PANEL);ui.text(246U,78U,turn_text(),UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    ui.fill(244U,124U,74U,20U,UI_COLOR_PANEL);format_u32(history_count_,value);ui.ascii_fit(246U,126U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
}

void GobangGame::restore_dialog_area(Ui& ui) const{
    ui.fill(30U,42U,210U,156U,BoardColor);
    for(uint8_t line=0U;line<BoardSize;line++){
        int16_t position=(int16_t)(15U+line*15U);
        if(position>=42&&position<=197){Lcd_draw_line(30,position,225,position,GridColor);}
        if(position>=30&&position<=225){Lcd_draw_line(position,42,position,197,GridColor);}
    }
    static const uint8_t stars[5][2]={{3U,3U},{3U,11U},{7U,7U},{11U,3U},{11U,11U}};
    for(uint8_t i=0U;i<5U;i++){
        int16_t x=(int16_t)(15U+stars[i][1]*15U),y=(int16_t)(15U+stars[i][0]*15U);if(x>=30&&y>=42&&y<=197){ui.filled_circle(x,y,2U,GridColor);}
    }
    for(uint16_t cell_value=0U;cell_value<CellCount;cell_value++){
        int16_t x=(int16_t)(15U+column_of((uint8_t)cell_value)*15U),y=(int16_t)(15U+row_of((uint8_t)cell_value)*15U);
        if(x>=30&&y>=42&&y<=197&&board_[cell_value]!=0U){draw_stone(ui,(uint8_t)cell_value);}
    }
    int16_t selected_x=(int16_t)(15U+column_of(selected_cell_)*15U),selected_y=(int16_t)(15U+row_of(selected_cell_)*15U);
    if(selected_x>=30&&selected_y>=42&&selected_y<=197){draw_corner_marker(ui,selected_x,selected_y,UI_COLOR_FOCUS);}
    draw_status(ui,true);
}

void GobangGame::draw(Ui& ui,bool full_redraw){
    if(full_redraw||full_dirty_){draw_board(ui);draw_status(ui,true);full_dirty_=false;dirty_count_=0U;status_dirty_=false;dialog_restore_dirty_=false;}
    else{
        if(dialog_restore_dirty_){restore_dialog_area(ui);dialog_restore_dirty_=false;status_dirty_=false;}
        for(uint8_t i=0U;i<dirty_count_;i++){draw_intersection(ui,dirty_[i],dirty_[i]==selected_cell_);}dirty_count_=0U;
        if(status_dirty_){draw_status(ui,false);status_dirty_=false;}
    }
    if(phase_==Phase::UndoConfirm&&(full_redraw||modal_dirty_)){ui.dialog(UiTextId::UndoRecordWarning,undo_focus_);modal_dirty_=false;}
}

uint16_t GobangGame::make_snapshot(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(GobangSnapshot)){return 0U;}
    GobangSnapshot* snapshot=(GobangSnapshot*)data;snapshot->magic=GOBANG_SNAPSHOT_MAGIC;snapshot->version=GOBANG_SNAPSHOT_VERSION;snapshot->role=role_;
    snapshot->phase=(uint8_t)phase_;snapshot->outcome=(uint8_t)outcome_;snapshot->selected_cell=selected_cell_;snapshot->undo_used=undo_used_?1U:0U;
    snapshot->undo_focus=undo_focus_;snapshot->history_count=history_count_;snapshot->last_ai_cell=last_ai_cell_;
    for(uint16_t i=0U;i<CellCount;i++){snapshot->board[i]=board_[i];snapshot->history[i]=i<history_count_?history_[i]:0xFFU;}
    return sizeof(GobangSnapshot);
}

bool GobangGame::restore(const uint8_t* data,uint16_t length){
    if(data==NULL||length!=sizeof(GobangSnapshot)){return false;}
    const GobangSnapshot* snapshot=(const GobangSnapshot*)data;
    if(snapshot->magic!=GOBANG_SNAPSHOT_MAGIC||snapshot->version!=GOBANG_SNAPSHOT_VERSION||snapshot->role>=RoleCount||snapshot->phase>=(uint8_t)Phase::Count||snapshot->outcome>(uint8_t)GameOutcome::Draw||snapshot->selected_cell>=CellCount||snapshot->undo_used>1U||snapshot->undo_focus>1U||snapshot->history_count>CellCount){return false;}
    Phase phase=(Phase)snapshot->phase;GameOutcome outcome=(GameOutcome)snapshot->outcome;
    if(outcome==GameOutcome::LevelComplete||outcome==GameOutcome::Failure){return false;}
    if(phase==Phase::InitialDraw&&(snapshot->history_count!=0U||outcome!=GameOutcome::InProgress)){return false;}
    if((phase==Phase::HumanTurn||phase==Phase::AiPending||phase==Phase::UndoConfirm)&&outcome!=GameOutcome::InProgress){return false;}
    if(phase==Phase::TerminalPending&&outcome==GameOutcome::InProgress){return false;}
    if(phase==Phase::HumanMoveDraw&&outcome!=GameOutcome::InProgress&&outcome!=GameOutcome::PlayerVictory&&outcome!=GameOutcome::Draw){return false;}
    if(phase==Phase::AiMoveDraw&&outcome!=GameOutcome::InProgress&&outcome!=GameOutcome::AiVictory&&outcome!=GameOutcome::Draw){return false;}
    if(phase==Phase::UndoConfirm&&(snapshot->undo_used!=0U||snapshot->history_count<2U)){return false;}
    for(uint16_t i=0U;i<CellCount;i++){if(snapshot->board[i]>2U){return false;}}
    for(uint16_t i=0U;i<snapshot->history_count;i++){
        uint8_t cell_value=snapshot->history[i];if(cell_value>=CellCount||snapshot->board[cell_value]!=history_stone(snapshot->role,(uint8_t)i)){return false;}
        for(uint16_t j=0U;j<i;j++){if(snapshot->history[j]==cell_value){return false;}}
    }
    for(uint16_t cell_value=0U;cell_value<CellCount;cell_value++){
        uint8_t expected=0U;for(uint16_t i=0U;i<snapshot->history_count;i++){if(snapshot->history[i]==cell_value){expected=history_stone(snapshot->role,(uint8_t)i);break;}}
        if(snapshot->board[cell_value]!=expected){return false;}
    }
    bool player_win=false,ai_win=false;
    for(uint16_t cell_value=0U;cell_value<CellCount;cell_value++){
        uint8_t stone=snapshot->board[cell_value];if(stone==0U){continue;}
        if(GobangAi_has_five(snapshot->board,row_of((uint8_t)cell_value),column_of((uint8_t)cell_value),stone)){
            if(stone==1U){player_win=true;}else{ai_win=true;}
        }
    }
    if(outcome==GameOutcome::InProgress&&(player_win||ai_win)){return false;}
    if(outcome==GameOutcome::PlayerVictory&&(!player_win||ai_win)){return false;}
    if(outcome==GameOutcome::AiVictory&&(!ai_win||player_win)){return false;}
    if(outcome==GameOutcome::Draw&&(snapshot->history_count!=CellCount||player_win||ai_win)){return false;}
    bool human_next=(snapshot->history_count&1U)==snapshot->role;
    if(phase==Phase::HumanTurn&& !human_next){return false;}
    if((phase==Phase::HumanMoveDraw||phase==Phase::AiPending)&&human_next){return false;}
    if((phase==Phase::AiMoveDraw)&&!human_next){return false;}
    if((phase==Phase::HumanTurn||phase==Phase::AiPending)&&snapshot->history_count>=CellCount){return false;}
    if(phase==Phase::UndoConfirm){
        uint8_t player_cell=snapshot->history[snapshot->history_count-2U],ai_cell=snapshot->history[snapshot->history_count-1U];
        if(snapshot->board[player_cell]!=1U||snapshot->board[ai_cell]!=2U||!human_next){return false;}
    }
    uint8_t expected_last_ai=0xFFU;
    for(uint8_t i=snapshot->history_count;i>0U;i--){uint8_t cell=snapshot->history[i-1U];if(snapshot->board[cell]==2U){expected_last_ai=cell;break;}}
    if(snapshot->last_ai_cell!=expected_last_ai){return false;}
    role_=snapshot->role;phase_=phase;outcome_=outcome;selected_cell_=snapshot->selected_cell;undo_used_=snapshot->undo_used!=0U;undo_focus_=snapshot->undo_focus;
    history_count_=snapshot->history_count;Input_repeat_reset(&repeat_);
    for(uint16_t i=0U;i<CellCount;i++){board_[i]=snapshot->board[i];history_[i]=snapshot->history[i];}
    last_ai_cell_=snapshot->last_ai_cell;
    dirty_count_=0U;full_dirty_=true;status_dirty_=true;modal_dirty_=phase_==Phase::UndoConfirm;dialog_restore_dirty_=false;draw_requested_=false;return true;
}

GameResult GobangGame::result() const{
    return {history_count_,history_count_,0U,role_,0U,outcome_,UiTextId::TotalMoves,UiTextId::TotalMoves,0U};
}

UiTextId GobangGame::display_name() const{return UiTextId::Gomoku;}
UiTextId GobangGame::menu_goal_text() const{return UiTextId::GoalGomoku;}
void GobangGame::format_menu_mode(char* text,uint8_t capacity) const{copy_ascii(role_==0U?"BLACK FIRST":"WHITE SECOND",text,capacity);}
uint8_t GobangGame::menu_control_count() const{return 3U;}
UiTextId GobangGame::menu_control_text(uint8_t index) const{static const UiTextId controls[3]={UiTextId::ControlGomokuSelect,UiTextId::ControlGomokuPlace,UiTextId::ControlGomokuUndo};return controls[index<3U?index:0U];}

void GobangGame::draw_home_thumbnail(Ui& ui,uint16_t x,uint16_t y,uint16_t width,uint16_t height) const{
    if(width<68U||height<68U){return;}int16_t cx=(int16_t)(x+width/2U),cy=(int16_t)(y+height/2U);
    for(int8_t line=-3;line<=3;line++){Lcd_draw_line(cx-30,cy+line*10,cx+30,cy+line*10,UI_COLOR_LINE);Lcd_draw_line(cx+line*10,cy-30,cx+line*10,cy+30,UI_COLOR_LINE);}
    static const int8_t stones[8][3]={{-2,-2,1},{-1,-1,1},{0,0,1},{1,1,1},{-2,1,2},{-1,1,2},{0,1,2},{2,-1,2}};
    for(uint8_t i=0U;i<8U;i++){
        int16_t sx=(int16_t)(cx+stones[i][0]*10),sy=(int16_t)(cy+stones[i][1]*10);uint16_t color=stones[i][2]==1?BlackStone:WhiteStone;
        ui.filled_circle(sx,sy,4U,color);if(color==WhiteStone){Lcd_draw_circle(sx,sy,4U,WhiteOutline);}
    }
}

void GobangGame::draw_entry_page(Ui& ui,const GameActivity& activity,const GameOverview& overview) const{
    char value[20];ui.fill(16U,38U,288U,126U,UI_COLOR_PANEL);
    Lcd_draw_rect(16U,38U,288U,126U,UI_COLOR_LINE);
    ui.fill(27U,49U,88U,88U,BoardColor);
    Lcd_draw_rect(27U,49U,88U,88U,UI_COLOR_LINE);
    for(uint8_t i=0U;i<5U;i++){
        uint16_t position=(uint16_t)(43U+i*14U);
        Lcd_draw_line(43,position+22,99,position+22,GridColor);
        Lcd_draw_line(position,65,position,121,GridColor);
    }
    ui.filled_circle(57,79,5U,BlackStone);
    ui.filled_circle(71,93,5U,WhiteStone);
    Lcd_draw_circle(71,93,5U,WhiteOutline);
    ui.text(132U,45U,UiTextId::Gomoku,UI_COLOR_PRIMARY,UI_COLOR_PANEL,24U);
    ui.text(132U,78U,UiTextId::PlayerRole,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    ui.text(224U,78U,overview.last_variant==0U?UiTextId::BlackFirst:UiTextId::WhiteSecond,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    ui.text(132U,101U,UiTextId::FastestWin,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    if(overview.high_score==0U){value[0]='-';value[1]='-';value[2]='\0';}
    else{format_u32(overview.high_score,value);}
    ui.ascii_fit(248U,101U,44U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(28U,143U,UiTextId::RecentPlay,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    format_activity(activity,value);
    ui.ascii_fit(100U,143U,166U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
}

void GobangGame::draw_entry_control(Ui& ui,uint8_t control,bool selected,bool snapshot_available) const{
    if(control==0U){ui.large_button(16U,180U,140U,snapshot_available?UiTextId::ContinueGame:UiTextId::StartGame,selected);}
    else if(control==1U){ui.large_button(164U,180U,140U,UiTextId::AdjustOrder,selected);}
}

bool GobangGame::variant_unlocked(const GameProgressView&,uint8_t variant) const{return variant<RoleCount;}

void GobangGame::draw_variant_select(Ui& ui,uint8_t selected,uint8_t previous,const GameProgressView&,bool page_redraw) const{
    auto draw_choice=[&ui](uint8_t role,bool active){
        int16_t x=role==0U?100:220,y=112;uint16_t stone=role==0U?BlackStone:WhiteStone;
        ui.filled_circle(x,y,18U,stone);Lcd_draw_circle(x,y,18U,stone==WhiteStone?WhiteOutline:BlackStone);
        uint16_t marker=active?UI_COLOR_FOCUS:UI_COLOR_PANEL;
        ui.fill((uint16_t)(x-24),(uint16_t)(y-24),8U,2U,marker);ui.fill((uint16_t)(x-24),(uint16_t)(y-24),2U,8U,marker);
        ui.fill((uint16_t)(x+17),(uint16_t)(y-24),8U,2U,marker);ui.fill((uint16_t)(x+23),(uint16_t)(y-24),2U,8U,marker);
        ui.fill((uint16_t)(x-24),(uint16_t)(y+23),8U,2U,marker);ui.fill((uint16_t)(x-24),(uint16_t)(y+17),2U,8U,marker);
        ui.fill((uint16_t)(x+17),(uint16_t)(y+23),8U,2U,marker);ui.fill((uint16_t)(x+23),(uint16_t)(y+17),2U,8U,marker);
        ui.text((uint16_t)(x-32),(uint16_t)(y+30),role==0U?UiTextId::BlackFirst:UiTextId::WhiteSecond,active?UI_COLOR_FOCUS:UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    };
    if(page_redraw){
        ui.fill(24U,38U,272U,164U,UI_COLOR_PANEL);Lcd_draw_rect(24U,38U,272U,164U,UI_COLOR_LINE);
        ui.text(96U,52U,UiTextId::AdjustOrder,UI_COLOR_MUTED,UI_COLOR_PANEL,24U);ui.lr_hint(48U,104U,216U,UI_COLOR_PANEL);
        draw_choice(0U,selected==0U);draw_choice(1U,selected==1U);ui.text(104U,176U,UiTextId::PromptSaveCancel,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    }
    else{draw_choice(previous,false);draw_choice(selected,true);}
}

GameSelectSignal GobangGame::handle_variant_select(const InputEvent& input,uint8_t* variant) const{
    if(variant==NULL||*variant>=RoleCount){return GameSelectSignal::None;}
    if(Input_is_pressed(input,InputAction::Action1)||Input_is_pressed(input,InputAction::Action2)){*variant=*variant==0U?1U:0U;return GameSelectSignal::SelectionChanged;}
    if(Input_is_pressed(input,InputAction::Confirm)){return GameSelectSignal::Confirmed;}
    return GameSelectSignal::None;
}
