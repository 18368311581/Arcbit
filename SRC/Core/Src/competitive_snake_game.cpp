#include "competitive_snake_game.hpp"
#include "basic.h"
#include "rng.h"
#include "save.hpp"
#include "ui.hpp"
#include "ui_sound.hpp"

#define COMPETITIVE_SNAKE_SNAPSHOT_MAGIC   0x43534E31UL
#define COMPETITIVE_SNAKE_SNAPSHOT_VERSION 1U
#define COMPETITIVE_SNAKE_DATA_VERSION     1U

namespace {

static const uint16_t speed_intervals[CompetitiveSnakeGame::SpeedCount]={125U,100U,75U,50U,25U};

#pragma pack(push,1)
struct CompetitiveSnakeSnapshot {
    uint32_t magic;
    uint16_t version;
    uint8_t speed_index;
    uint8_t direction;
    uint8_t queue_count;
    uint8_t queue[2];
    uint8_t reserved;
    uint16_t length;
    uint16_t food;
    uint16_t remaining_step_ms;
    uint32_t score;
    uint16_t segments[CompetitiveSnakeGame::MaxCells];
};

#pragma pack(pop)

struct CompetitiveSnakePersistentData {
    uint32_t high_score[CompetitiveSnakeGame::SpeedCount];
};

static_assert(sizeof(CompetitiveSnakeSnapshot)<=SAVE_SNAPSHOT_CAPACITY,"Competitive Snake snapshot exceeds save capacity");
static_assert(sizeof(CompetitiveSnakePersistentData)<=SAVE_GAME_DATA_CAPACITY,"Competitive Snake data exceeds game slot");
static CompetitiveSnakeSnapshot competitive_snapshot_work;
static CompetitiveSnakePersistentData competitive_persistent_work;

static void speed_text(uint8_t speed,char* text){
    const char* label;switch(speed){
        case 0U:label="Level 0";break;
        case 1U:label="Level 1";break;
        case 2U:label="Level 2";break;
        case 3U:label="Level 3";break;
        case 4U:label="Level 4";break;
        default:label="Normal";break;}
    uint8_t index=0U;while(label[index]!='\0'){text[index]=label[index];index++;}text[index]='\0';
}

}

CompetitiveSnakeGame::CompetitiveSnakeGame():length_(0U),tail_index_(0U),food_(InvalidCell),score_(0U),last_step_ms_(0U),speed_index_(2U),direction_(Direction::Right),direction_queue_count_(0U),dirty_count_(0U),running_(false),full_dirty_(true),status_dirty_(true){persistent_defaults();}

GameId CompetitiveSnakeGame::id() const{return GameId::CompetitiveSnake;}
const char* CompetitiveSnakeGame::title() const{return "Arena Snake";}
uint8_t CompetitiveSnakeGame::variant_count() const{return SpeedCount;}
uint8_t CompetitiveSnakeGame::default_variant() const{return 2U;}
const char* CompetitiveSnakeGame::variant_page_title() const{return "Difficulty";}
UiTextId CompetitiveSnakeGame::variant_action_text() const{return UiTextId::AdjustDifficulty;}
uint16_t CompetitiveSnakeGame::persistent_data_version() const{return COMPETITIVE_SNAKE_DATA_VERSION;}

void CompetitiveSnakeGame::persistent_defaults(){for(uint8_t i=0U;i<SpeedCount;i++){high_score_[i]=0U;}}

bool CompetitiveSnakeGame::persistent_data_valid(const uint8_t* data,uint16_t length,uint16_t version) const{return data!=NULL&&length==sizeof(CompetitiveSnakePersistentData)&&version==COMPETITIVE_SNAKE_DATA_VERSION;}

void CompetitiveSnakeGame::load_persistent_data(const uint8_t* data,uint16_t length){
    if(!persistent_data_valid(data,length,COMPETITIVE_SNAKE_DATA_VERSION)){return;}
    uint8_t* target=(uint8_t*)&competitive_persistent_work;for(uint16_t i=0U;i<sizeof(competitive_persistent_work);i++){target[i]=data[i];}
    for(uint8_t i=0U;i<SpeedCount;i++){high_score_[i]=competitive_persistent_work.high_score[i];}
}

