#include "game_2048.hpp"
#include "lcd.h"
#include "save.hpp"
#include "ui.hpp"
#include "ui_sound.hpp"

#define GAME_2048_DATA_VERSION 1U

namespace {

#pragma pack(push,1)
struct Game2048PersistentData {
    uint32_t high_score[Game2048Rules::VariantCount];
};
#pragma pack(pop)

static_assert(sizeof(Game2048PersistentData)==12U,"2048 persistent format changed");
static_assert(sizeof(Game2048PersistentData)<=SAVE_GAME_DATA_CAPACITY,"2048 data exceeds game slot");
static_assert(sizeof(Game2048Snapshot)<=SAVE_SNAPSHOT_CAPACITY,"2048 snapshot exceeds save capacity");

static Game2048Snapshot snapshot_work;

static void size_text(uint8_t size_index,char* text){
    uint8_t size=Game2048Rules::board_size(size_index);
    text[0]=(char)('0'+size);text[1]='x';text[2]=(char)('0'+size);text[3]='\0';
}

static void tile_text(uint8_t exponent,char* text){
    if(exponent<32U){Game::format_u32(Game2048Rules::tile_value(exponent),text);return;}
    text[0]='2';text[1]='^';
    if(exponent>=10U){text[2]=(char)('0'+exponent/10U);text[3]=(char)('0'+exponent%10U);text[4]='\0';}
    else{text[2]=(char)('0'+exponent);text[3]='\0';}
}

static uint16_t tile_color(uint8_t exponent){
    if(exponent==0U){return UI_COLOR_PANEL;}
    if(exponent<=2U){return UI_COLOR_LINE;}
    if(exponent<=4U){return UI_COLOR_PRIMARY_DARK;}
    if(exponent<=7U){return UI_COLOR_PRIMARY;}
    if(exponent<=9U){return UI_COLOR_GOLD;}
    return exponent<=11U?UI_COLOR_FOCUS:UI_COLOR_DANGER;
}

static void draw_rounded_card(Ui& ui,uint16_t x,uint16_t y,uint16_t size,uint16_t color){
    ui.fill((uint16_t)(x+2U),y,(uint16_t)(size-4U),size,color);
    ui.fill(x,(uint16_t)(y+2U),size,(uint16_t)(size-4U),color);
}

}

Game2048::Game2048():dirty_cells_(0U),full_dirty_(true),status_dirty_(true),draw_requested_(false),game_over_pending_(false){
    for(uint8_t i=0U;i<Game2048Rules::MaxCells;i++){state_.cells[i]=0U;}
    state_.score=0U;state_.size_index=0U;state_.running=0U;
    persistent_defaults();
}

GameId Game2048::id() const{return GameId::Game2048;}
const char* Game2048::title() const{return "2048";}
uint8_t Game2048::variant_count() const{return Game2048Rules::VariantCount;}
uint8_t Game2048::default_variant() const{return 0U;}
const char* Game2048::variant_page_title() const{return "Board Size";}
UiTextId Game2048::variant_action_text() const{return UiTextId::AdjustBoardSize;}
uint16_t Game2048::persistent_data_version() const{return GAME_2048_DATA_VERSION;}

void Game2048::persistent_defaults(){for(uint8_t i=0U;i<Game2048Rules::VariantCount;i++){high_scores_[i]=0U;}}

bool Game2048::persistent_data_valid(const uint8_t* data,uint16_t length,uint16_t version) const{
    return data!=NULL&&length==sizeof(Game2048PersistentData)&&version==GAME_2048_DATA_VERSION;
}

void Game2048::load_persistent_data(const uint8_t* data,uint16_t length){
    if(!persistent_data_valid(data,length,GAME_2048_DATA_VERSION)){return;}
    for(uint8_t variant=0U;variant<Game2048Rules::VariantCount;variant++){
        uint8_t offset=(uint8_t)(variant*4U);
        high_scores_[variant]=(uint32_t)data[offset]|((uint32_t)data[offset+1U]<<8)|((uint32_t)data[offset+2U]<<16)|((uint32_t)data[offset+3U]<<24);
    }
}

uint16_t Game2048::make_persistent_data(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(Game2048PersistentData)){return 0U;}
    for(uint8_t variant=0U;variant<Game2048Rules::VariantCount;variant++){
        uint8_t offset=(uint8_t)(variant*4U);uint32_t value=high_scores_[variant];
        data[offset]=(uint8_t)value;data[offset+1U]=(uint8_t)(value>>8);data[offset+2U]=(uint8_t)(value>>16);data[offset+3U]=(uint8_t)(value>>24);
    }
    return sizeof(Game2048PersistentData);
}