uint16_t CompetitiveSnakeGame::make_persistent_data(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(CompetitiveSnakePersistentData)){return 0U;}
    for(uint8_t i=0U;i<SpeedCount;i++){competitive_persistent_work.high_score[i]=high_score_[i];}
    const uint8_t* source=(const uint8_t*)&competitive_persistent_work;for(uint16_t i=0U;i<sizeof(competitive_persistent_work);i++){data[i]=source[i];}
    return sizeof(competitive_persistent_work);
}

GameOverview CompetitiveSnakeGame::persistent_overview(uint8_t selected_variant) const{
    uint8_t speed=selected_variant<SpeedCount?selected_variant:default_variant();
    return {high_score_[speed],0U,SpeedCount,SpeedCount,speed,{0U,0U,0U}};
}

GameRecordSummary CompetitiveSnakeGame::record_summary(const uint8_t* data,uint16_t length,uint16_t version) const{
    GameRecordSummary summary={0U,0U,0U,0U,0U};if(!persistent_data_valid(data,length,version)){return summary;}
    const CompetitiveSnakePersistentData& value=*(const CompetitiveSnakePersistentData*)data;
    for(uint8_t i=0U;i<SpeedCount;i++){if(value.high_score[i]>summary.value){summary.value=value.high_score[i];summary.variant=i;}}
    summary.valid=summary.value!=0U?1U:0U;return summary;
}

void CompetitiveSnakeGame::format_record_variant(uint8_t variant,char* text,uint8_t capacity) const{if(text==NULL||capacity==0U){return;}char value[12];speed_text(variant,value);copy_ascii(value,text,capacity);}

void CompetitiveSnakeGame::apply_result(const GameResult& value,bool* new_record,bool* new_unlock){
    bool record=false;if(value.variant<SpeedCount&&value.score>high_score_[value.variant]){high_score_[value.variant]=value.score;record=true;}
    if(new_record!=NULL){*new_record=record;}if(new_unlock!=NULL){*new_unlock=false;}
}

uint16_t CompetitiveSnakeGame::speed_interval(uint8_t variant){return speed_intervals[variant<SpeedCount?variant:2U];}
uint16_t CompetitiveSnakeGame::make_cell(uint8_t x,uint8_t y){return (uint16_t)y*Width+x;}
uint8_t CompetitiveSnakeGame::cell_x(uint16_t cell){return (uint8_t)(cell%Width);}
uint8_t CompetitiveSnakeGame::cell_y(uint16_t cell){return (uint8_t)(cell/Width);}

bool CompetitiveSnakeGame::opposite(Direction first,Direction second){
    return (first==Direction::Up&&second==Direction::Down)||(first==Direction::Down&&second==Direction::Up)||
           (first==Direction::Left&&second==Direction::Right)||(first==Direction::Right&&second==Direction::Left);
}

uint16_t CompetitiveSnakeGame::head_cell() const{return length_==0U?InvalidCell:segments_[(uint16_t)((tail_index_+length_-1U)%MaxCells)];}

bool CompetitiveSnakeGame::body_contains(uint16_t cell,bool exclude_tail) const{
    uint16_t start=exclude_tail&&length_!=0U?1U:0U;
    for(uint16_t i=start;i<length_;i++){if(segments_[(uint16_t)((tail_index_+i)%MaxCells)]==cell){return true;}}
    return false;
}

bool CompetitiveSnakeGame::spawn_food(){
    uint16_t start=(uint16_t)Rng_get_range_int(0,MaxCells-1U);
    for(uint16_t offset=0U;offset<MaxCells;offset++){
        uint16_t candidate=(uint16_t)((start+offset)%MaxCells);
        if(!body_contains(candidate,false)){food_=candidate;add_dirty(candidate);return true;}
    }
    food_=InvalidCell;return false;
}

void CompetitiveSnakeGame::add_dirty(uint16_t cell){
    if(cell>=MaxCells){return;}
    for(uint8_t i=0U;i<dirty_count_;i++){if(dirty_[i]==cell){return;}}
    if(dirty_count_<sizeof(dirty_)/sizeof(dirty_[0])){dirty_[dirty_count_++]=cell;}
    else{full_dirty_=true;}
}

CompetitiveSnakeGame::Direction CompetitiveSnakeGame::planned_direction() const{return direction_queue_count_==0U?direction_:direction_queue_[direction_queue_count_-1U];}

bool CompetitiveSnakeGame::queue_direction(Direction requested){
    Direction planned=planned_direction();
    if(requested==planned||opposite(requested,planned)||direction_queue_count_>=2U){return false;}
    direction_queue_[direction_queue_count_++]=requested;return true;
}

void CompetitiveSnakeGame::pop_direction(){
    if(direction_queue_count_==0U){return;}
    direction_=direction_queue_[0];
    if(direction_queue_count_==2U){direction_queue_[0]=direction_queue_[1];}
    direction_queue_count_--;
}

void CompetitiveSnakeGame::start_new(uint8_t variant){
    speed_index_=variant<SpeedCount?variant:2U;length_=4U;tail_index_=0U;food_=InvalidCell;score_=0U;
    direction_=Direction::Right;direction_queue_count_=0U;dirty_count_=0U;running_=true;full_dirty_=true;status_dirty_=true;
    uint8_t y=Height/2U;uint8_t first_x=(uint8_t)(Width/2U-3U);
    for(uint16_t i=0U;i<length_;i++){segments_[i]=make_cell((uint8_t)(first_x+i),y);}
    spawn_food();last_step_ms_=HAL_GetTick();
}

void CompetitiveSnakeGame::handle_input(const InputEvent& input){
    Direction requested=Direction::Right;bool found=true;
    if(Input_is_pressed(input,InputAction::Up)){requested=Direction::Up;}
    else if(Input_is_pressed(input,InputAction::Down)){requested=Direction::Down;}
    else if(Input_is_pressed(input,InputAction::Left)){requested=Direction::Left;}
    else if(Input_is_pressed(input,InputAction::Right)){requested=Direction::Right;}
    else{found=false;}
    if(found&&queue_direction(requested)){UiSound_play(UiSoundId::Turn);}
}

GameSignal CompetitiveSnakeGame::update(uint32_t now_ms){
    if(!running_||(uint32_t)(now_ms-last_step_ms_)<speed_interval(speed_index_)){return GameSignal::None;}
    last_step_ms_=now_ms;pop_direction();
    uint16_t old_head=head_cell();int16_t next_x=cell_x(old_head),next_y=cell_y(old_head);
    if(direction_==Direction::Up){next_y--;}else if(direction_==Direction::Down){next_y++;}else if(direction_==Direction::Left){next_x--;}else{next_x++;}
    if(next_x<0||next_x>=Width||next_y<0||next_y>=Height){running_=false;return GameSignal::GameOver;}
    uint16_t next=make_cell((uint8_t)next_x,(uint8_t)next_y);bool eat=next==food_;bool exclude_tail=!eat;
    if(body_contains(next,exclude_tail)){running_=false;return GameSignal::GameOver;}
    add_dirty(old_head);add_dirty(next);
    uint16_t insert=(uint16_t)((tail_index_+length_)%MaxCells);segments_[insert]=next;
    if(eat){
        add_dirty(food_);score_++;status_dirty_=true;
        if(length_<MaxCells){length_++;}
        if(length_>=MaxCells||!spawn_food()){running_=false;return GameSignal::GameOver;}
    }
    else{add_dirty(segments_[tail_index_]);tail_index_=(uint16_t)((tail_index_+1U)%MaxCells);}
    return GameSignal::NeedsDraw;
}

void CompetitiveSnakeGame::draw_cell(Ui& ui,uint16_t cell) const{
    if(cell==food_){ui.grid_cell(cell_x(cell),cell_y(cell),10U,UI_COLOR_FOOD);}
    else if(cell==head_cell()){ui.grid_cell(cell_x(cell),cell_y(cell),10U,UI_COLOR_SNAKE_HEAD);}
    else if(body_contains(cell,false)){ui.grid_cell(cell_x(cell),cell_y(cell),10U,UI_COLOR_SNAKE);}
    else{ui.grid_cell_clear(cell_x(cell),cell_y(cell),10U);}
}