GameOverview Game2048::persistent_overview(uint8_t selected_variant) const{
    uint8_t variant=selected_variant<Game2048Rules::VariantCount?selected_variant:default_variant();
    return {high_scores_[variant],0U,Game2048Rules::VariantCount,Game2048Rules::VariantCount,variant,{0U,0U,0U}};
}

GameRecordSummary Game2048::record_summary(const uint8_t* data,uint16_t length,uint16_t version) const{
    GameRecordSummary summary={0U,0U,0U,0U,0U};if(!persistent_data_valid(data,length,version)){return summary;}
    const Game2048PersistentData& value=*(const Game2048PersistentData*)data;
    for(uint8_t i=0U;i<Game2048Rules::VariantCount;i++){if(value.high_score[i]>summary.value){summary.value=value.high_score[i];summary.variant=i;}}
    summary.valid=summary.value!=0U?1U:0U;return summary;
}

void Game2048::format_record_variant(uint8_t variant,char* text,uint8_t capacity) const{if(text==NULL||capacity==0U){return;}char value[8];size_text(variant,value);copy_ascii(value,text,capacity);}

void Game2048::apply_result(const GameResult& value,bool* new_record,bool* new_unlock){
    bool record=Game2048Rules::update_high_score(high_scores_,value.variant,value.score);
    if(new_record!=NULL){*new_record=record;}
    if(new_unlock!=NULL){*new_unlock=false;}
}

void Game2048::start_new(uint8_t variant){
    Game2048Rules::start(&state_,variant);
    dirty_cells_=0U;full_dirty_=true;status_dirty_=true;draw_requested_=false;game_over_pending_=false;
}

void Game2048::handle_input(const InputEvent& input){
    if(state_.running==0U){return;}
    Game2048Direction direction;
    bool found=true;
    if(Input_is_pressed(input,InputAction::Up)){direction=Game2048Direction::Up;}
    else if(Input_is_pressed(input,InputAction::Down)){direction=Game2048Direction::Down;}
    else if(Input_is_pressed(input,InputAction::Left)){direction=Game2048Direction::Left;}
    else if(Input_is_pressed(input,InputAction::Right)){direction=Game2048Direction::Right;}
    else{found=false;direction=Game2048Direction::Up;}
    if(!found){return;}
    uint8_t previous[Game2048Rules::MaxCells];
    for(uint8_t i=0U;i<Game2048Rules::MaxCells;i++){previous[i]=state_.cells[i];}
    Game2048MoveInfo info;
    if(!Game2048Rules::move(&state_,direction,&info)){return;}
    for(uint8_t i=0U;i<Game2048Rules::MaxCells;i++){if(previous[i]!=state_.cells[i]){dirty_cells_|=(1ULL<<i);}}
    status_dirty_=true;draw_requested_=true;game_over_pending_=state_.running==0U;
    if(state_.running!=0U){
        uint8_t tier=Game2048Rules::sound_tier(info.score_gain);
        if(tier==1U){UiSound_play(UiSoundId::Game2048MergeLow);}
        else if(tier==2U){UiSound_play(UiSoundId::Game2048MergeMedium);}
        else if(tier==3U){UiSound_play(UiSoundId::Game2048MergeHigh);}
    }
}

GameSignal Game2048::update(uint32_t){
    if(game_over_pending_){game_over_pending_=false;draw_requested_=false;return GameSignal::GameOver;}
    if(draw_requested_){draw_requested_=false;return GameSignal::NeedsDraw;}
    return GameSignal::None;
}

void Game2048::draw_cell(Ui& ui,uint8_t index) const{
    uint8_t size=Game2048Rules::board_size(state_.size_index);
    uint8_t x=(uint8_t)(index%Game2048Rules::MaxSize),y=(uint8_t)(index/Game2048Rules::MaxSize);
    if(x>=size||y>=size){return;}
    uint8_t cell_size=(uint8_t)(240U/size);
    uint16_t px=(uint16_t)x*cell_size,py=(uint16_t)y*cell_size;
    uint8_t exponent=state_.cells[index];
    uint16_t card_x=(uint16_t)(px+3U),card_y=(uint16_t)(py+3U),card_size=(uint16_t)(cell_size-6U);
    ui.fill(px,py,cell_size,cell_size,UI_COLOR_BACKGROUND);
    if(exponent!=0U){draw_rounded_card(ui,(uint16_t)(card_x+1U),(uint16_t)(card_y+1U),card_size,UI_COLOR_LINE);}
    draw_rounded_card(ui,card_x,card_y,card_size,tile_color(exponent));
    if(exponent==0U){return;}
    char value[12];tile_text(exponent,value);
    uint8_t font_size=Ui::text_width(value,16U)<=(uint16_t)(card_size-6U)?16U:12U;
    uint16_t width=Ui::text_width(value,font_size);
    ui.ascii((uint16_t)(px+(cell_size-width)/2U),(uint16_t)(py+(cell_size-font_size)/2U),value,UI_COLOR_TEXT,tile_color(exponent),font_size);
}