void CompetitiveSnakeGame::draw(Ui& ui,bool full_redraw){
    char value[12];
    if(full_redraw||full_dirty_){
        ui.fill(0U,0U,240U,240U,UI_COLOR_BACKGROUND);ui.fill(240U,0U,80U,240U,UI_COLOR_PANEL);ui.fill(239U,0U,1U,240U,UI_COLOR_LINE);
        ui.text(246U,8U,UiTextId::Difficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
        ui.text(246U,76U,UiTextId::Score,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
        ui.text(246U,144U,UiTextId::Length,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
        for(uint8_t y=0U;y<Height;y++){for(uint8_t x=0U;x<Width;x++){draw_cell(ui,make_cell(x,y));}}
        full_dirty_=false;dirty_count_=0U;status_dirty_=true;
    }
    else{for(uint8_t i=0U;i<dirty_count_;i++){draw_cell(ui,dirty_[i]);}dirty_count_=0U;}
    if(status_dirty_){
        ui.fill(244U,30U,74U,28U,UI_COLOR_PANEL);speed_text(speed_index_,value);ui.ascii_fit(246U,36U,70U,value,UI_COLOR_PRIMARY,UI_COLOR_PANEL);
        ui.fill(244U,98U,74U,28U,UI_COLOR_PANEL);format_u32(score_,value);ui.ascii_fit(246U,104U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
        ui.fill(244U,166U,74U,28U,UI_COLOR_PANEL);format_u32(length_,value);ui.ascii_fit(246U,172U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
        status_dirty_=false;
    }
}

uint16_t CompetitiveSnakeGame::make_snapshot(uint8_t* data,uint16_t capacity) const{
    CompetitiveSnakeSnapshot& snapshot=competitive_snapshot_work;
    if(data==NULL||capacity<sizeof(snapshot)||!running_){return 0U;}
    uint32_t now=HAL_GetTick();uint16_t step=speed_interval(speed_index_);uint32_t elapsed=now-last_step_ms_;
    snapshot.magic=COMPETITIVE_SNAKE_SNAPSHOT_MAGIC;snapshot.version=COMPETITIVE_SNAKE_SNAPSHOT_VERSION;snapshot.speed_index=speed_index_;
    snapshot.direction=(uint8_t)direction_;snapshot.queue_count=direction_queue_count_;snapshot.reserved=0U;snapshot.length=length_;snapshot.food=food_;snapshot.score=score_;
    for(uint8_t i=0U;i<2U;i++){snapshot.queue[i]=(uint8_t)direction_queue_[i];}
    snapshot.remaining_step_ms=elapsed<step?(uint16_t)(step-elapsed):0U;
    for(uint16_t i=0U;i<length_;i++){snapshot.segments[i]=segments_[(uint16_t)((tail_index_+i)%MaxCells)];}
    for(uint16_t i=length_;i<MaxCells;i++){snapshot.segments[i]=InvalidCell;}
    const uint8_t* source=(const uint8_t*)&snapshot;for(uint16_t i=0U;i<sizeof(snapshot);i++){data[i]=source[i];}
    return sizeof(snapshot);
}

bool CompetitiveSnakeGame::restore(const uint8_t* data,uint16_t length){
    CompetitiveSnakeSnapshot& snapshot=competitive_snapshot_work;uint8_t* target=(uint8_t*)&snapshot;
    if(data==NULL||length!=sizeof(snapshot)){return false;}
    for(uint16_t i=0U;i<sizeof(snapshot);i++){target[i]=data[i];}
    if(snapshot.magic!=COMPETITIVE_SNAKE_SNAPSHOT_MAGIC||snapshot.version!=COMPETITIVE_SNAKE_SNAPSHOT_VERSION||snapshot.speed_index>=SpeedCount||snapshot.direction>3U||snapshot.queue_count>2U||snapshot.length<4U||snapshot.length>=MaxCells||snapshot.food>=MaxCells||snapshot.remaining_step_ms>speed_interval(snapshot.speed_index)){return false;}
    for(uint8_t i=0U;i<2U;i++){if(snapshot.queue[i]>3U){return false;}}
    for(uint16_t i=0U;i<snapshot.length;i++){
        uint16_t cell=snapshot.segments[i];if(cell>=MaxCells||cell==snapshot.food){return false;}
        for(uint16_t j=0U;j<i;j++){if(snapshot.segments[j]==cell){return false;}}
        if(i!=0U){uint16_t previous=snapshot.segments[i-1U];uint8_t dx=cell_x(cell)>cell_x(previous)?(uint8_t)(cell_x(cell)-cell_x(previous)):(uint8_t)(cell_x(previous)-cell_x(cell));uint8_t dy=cell_y(cell)>cell_y(previous)?(uint8_t)(cell_y(cell)-cell_y(previous)):(uint8_t)(cell_y(previous)-cell_y(cell));if((uint8_t)(dx+dy)!=1U){return false;}}
    }
    speed_index_=snapshot.speed_index;direction_=(Direction)snapshot.direction;direction_queue_count_=snapshot.queue_count;
    for(uint8_t i=0U;i<2U;i++){direction_queue_[i]=(Direction)snapshot.queue[i];}
    length_=snapshot.length;tail_index_=0U;food_=snapshot.food;score_=snapshot.score;
    for(uint16_t i=0U;i<length_;i++){segments_[i]=snapshot.segments[i];}
    uint16_t step=speed_interval(speed_index_);uint32_t now=HAL_GetTick();last_step_ms_=snapshot.remaining_step_ms==0U?now-step:now-(uint32_t)(step-snapshot.remaining_step_ms);
    running_=true;dirty_count_=0U;full_dirty_=true;status_dirty_=true;return true;
}

GameResult CompetitiveSnakeGame::result() const{return {score_,length_,0U,speed_index_,0U,running_?GameOutcome::InProgress:GameOutcome::Failure,UiTextId::Score,UiTextId::Length,1U};}

UiTextId CompetitiveSnakeGame::display_name() const{return UiTextId::CompetitiveSnake;}
UiTextId CompetitiveSnakeGame::menu_goal_text() const{return UiTextId::GoalCompetitiveSnake;}
void CompetitiveSnakeGame::format_menu_mode(char* text,uint8_t capacity) const{if(text==NULL||capacity==0U){return;}char value[12];speed_text(speed_index_,value);copy_ascii(value,text,capacity);}
uint8_t CompetitiveSnakeGame::menu_control_count() const{return 1U;}
UiTextId CompetitiveSnakeGame::menu_control_text(uint8_t) const{return UiTextId::ControlCompetitiveTurn;}

void CompetitiveSnakeGame::draw_home_thumbnail(Ui& ui,uint16_t x,uint16_t y,uint16_t width,uint16_t height) const{
    if(width<72U||height<72U){return;}
    int16_t center_x=(int16_t)(x+width/2U),center_y=(int16_t)(y+height/2U);
    auto draw_scaled=[&ui,center_x,center_y](uint8_t local_x,uint8_t local_y,uint16_t color){
        int16_t local_center_x=(int16_t)local_x+3,local_center_y=(int16_t)local_y+3;
        int16_t target_x=(int16_t)(center_x+(local_center_x-36)*6/5-4);
        int16_t target_y=(int16_t)(center_y+(local_center_y-36)*6/5-4);
        ui.fill((uint16_t)target_x,(uint16_t)target_y,8U,8U,color);
    };
    draw_scaled(14U,12U,UI_COLOR_FOOD);
    static const uint8_t pixels[][2]={{0U,3U},{1U,3U},{2U,3U},{3U,3U},{4U,3U},{5U,3U},{5U,2U},{5U,1U}};
    for(uint8_t i=0U;i<sizeof(pixels)/sizeof(pixels[0]);i++){draw_scaled((uint8_t)(10U+pixels[i][0]*8U),(uint8_t)(28U+pixels[i][1]*8U),i+1U==sizeof(pixels)/sizeof(pixels[0])?UI_COLOR_SNAKE_HEAD:UI_COLOR_SNAKE);}
}

void CompetitiveSnakeGame::draw_entry_page(Ui& ui,const GameActivity& activity,const GameOverview& overview) const{
    char value[20];
    ui.fill(16U,38U,288U,126U,UI_COLOR_PANEL);Lcd_draw_rect(16U,38U,288U,126U,UI_COLOR_LINE);
    ui.fill(27U,49U,88U,88U,UI_COLOR_BACKGROUND);Lcd_draw_rect(27U,49U,88U,88U,UI_COLOR_LINE);
    for(uint8_t i=0U;i<7U;i++){ui.fill((uint16_t)(35U+i*9U),96U,8U,8U,i==6U?UI_COLOR_SNAKE_HEAD:UI_COLOR_SNAKE);}ui.fill(50U,65U,8U,8U,UI_COLOR_FOOD);
    ui.text(132U,45U,UiTextId::CompetitiveSnake,UI_COLOR_PRIMARY,UI_COLOR_PANEL,24U);
    ui.text(132U,78U,UiTextId::Difficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);speed_text(overview.last_variant,value);ui.ascii_fit(224U,78U,68U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(132U,101U,UiTextId::HighScore,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);format_u32(overview.high_score,value);ui.ascii_fit(224U,101U,68U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(28U,143U,UiTextId::RecentPlay,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    format_activity(activity,value);ui.ascii_fit(100U,143U,166U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
}

void CompetitiveSnakeGame::draw_entry_control(Ui& ui,uint8_t control,bool selected,bool snapshot_available) const{
    if(control==0U){ui.large_button(16U,180U,140U,snapshot_available?UiTextId::ContinueGame:UiTextId::StartGame,selected);}
    else if(control==1U){ui.large_button(164U,180U,140U,UiTextId::AdjustDifficulty,selected);}
}

bool CompetitiveSnakeGame::variant_unlocked(const GameProgressView&,uint8_t variant) const{return variant<SpeedCount;}

void CompetitiveSnakeGame::draw_variant_select(Ui& ui,uint8_t selected,uint8_t previous,const GameProgressView&,bool page_redraw) const{
    char value[12];
    auto draw_marker=[&ui](uint8_t index,bool active){uint16_t x=(uint16_t)(62U+index*49U);ui.filled_circle((int16_t)x,116,8U,active?UI_COLOR_FOCUS:UI_COLOR_LINE);};
    if(page_redraw){
        ui.fill(24U,38U,272U,164U,UI_COLOR_PANEL);Lcd_draw_rect(24U,38U,272U,164U,UI_COLOR_LINE);
        ui.text(112U,52U,UiTextId::AdjustDifficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,24U);
        ui.text(58U,82U,UiTextId::Difficulty,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);ui.lr_hint(150U,82U,120U,UI_COLOR_PANEL);
        for(uint8_t i=0U;i<SpeedCount;i++){draw_marker(i,i==selected);}
        ui.text(104U,176U,UiTextId::PromptSaveCancel,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    }
    else{draw_marker(previous,false);draw_marker(selected,true);ui.fill(184U,80U,70U,20U,UI_COLOR_PANEL);}
    speed_text(selected,value);ui.ascii_fit(186U,82U,66U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
}

GameSelectSignal CompetitiveSnakeGame::handle_variant_select(const InputEvent& input,uint8_t* variant) const{
    if(variant==NULL||*variant>=SpeedCount){return GameSelectSignal::None;}
    if(Input_is_pressed(input,InputAction::Action1)){*variant=*variant==0U?(SpeedCount-1U):(uint8_t)(*variant-1U);return GameSelectSignal::SelectionChanged;}
    if(Input_is_pressed(input,InputAction::Action2)){*variant=(uint8_t)((*variant+1U)%SpeedCount);return GameSelectSignal::SelectionChanged;}
    if(Input_is_pressed(input,InputAction::Confirm)){return GameSelectSignal::Confirmed;}
    return GameSelectSignal::None;
}