void Game2048::draw_status(Ui& ui) const{
    char value[12];
    ui.text(246U,8U,UiTextId::GridSize,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    size_text(state_.size_index,value);ui.ascii_fit(246U,30U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(246U,70U,UiTextId::Score,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    format_u32(state_.score,value);ui.ascii_fit(246U,92U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(246U,132U,UiTextId::HighScore,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    format_u32(high_scores_[state_.size_index],value);ui.ascii_fit(246U,154U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
}

void Game2048::draw(Ui& ui,bool full_redraw){
    if(full_redraw||full_dirty_){
        ui.fill(0U,0U,240U,240U,UI_COLOR_BACKGROUND);
        ui.fill(240U,0U,80U,240U,UI_COLOR_PANEL);
        ui.fill(239U,0U,1U,240U,UI_COLOR_LINE);
        uint8_t size=Game2048Rules::board_size(state_.size_index);
        for(uint8_t y=0U;y<size;y++){for(uint8_t x=0U;x<size;x++){draw_cell(ui,Game2048Rules::cell_index(x,y));}}
        draw_status(ui);
    }
    else{
        for(uint8_t i=0U;i<Game2048Rules::MaxCells;i++){if((dirty_cells_&(1ULL<<i))!=0U){draw_cell(ui,i);}}
        if(status_dirty_){draw_status(ui);}
    }
    dirty_cells_=0U;full_dirty_=false;status_dirty_=false;
}

uint16_t Game2048::make_snapshot(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(Game2048Snapshot)||!Game2048Rules::make_snapshot(state_,&snapshot_work)){return 0U;}
    const uint8_t* source=(const uint8_t*)&snapshot_work;
    for(uint16_t i=0U;i<sizeof(snapshot_work);i++){data[i]=source[i];}
    return sizeof(snapshot_work);
}

bool Game2048::restore(const uint8_t* data,uint16_t length){
    if(data==NULL||length!=sizeof(Game2048Snapshot)){return false;}
    uint8_t* target=(uint8_t*)&snapshot_work;
    for(uint16_t i=0U;i<sizeof(snapshot_work);i++){target[i]=data[i];}
    Game2048State restored;
    if(!Game2048Rules::restore_snapshot(snapshot_work,&restored)){return false;}
    state_=restored;dirty_cells_=0U;full_dirty_=true;status_dirty_=true;draw_requested_=false;game_over_pending_=false;
    return true;
}

GameResult Game2048::result() const{
    return {state_.score,0U,0U,state_.size_index,0U,state_.running!=0U?GameOutcome::InProgress:GameOutcome::Failure,UiTextId::Score,UiTextId::Score,0U};
}

UiTextId Game2048::display_name() const{return UiTextId::Game2048;}
UiTextId Game2048::menu_goal_text() const{return UiTextId::Goal2048;}
void Game2048::format_menu_mode(char* text,uint8_t capacity) const{if(text==NULL||capacity==0U){return;}char value[5];size_text(state_.size_index,value);copy_ascii(value,text,capacity);}
uint8_t Game2048::menu_control_count() const{return 1U;}
UiTextId Game2048::menu_control_text(uint8_t) const{return UiTextId::Control2048Move;}

void Game2048::draw_home_thumbnail(Ui& ui,uint16_t x,uint16_t y,uint16_t width,uint16_t height) const{
    if(width<64U||height<64U){return;}
    static const uint8_t values[16]={1U,2U,0U,3U,0U,4U,5U,0U,2U,0U,6U,7U,0U,3U,0U,11U};
    uint16_t cell=14U,start_x=(uint16_t)(x+(width-56U)/2U),start_y=(uint16_t)(y+(height-56U)/2U);
    for(uint8_t row=0U;row<4U;row++){
        for(uint8_t column=0U;column<4U;column++){
            uint8_t exponent=values[row*4U+column];
            if(exponent!=0U){ui.fill((uint16_t)(start_x+column*cell+1U),(uint16_t)(start_y+row*cell+1U),12U,12U,tile_color(exponent));}
        }
    }
}

void Game2048::draw_entry_page(Ui& ui,const GameActivity& activity,const GameOverview& overview) const{
    char value[20];
    ui.fill(16U,38U,288U,126U,UI_COLOR_PANEL);Lcd_draw_rect(16U,38U,288U,126U,UI_COLOR_LINE);
    ui.fill(27U,49U,88U,88U,UI_COLOR_BACKGROUND);Lcd_draw_rect(27U,49U,88U,88U,UI_COLOR_LINE);
    static const uint8_t cells[16]={1U,2U,0U,3U,0U,4U,5U,0U,2U,0U,6U,7U,0U,3U,0U,11U};
    for(uint8_t row=0U;row<4U;row++){
        for(uint8_t column=0U;column<4U;column++){
            uint8_t exponent=cells[row*4U+column];
            ui.fill((uint16_t)(34U+column*19U),(uint16_t)(56U+row*19U),17U,17U,exponent==0U?UI_COLOR_PANEL:tile_color(exponent));
        }
    }
    ui.text(132U,45U,UiTextId::Game2048,UI_COLOR_PRIMARY,UI_COLOR_PANEL,24U);
    ui.text(132U,78U,UiTextId::GridSize,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);size_text(overview.last_variant,value);ui.ascii_fit(224U,78U,68U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(132U,101U,UiTextId::HighScore,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);format_u32(overview.high_score,value);ui.ascii_fit(224U,101U,68U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(28U,143U,UiTextId::RecentPlay,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);format_activity(activity,value);ui.ascii_fit(100U,143U,166U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
}

void Game2048::draw_entry_control(Ui& ui,uint8_t control,bool selected,bool snapshot_available) const{
    if(control==0U){ui.large_button(16U,180U,140U,snapshot_available?UiTextId::ContinueGame:UiTextId::StartGame,selected);}
    else if(control==1U){ui.large_button(164U,180U,140U,UiTextId::AdjustBoardSize,selected);}
}

bool Game2048::variant_unlocked(const GameProgressView&,uint8_t variant) const{return variant<Game2048Rules::VariantCount;}

void Game2048::draw_variant_select(Ui& ui,uint8_t selected,uint8_t previous,const GameProgressView&,bool page_redraw) const{
    auto draw_choice=[&ui](uint8_t variant,bool active){
        uint16_t x=(uint16_t)(74U+variant*86U),background=active?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL,border=active?UI_COLOR_FOCUS:UI_COLOR_LINE;
        char value[4];size_text(variant,value);ui.fill((uint16_t)(x-30U),98U,60U,48U,background);Lcd_draw_rect((uint16_t)(x-30U),98U,60U,48U,border);
        uint16_t width=Ui::text_width(value,16U);ui.ascii((uint16_t)(x-width/2U),114U,value,active?UI_COLOR_FOCUS:UI_COLOR_TEXT,background,16U);
    };
    if(page_redraw){
        ui.fill(24U,38U,272U,164U,UI_COLOR_PANEL);Lcd_draw_rect(24U,38U,272U,164U,UI_COLOR_LINE);
        ui.text(112U,52U,UiTextId::AdjustBoardSize,UI_COLOR_MUTED,UI_COLOR_PANEL,24U);
        ui.lr_hint(31U,114U,258U,UI_COLOR_PANEL);
        for(uint8_t variant=0U;variant<Game2048Rules::VariantCount;variant++){draw_choice(variant,variant==selected);}
        ui.text(104U,176U,UiTextId::PromptSaveCancel,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    }
    else{draw_choice(previous,false);draw_choice(selected,true);}
}

GameSelectSignal Game2048::handle_variant_select(const InputEvent& input,uint8_t* variant) const{
    if(variant==NULL||*variant>=Game2048Rules::VariantCount){return GameSelectSignal::None;}
    if(Input_is_pressed(input,InputAction::Action1)){*variant=*variant==0U?(Game2048Rules::VariantCount-1U):(uint8_t)(*variant-1U);return GameSelectSignal::SelectionChanged;}
    if(Input_is_pressed(input,InputAction::Action2)){*variant=(uint8_t)((*variant+1U)%Game2048Rules::VariantCount);return GameSelectSignal::SelectionChanged;}
    if(Input_is_pressed(input,InputAction::Confirm)){return GameSelectSignal::Confirmed;}
    return GameSelectSignal::None;
}
