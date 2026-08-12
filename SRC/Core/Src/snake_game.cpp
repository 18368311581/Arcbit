#include "snake_game.hpp"
#include "rng.h"
#include "save.hpp"
#include "ui.hpp"
#include "ui_sound.hpp"

enum class SnakeLevelDirection : uint8_t {Up=0U,Down,Left,Right};

struct SnakeLevelDefinition {
    uint16_t target_score;
    uint16_t base_step_ms;
    uint8_t lives;
    uint8_t spawn_x;
    uint8_t spawn_y;
    SnakeLevelDirection spawn_direction;
    uint8_t wall_pattern;
    uint8_t wall_variant;
    uint8_t belt_mask;
};

namespace {

#define LEVEL(PAGE,LOCAL,PATTERN,BELTS) \
    {(uint16_t)(240U+(PAGE)*20U+((LOCAL)/3U)*10U+(((PAGE)==5U&&(LOCAL)>=9U)?20U:0U)), \
     (uint16_t)(160U-(PAGE)*6U-(LOCAL)/2U), \
     (uint8_t)((PAGE)<2U?3U:((PAGE)<5U?2U:1U)),2U,2U,SnakeLevelDirection::Right, \
     (uint8_t)(PATTERN),(uint8_t)(((PAGE)*12U+(LOCAL))%3U),(uint8_t)(BELTS)}

static const SnakeLevelDefinition levels[SNAKE_LEVEL_COUNT]={
    LEVEL(0,0,0,0), LEVEL(0,1,1,0), LEVEL(0,2,2,0), LEVEL(0,3,7,0),
    LEVEL(0,4,3,0), LEVEL(0,5,4,0), LEVEL(0,6,8,0), LEVEL(0,7,9,0),
    LEVEL(0,8,5,0), LEVEL(0,9,6,0), LEVEL(0,10,10,0), LEVEL(0,11,11,0),

    LEVEL(1,0,0,1), LEVEL(1,1,1,2), LEVEL(1,2,2,4), LEVEL(1,3,7,8),
    LEVEL(1,4,3,1), LEVEL(1,5,4,2), LEVEL(1,6,8,4), LEVEL(1,7,9,8),
    LEVEL(1,8,5,1), LEVEL(1,9,6,2), LEVEL(1,10,10,4), LEVEL(1,11,11,8),

    LEVEL(2,0,1,3), LEVEL(2,1,2,5), LEVEL(2,2,7,10), LEVEL(2,3,3,6),
    LEVEL(2,4,4,9), LEVEL(2,5,8,12), LEVEL(2,6,9,5), LEVEL(2,7,5,10),
    LEVEL(2,8,6,3), LEVEL(2,9,10,12), LEVEL(2,10,11,6), LEVEL(2,11,7,9),

    LEVEL(3,0,3,7), LEVEL(3,1,4,11), LEVEL(3,2,8,13), LEVEL(3,3,9,14),
    LEVEL(3,4,5,7), LEVEL(3,5,6,11), LEVEL(3,6,10,13), LEVEL(3,7,11,14),
    LEVEL(3,8,7,7), LEVEL(3,9,8,11), LEVEL(3,10,9,13), LEVEL(3,11,10,14),

    LEVEL(4,0,5,15), LEVEL(4,1,6,7), LEVEL(4,2,10,11), LEVEL(4,3,11,13),
    LEVEL(4,4,7,14), LEVEL(4,5,8,15), LEVEL(4,6,9,7), LEVEL(4,7,3,11),
    LEVEL(4,8,4,13), LEVEL(4,9,2,14), LEVEL(4,10,11,15), LEVEL(4,11,6,15),

    LEVEL(5,0,6,15), LEVEL(5,1,10,15), LEVEL(5,2,11,15), LEVEL(5,3,7,15),
    LEVEL(5,4,8,15), LEVEL(5,5,9,15), LEVEL(5,6,1,15), LEVEL(5,7,3,15),
    LEVEL(5,8,4,15), LEVEL(5,9,2,15), LEVEL(5,10,1,15), LEVEL(5,11,3,15)
};

#undef LEVEL

static bool snake_in_range(uint8_t value,uint8_t first,uint8_t last){return value>=first&&value<=last;}

static const SnakeLevelDefinition& snake_level_definition(uint8_t level){
    return levels[level<SNAKE_LEVEL_COUNT?level:0U];
}

static bool snake_level_wall(uint8_t level,uint8_t x,uint8_t y){
    if(level>=SNAKE_LEVEL_COUNT||x>=24U||y>=24U){return true;}
    const SnakeLevelDefinition& definition=levels[level];
    uint8_t shift=(uint8_t)(definition.wall_variant%3U);
    switch(definition.wall_pattern){
    case 1U:return x==(uint8_t)(10U+shift)&&snake_in_range(y,5U,19U)&&!snake_in_range(y,(uint8_t)(10U+shift),(uint8_t)(12U+shift));
    case 2U:return y==(uint8_t)(10U+shift)&&snake_in_range(x,5U,19U)&&!snake_in_range(x,(uint8_t)(10U+shift),(uint8_t)(12U+shift));
    case 3U:return (x==7U||x==16U)&&snake_in_range(y,5U,19U)&&!snake_in_range(y,(uint8_t)(10U+shift),(uint8_t)(12U+shift));
    case 4U:return (y==7U||y==16U)&&snake_in_range(x,5U,19U)&&!snake_in_range(x,(uint8_t)(10U+shift),(uint8_t)(12U+shift));
    case 5U:{bool horizontal=(y==6U||y==17U)&&snake_in_range(x,6U,17U)&&!snake_in_range(x,11U,12U);bool vertical=(x==6U||x==17U)&&snake_in_range(y,6U,17U)&&!snake_in_range(y,11U,12U);return horizontal||vertical;}
    case 6U:{bool vertical=snake_in_range(x,11U,12U)&&snake_in_range(y,5U,19U)&&!snake_in_range(y,(uint8_t)(7U+shift),(uint8_t)(8U+shift))&&!snake_in_range(y,15U,16U);bool horizontal=snake_in_range(y,11U,12U)&&snake_in_range(x,5U,19U)&&!snake_in_range(x,7U,8U)&&!snake_in_range(x,(uint8_t)(15U-shift),(uint8_t)(16U-shift));return vertical||horizontal;}
    case 7U:return (snake_in_range(x,7U,9U)||snake_in_range(x,15U,17U))&&(snake_in_range(y,7U,9U)||snake_in_range(y,15U,17U));
    case 8U:return (x==(uint8_t)(8U+shift)&&snake_in_range(y,6U,13U))||(x==(uint8_t)(15U-shift)&&snake_in_range(y,11U,18U));
    case 9U:return (y==(uint8_t)(7U+shift)&&snake_in_range(x,5U,14U))||(y==(uint8_t)(16U-shift)&&snake_in_range(x,10U,19U));
    case 10U:{bool horizontal=(y==5U||y==18U)&&snake_in_range(x,5U,18U)&&!snake_in_range(x,11U,12U);bool vertical=(x==5U||x==18U)&&snake_in_range(y,5U,18U)&&!snake_in_range(y,11U,12U);return horizontal||vertical;}
    case 11U:return (x==7U&&snake_in_range(y,6U,18U)&&!snake_in_range(y,9U,11U))||(x==12U&&snake_in_range(y,5U,17U)&&!snake_in_range(y,13U,15U))||(x==17U&&snake_in_range(y,7U,19U)&&!snake_in_range(y,(uint8_t)(9U+shift),(uint8_t)(11U+shift)));
    default:return false;
    }
}

static uint8_t snake_level_belt_direction(uint8_t level,uint8_t x,uint8_t y){
    if(level>=SNAKE_LEVEL_COUNT){return SNAKE_BELT_NONE;}
    uint8_t mask=levels[level].belt_mask;
    uint8_t lanes=level<36U?2U:3U;
    uint8_t far_first=(uint8_t)(23U-lanes);
    if((mask&1U)!=0U&&snake_in_range(y,1U,lanes)&&snake_in_range(x,8U,13U)){return (uint8_t)SnakeLevelDirection::Right;}
    if((mask&2U)!=0U&&snake_in_range(y,far_first,22U)&&snake_in_range(x,10U,15U)){return (uint8_t)SnakeLevelDirection::Left;}
    if((mask&4U)!=0U&&snake_in_range(x,1U,lanes)&&snake_in_range(y,8U,13U)){return (uint8_t)SnakeLevelDirection::Down;}
    if((mask&8U)!=0U&&snake_in_range(x,far_first,22U)&&snake_in_range(y,10U,15U)){return (uint8_t)SnakeLevelDirection::Up;}
    return SNAKE_BELT_NONE;
}

static bool snake_belt_move_allowed(uint8_t current_belt,uint8_t next_belt,uint8_t direction){
    if(current_belt==SNAKE_BELT_NONE){return next_belt==SNAKE_BELT_NONE||next_belt==direction;}
    if(next_belt!=SNAKE_BELT_NONE&&next_belt==current_belt){return true;}
    return current_belt==direction&&(next_belt==SNAKE_BELT_NONE||next_belt==direction);
}

}

#define SNAKE_SNAPSHOT_MAGIC      0x534E4B32UL
#define SNAKE_SNAPSHOT_VERSION    2U
#define SNAKE_PERSISTENT_VERSION  1U
#define SNAKE_RECOVERY_MS         800U

#pragma pack(push,1)
struct SnakeSnapshot {
    uint32_t magic;
    uint16_t version;
    uint8_t level;
    uint8_t lives;
    uint8_t direction;
    uint8_t queue_count;
    uint8_t queue[2];
    uint8_t special_type;
    uint8_t completed;
    uint8_t forward_held;
    uint8_t reserved;
    uint16_t length;
    uint16_t growth_pending;
    uint16_t normal_food_count;
    uint16_t food;
    uint16_t special_food;
    uint32_t score;
    uint32_t special_remaining_ms;
    uint32_t slow_remaining_ms;
    uint32_t recovery_remaining_ms;
    uint16_t segments[SnakeGame::MaxCells];
};

#pragma pack(pop)

struct SnakePersistentData {
    uint8_t completed_levels[(SNAKE_LEVEL_COUNT+7U)/8U];
    uint8_t reserved[3];
    uint16_t high_length[SNAKE_LEVEL_COUNT];
    uint32_t high_score[SNAKE_LEVEL_COUNT];
};

static_assert(sizeof(SnakeSnapshot)<=SAVE_SNAPSHOT_CAPACITY,"Snake snapshot exceeds save capacity");
static_assert(sizeof(SnakePersistentData)<=SAVE_GAME_DATA_CAPACITY,"Snake persistent data exceeds game slot");
static SnakeSnapshot snake_snapshot_work;
static SnakePersistentData snake_persistent_work;

static void snake_append_u32(char* text,uint8_t* index,uint32_t value){
    char reverse[10];uint8_t count=0U;
    if(value==0U){text[(*index)++]='0';text[*index]='\0';return;}
    while(value!=0U&&count<10U){reverse[count++]=(char)('0'+value%10U);value/=10U;}
    while(count!=0U&&*index<47U){text[(*index)++]=reverse[--count];}text[*index]='\0';
}
static uint8_t snake_level_position(uint8_t local){
    uint8_t row=(uint8_t)(local/3U),column=(uint8_t)(local%3U);
    if((row&1U)!=0U){column=(uint8_t)(2U-column);}
    return (uint8_t)(row*3U+column);
}

SnakeGame::SnakeGame():length_(0U),tail_index_(0U),food_(InvalidCell),special_food_(InvalidCell),growth_pending_(0U),normal_food_count_(0U),score_(0U),last_step_ms_(0U),special_deadline_ms_(0U),slow_deadline_ms_(0U),recovery_deadline_ms_(0U),map_(0U),lives_(0U),direction_(Direction::Right),direction_queue_count_(0U),special_type_(SpecialFood::None),dirty_count_(0U),pending_signal_(GameSignal::None),running_(false),completed_(false),forward_held_(false),full_dirty_(true),status_dirty_(true),lives_dirty_(true){persistent_defaults();}

GameId SnakeGame::id() const{return GameId::Snake;}
const char* SnakeGame::title() const{return "Snake";}
uint16_t SnakeGame::make_cell(uint8_t x,uint8_t y){return (uint16_t)y*Width+x;}
uint8_t SnakeGame::cell_x(uint16_t cell){return (uint8_t)(cell%Width);}
uint8_t SnakeGame::cell_y(uint16_t cell){return (uint8_t)(cell/Width);}

bool SnakeGame::opposite(Direction first,Direction second){
    return (first==Direction::Up&&second==Direction::Down)||(first==Direction::Down&&second==Direction::Up)||
           (first==Direction::Left&&second==Direction::Right)||(first==Direction::Right&&second==Direction::Left);
}

uint32_t SnakeGame::remaining_ms(uint32_t now,uint32_t deadline){return (int32_t)(deadline-now)>0?(uint32_t)(deadline-now):0U;}

bool SnakeGame::map_obstacle(uint8_t map,uint8_t x,uint8_t y){return snake_level_wall(map,x,y);}
uint8_t SnakeGame::map_belt(uint8_t map,uint8_t x,uint8_t y){return snake_level_belt_direction(map,x,y);}
uint16_t SnakeGame::map_base_step(uint8_t map){return snake_level_definition(map).base_step_ms;}
uint16_t SnakeGame::map_target(uint8_t map){return snake_level_definition(map).target_score;}
uint8_t SnakeGame::map_lives(uint8_t map){return snake_level_definition(map).lives;}
UiTextId SnakeGame::map_name(uint8_t){return UiTextId::Level;}
uint8_t SnakeGame::variant_count() const{return SNAKE_LEVEL_COUNT;}
uint8_t SnakeGame::default_variant() const{return 0U;}
const char* SnakeGame::variant_page_title() const{return "Levels";}
UiTextId SnakeGame::variant_action_text() const{return UiTextId::SelectLevel;}

uint16_t SnakeGame::persistent_data_version() const{return SNAKE_PERSISTENT_VERSION;}

void SnakeGame::persistent_defaults(){
    for(uint8_t i=0U;i<sizeof(completed_levels_);i++){completed_levels_[i]=0U;}
    for(uint8_t i=0U;i<SNAKE_LEVEL_COUNT;i++){high_length_[i]=0U;high_score_[i]=0U;}
}

bool SnakeGame::persistent_data_valid(const uint8_t* data,uint16_t length,uint16_t version) const{return data!=NULL&&length==sizeof(SnakePersistentData)&&version==SNAKE_PERSISTENT_VERSION;}

void SnakeGame::load_persistent_data(const uint8_t* data,uint16_t length){
    if(!persistent_data_valid(data,length,SNAKE_PERSISTENT_VERSION)){return;}
    uint8_t* target=(uint8_t*)&snake_persistent_work;for(uint16_t i=0U;i<sizeof(snake_persistent_work);i++){target[i]=data[i];}
    for(uint8_t i=0U;i<sizeof(completed_levels_);i++){completed_levels_[i]=snake_persistent_work.completed_levels[i];}
    for(uint8_t i=0U;i<SNAKE_LEVEL_COUNT;i++){high_length_[i]=snake_persistent_work.high_length[i];high_score_[i]=snake_persistent_work.high_score[i];}
}

uint16_t SnakeGame::make_persistent_data(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(SnakePersistentData)){return 0U;}
    uint8_t* work=(uint8_t*)&snake_persistent_work;for(uint16_t i=0U;i<sizeof(snake_persistent_work);i++){work[i]=0U;}
    for(uint8_t i=0U;i<sizeof(completed_levels_);i++){snake_persistent_work.completed_levels[i]=completed_levels_[i];}
    for(uint8_t i=0U;i<SNAKE_LEVEL_COUNT;i++){snake_persistent_work.high_length[i]=high_length_[i];snake_persistent_work.high_score[i]=high_score_[i];}
    for(uint16_t i=0U;i<sizeof(snake_persistent_work);i++){data[i]=work[i];}
    return sizeof(snake_persistent_work);
}

GameOverview SnakeGame::persistent_overview(uint8_t selected_variant) const{
    GameOverview overview={0U,0U,1U,SNAKE_LEVEL_COUNT,selected_variant,{0U,0U,0U}};
    for(uint8_t i=0U;i<SNAKE_LEVEL_COUNT;i++){
        if(high_score_[i]>overview.high_score){overview.high_score=high_score_[i];}
        if(high_length_[i]>overview.high_progress){overview.high_progress=high_length_[i];}
    }
    while(overview.unlocked_count<SNAKE_LEVEL_COUNT&&level_completed((uint8_t)(overview.unlocked_count-1U))){overview.unlocked_count++;}
    return overview;
}

GameRecordSummary SnakeGame::record_summary(const uint8_t* data,uint16_t length,uint16_t version) const{
    GameRecordSummary summary={0U,0U,0U,0U,0U};if(!persistent_data_valid(data,length,version)){return summary;}
    const SnakePersistentData& value=*(const SnakePersistentData*)data;
    for(uint8_t i=0U;i<SNAKE_LEVEL_COUNT;i++){if(value.high_score[i]>summary.value){summary.value=value.high_score[i];summary.variant=i;}}
    summary.valid=summary.value!=0U?1U:0U;return summary;
}

void SnakeGame::format_record_variant(uint8_t variant,char* text,uint8_t capacity) const{
    if(text==NULL||capacity<4U){return;}text[0]='L';format_u32((uint32_t)variant+1U,&text[1]);text[capacity-1U]='\0';
}

void SnakeGame::apply_result(const GameResult& value,bool* new_record,bool* new_unlock){
    if(value.variant>=SNAKE_LEVEL_COUNT){return;}
    bool record=false,unlock=false;
    if(value.score>high_score_[value.variant]){high_score_[value.variant]=value.score;record=true;}
    if(value.length>high_length_[value.variant]){high_length_[value.variant]=value.length;record=true;}
    if(value.outcome==GameOutcome::LevelComplete&&!level_completed(value.variant)){set_level_completed(value.variant);unlock=value.variant+1U<SNAKE_LEVEL_COUNT;}
    if(new_record!=NULL){*new_record=record;}if(new_unlock!=NULL){*new_unlock=unlock;}
}

bool SnakeGame::level_completed(uint8_t level) const{return level<SNAKE_LEVEL_COUNT&&(completed_levels_[level/8U]&(uint8_t)(1U<<(level%8U)))!=0U;}
void SnakeGame::set_level_completed(uint8_t level){if(level<SNAKE_LEVEL_COUNT){completed_levels_[level/8U]|=(uint8_t)(1U<<(level%8U));}}

bool SnakeGame::variant_unlocked(const GameProgressView&,uint8_t variant) const{
    return variant<SNAKE_LEVEL_COUNT&&(variant==0U||level_completed((uint8_t)(variant-1U)));
}

void SnakeGame::draw_variant_select(Ui& ui,uint8_t selected,uint8_t previous,const GameProgressView&,bool page_redraw) const{
    static const uint16_t xs[3]={38U,92U,146U};static const uint16_t ys[4]={53U,99U,145U,191U};
    auto state=[this](uint8_t level)->uint8_t{return level_completed(level)?2U:(variant_unlocked({NULL,0U},level)?1U:0U);};
    auto draw_node=[&ui,&state,this](uint8_t level){
        uint8_t position=snake_level_position((uint8_t)(level%12U));
        draw_level_node(ui,xs[position%3U],ys[position/3U],(uint8_t)(level+1U),state(level));
    };
    auto draw_focus=[&ui,this](uint8_t level,bool selected_focus){
        uint8_t position=snake_level_position((uint8_t)(level%12U));
        draw_level_focus(ui,xs[position%3U],ys[position/3U],selected_focus);
    };
    auto draw_detail=[&ui,&state,this](uint8_t level){
        char value[16];
        ui.fill(186U,29U,134U,211U,UI_COLOR_PANEL);ui.fill(186U,29U,1U,211U,UI_COLOR_LINE);
        value[0]='L';value[1]='\0';format_u32((uint32_t)level+1U,&value[1]);ui.ascii(194U,35U,value,UI_COLOR_PRIMARY,UI_COLOR_PANEL,16U);
        ui.fill(218U,55U,72U,72U,UI_COLOR_BACKGROUND);Lcd_draw_rect(217U,54U,74U,74U,UI_COLOR_LINE);
        for(uint8_t y=0U;y<Height;y++){
            for(uint8_t x=0U;x<Width;x++){
                if(map_obstacle(level,x,y)){ui.fill((uint16_t)(218U+x*3U),(uint16_t)(55U+y*3U),3U,3U,UI_COLOR_WALL);}
                else if(map_belt(level,x,y)!=SNAKE_BELT_NONE){ui.fill((uint16_t)(218U+x*3U),(uint16_t)(55U+y*3U),3U,3U,UI_COLOR_PRIMARY_DARK);}
            }
        }
        UiTextId text=level_completed(level)?UiTextId::LevelComplete:(variant_unlocked({NULL,0U},level)?UiTextId::Unlocked:UiTextId::Locked);
        ui.text(196U,134U,text,state(level)==0U?UI_COLOR_MUTED:(state(level)==2U?UI_COLOR_GOLD:UI_COLOR_TEXT),UI_COLOR_PANEL,16U);
        value[0]='T';value[1]=':';format_u32(map_target(level),&value[2]);ui.ascii(196U,158U,value,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
        value[0]='V';value[1]=':';format_u32(10000U/map_base_step(level),&value[2]);ui.ascii(250U,158U,value,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
        value[0]='H';value[1]='P';value[2]=':';value[3]=(char)('0'+map_lives(level));value[4]='\0';ui.ascii(196U,181U,value,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
        uint8_t page_index=(uint8_t)(level/12U);value[0]='<';value[1]=' ';value[2]=(char)('1'+page_index);value[3]='/';value[4]='6';value[5]=' ';value[6]='>';value[7]='\0';ui.ascii(72U,216U,value,UI_COLOR_TEXT,UI_COLOR_BACKGROUND,16U);
    };
    if(page_redraw){
        ui.fill(0U,29U,186U,211U,UI_COLOR_BACKGROUND);
        uint8_t first=(uint8_t)((selected/12U)*12U);
        for(uint8_t row=0U;row<4U;row++){
            draw_level_track(ui,xs[0],ys[row],xs[1],ys[row]);draw_level_track(ui,xs[1],ys[row],xs[2],ys[row]);
            if(row<3U){uint8_t column=(row&1U)==0U?2U:0U;draw_level_track(ui,xs[column],ys[row],xs[column],ys[row+1U]);}
        }
        for(uint8_t i=0U;i<12U;i++){draw_node((uint8_t)(first+i));}
        draw_focus(selected,true);
    }
    else{draw_focus(previous,false);draw_focus(selected,true);}
    draw_detail(selected);
}

GameSelectSignal SnakeGame::handle_variant_select(const InputEvent& input,uint8_t* variant) const{
    if(variant==NULL||*variant>=SNAKE_LEVEL_COUNT){return GameSelectSignal::None;}
    uint8_t local=(uint8_t)(*variant%12U),position=snake_level_position(local),column=(uint8_t)(position%3U),row=(uint8_t)(position/3U),next=position;
    if(Input_is_pressed(input,InputAction::Left)&&column>0U){next=(uint8_t)(position-1U);}
    else if(Input_is_pressed(input,InputAction::Right)&&column<2U){next=(uint8_t)(position+1U);}
    else if(Input_is_pressed(input,InputAction::Up)&&row>0U){next=(uint8_t)(position-3U);}
    else if(Input_is_pressed(input,InputAction::Down)&&row<3U){next=(uint8_t)(position+3U);}
    else if(Input_is_pressed(input,InputAction::Action1)){*variant=(uint8_t)((((*variant/12U)+5U)%6U)*12U+local);return GameSelectSignal::PageLeft;}
    else if(Input_is_pressed(input,InputAction::Action2)){*variant=(uint8_t)((((*variant/12U)+1U)%6U)*12U+local);return GameSelectSignal::PageRight;}
    if(next==position){return GameSelectSignal::None;}
    *variant=(uint8_t)((*variant/12U)*12U+snake_level_position(next));
    return GameSelectSignal::SelectionChanged;
}

uint16_t SnakeGame::head_cell(void) const{
    return length_==0U?InvalidCell:segments_[(uint16_t)((tail_index_+length_-1U)%MaxCells)];
}

bool SnakeGame::body_contains(uint16_t cell,bool exclude_tail) const{
    uint16_t start=exclude_tail&&length_!=0U?1U:0U;
    for(uint16_t i=start;i<length_;i++){
        if(segments_[(uint16_t)((tail_index_+i)%MaxCells)]==cell){return true;}
    }
    return false;
}

bool SnakeGame::occupied(uint16_t cell) const{
    if(cell>=MaxCells){return true;}
    uint8_t x=cell_x(cell),y=cell_y(cell);
    return map_obstacle(map_,x,y)||map_belt(map_,x,y)!=SNAKE_BELT_NONE||body_contains(cell,false)||cell==food_||cell==special_food_;
}

bool SnakeGame::find_free_cell(uint16_t* cell) const{
    if(cell==NULL){return false;}
    uint16_t start=(uint16_t)Rng_get_range_int(0,MaxCells-1U);
    for(uint16_t offset=0U;offset<MaxCells;offset++){
        uint16_t candidate=(uint16_t)((start+offset)%MaxCells);
        if(!occupied(candidate)){*cell=candidate;return true;}
    }
    return false;
}

bool SnakeGame::spawn_food(void){
    uint16_t cell;
    if(!find_free_cell(&cell)){food_=InvalidCell;return false;}
    food_=cell;add_dirty(cell);return true;
}

void SnakeGame::spawn_special(uint32_t now_ms){
    uint16_t cell;
    if(special_food_!=InvalidCell||!find_free_cell(&cell)){return;}
    special_food_=cell;
    special_type_=(Rng_get_int()&1U)!=0U?SpecialFood::Golden:SpecialFood::Clock;
    special_deadline_ms_=now_ms+6000U;
    add_dirty(cell);status_dirty_=true;
}

void SnakeGame::add_dirty(uint16_t cell){
    if(cell==InvalidCell){return;}
    for(uint8_t i=0U;i<dirty_count_;i++){if(dirty_[i]==cell){return;}}
    if(dirty_count_<sizeof(dirty_)/sizeof(dirty_[0])){dirty_[dirty_count_++]=cell;}
    else{full_dirty_=true;}
}

SnakeGame::Direction SnakeGame::planned_direction(void) const{
    return direction_queue_count_!=0U?direction_queue_[direction_queue_count_-1U]:direction_;
}

SnakeGame::Direction SnakeGame::input_direction(InputAction action){
    if(action==InputAction::Up){return Direction::Up;}
    if(action==InputAction::Down){return Direction::Down;}
    if(action==InputAction::Left){return Direction::Left;}
    return Direction::Right;
}

InputAction SnakeGame::direction_action(Direction direction){
    if(direction==Direction::Up){return InputAction::Up;}
    if(direction==Direction::Down){return InputAction::Down;}
    if(direction==Direction::Left){return InputAction::Left;}
    return InputAction::Right;
}

bool SnakeGame::queue_direction(Direction direction){
    Direction previous=planned_direction();
    if(direction==previous||opposite(direction,previous)||direction_queue_count_>=2U){return false;}
    direction_queue_[direction_queue_count_++]=direction;
    return true;
}

void SnakeGame::pop_direction(void){
    if(direction_queue_count_==0U){return;}
    direction_=direction_queue_[0];
    if(direction_queue_count_==2U){direction_queue_[0]=direction_queue_[1];}
    direction_queue_count_--;
}

void SnakeGame::lose_life(uint32_t now_ms){
    if(remaining_ms(now_ms,recovery_deadline_ms_)!=0U||!running_){return;}
    if(lives_!=0U){lives_--;}
    direction_queue_count_=0U;
    forward_held_=false;
    status_dirty_=true;
    lives_dirty_=true;
    if(lives_==0U){running_=false;pending_signal_=GameSignal::GameOver;}
    else{recovery_deadline_ms_=now_ms+SNAKE_RECOVERY_MS;pending_signal_=GameSignal::LifeLost;}
}

void SnakeGame::start_new(uint8_t variant){
    map_=variant<SNAKE_LEVEL_COUNT?variant:0U;
    const SnakeLevelDefinition& level=snake_level_definition(map_);
    length_=4U;tail_index_=0U;direction_=(Direction)level.spawn_direction;direction_queue_count_=0U;
    food_=InvalidCell;special_food_=InvalidCell;special_type_=SpecialFood::None;
    growth_pending_=0U;normal_food_count_=0U;score_=0U;lives_=level.lives;
    special_deadline_ms_=0U;slow_deadline_ms_=0U;recovery_deadline_ms_=0U;
    completed_=false;running_=true;forward_held_=false;pending_signal_=GameSignal::None;dirty_count_=0U;
    for(uint16_t i=0U;i<length_;i++){segments_[i]=make_cell((uint8_t)(level.spawn_x+i),level.spawn_y);}
    spawn_food();last_step_ms_=HAL_GetTick();full_dirty_=true;status_dirty_=true;lives_dirty_=true;
}

void SnakeGame::handle_input(const InputEvent& input){
    Direction planned=planned_direction();
    forward_held_=Input_is_held(input,direction_action(planned));
    InputAction action=InputAction::Pause;
    bool has_direction=false;
    if(Input_is_pressed(input,InputAction::Up)){action=InputAction::Up;has_direction=true;}
    else if(Input_is_pressed(input,InputAction::Down)){action=InputAction::Down;has_direction=true;}
    else if(Input_is_pressed(input,InputAction::Left)){action=InputAction::Left;has_direction=true;}
    else if(Input_is_pressed(input,InputAction::Right)){action=InputAction::Right;has_direction=true;}
    if(!has_direction){return;}
    Direction requested=input_direction(action);
    uint32_t now=HAL_GetTick();
    if(opposite(requested,planned)){
        if(remaining_ms(now,recovery_deadline_ms_)==0U){lose_life(now);}
        return;
    }
    if(requested!=planned&&queue_direction(requested)){UiSound_play(UiSoundId::Turn);}
    forward_held_=Input_is_held(input,direction_action(planned_direction()));
}

uint16_t SnakeGame::current_step(uint32_t now_ms) const{
    uint16_t acceleration=(uint16_t)((normal_food_count_/5U)*10U);
    if(acceleration>60U){acceleration=60U;}
    uint16_t base=map_base_step(map_);
    base=base>acceleration?(uint16_t)(base-acceleration):75U;
    if(base<75U){base=75U;}
    if(remaining_ms(now_ms,slow_deadline_ms_)!=0U){base=(uint16_t)(base+80U);}
    if(forward_held_){base=(uint16_t)(base/2U);if(base<60U){base=60U;}}
    return base;
}

GameSignal SnakeGame::update(uint32_t now_ms){
    if(pending_signal_!=GameSignal::None){GameSignal signal=pending_signal_;pending_signal_=GameSignal::None;return signal;}
    if(!running_){return GameSignal::None;}
    bool changed=false;
    if(special_food_!=InvalidCell&&remaining_ms(now_ms,special_deadline_ms_)==0U){
        add_dirty(special_food_);special_food_=InvalidCell;special_type_=SpecialFood::None;status_dirty_=true;changed=true;
    }
    if(recovery_deadline_ms_!=0U&&remaining_ms(now_ms,recovery_deadline_ms_)==0U){recovery_deadline_ms_=0U;status_dirty_=true;changed=true;}
    if(remaining_ms(now_ms,recovery_deadline_ms_)!=0U){return changed?GameSignal::NeedsDraw:GameSignal::None;}
    if((uint32_t)(now_ms-last_step_ms_)<current_step(now_ms)){return changed?GameSignal::NeedsDraw:GameSignal::None;}
    last_step_ms_=now_ms;
    pop_direction();
    uint16_t old_head=head_cell();
    int16_t next_x=cell_x(old_head),next_y=cell_y(old_head);
    if(direction_==Direction::Up){next_y--;}
    else if(direction_==Direction::Down){next_y++;}
    else if(direction_==Direction::Left){next_x--;}
    else{next_x++;}
    if(next_x<0||next_x>=Width||next_y<0||next_y>=Height){lose_life(now_ms);GameSignal signal=pending_signal_;pending_signal_=GameSignal::None;return signal;}
    uint8_t current_belt=map_belt(map_,cell_x(old_head),cell_y(old_head));
    uint8_t next_belt=map_belt(map_,(uint8_t)next_x,(uint8_t)next_y);
    if(!snake_belt_move_allowed(current_belt,next_belt,(uint8_t)direction_)){
        lose_life(now_ms);GameSignal signal=pending_signal_;pending_signal_=GameSignal::None;return signal;
    }
    uint16_t next=make_cell((uint8_t)next_x,(uint8_t)next_y);
    bool eat_normal=next==food_,eat_special=next==special_food_;
    uint16_t growth_add=eat_normal?1U:0U;
    if(eat_special){growth_add=(uint16_t)(growth_add+(special_type_==SpecialFood::Golden?2U:1U));}
    bool exclude_tail=growth_pending_==0U&&growth_add==0U;
    if(map_obstacle(map_,(uint8_t)next_x,(uint8_t)next_y)||body_contains(next,exclude_tail)){
        lose_life(now_ms);GameSignal signal=pending_signal_;pending_signal_=GameSignal::None;return signal;
    }
    add_dirty(old_head);
    if(eat_normal){add_dirty(food_);food_=InvalidCell;score_+=10U;normal_food_count_++;}
    if(eat_special){
        add_dirty(special_food_);
        if(special_type_==SpecialFood::Golden){score_+=50U;}
        else{score_+=20U;slow_deadline_ms_=now_ms+5000U;}
        special_food_=InvalidCell;special_type_=SpecialFood::None;special_deadline_ms_=0U;
    }
    growth_pending_=(uint16_t)(growth_pending_+growth_add);
    uint16_t insert=(uint16_t)((tail_index_+length_)%MaxCells);segments_[insert]=next;
    if(growth_pending_!=0U){
        if(length_<MaxCells){length_++;growth_pending_--;}
        else{running_=false;completed_=true;return GameSignal::LevelComplete;}
    }
    else{add_dirty(segments_[tail_index_]);tail_index_=(uint16_t)((tail_index_+1U)%MaxCells);}
    add_dirty(next);status_dirty_=true;
    if(!completed_&&score_>=map_target(map_)){completed_=true;}
    if(eat_normal){
        if(!spawn_food()){running_=false;completed_=true;return GameSignal::LevelComplete;}
        if(normal_food_count_%5U==0U){spawn_special(now_ms);}
    }
    return GameSignal::NeedsDraw;
}

uint16_t SnakeGame::cell_color(uint16_t cell) const{
    if(cell>=MaxCells){return UI_COLOR_BACKGROUND;}
    if(map_obstacle(map_,cell_x(cell),cell_y(cell))){return UI_COLOR_WALL;}
    if(cell==head_cell()){return UI_COLOR_SNAKE_HEAD;}
    if(body_contains(cell,false)){return UI_COLOR_SNAKE;}
    if(cell==food_){return UI_COLOR_FOOD;}
    if(cell==special_food_){return special_type_==SpecialFood::Golden?UI_COLOR_GOLD:UI_COLOR_PRIMARY;}
    return UI_COLOR_BACKGROUND;
}

void SnakeGame::draw_level_node(Ui& ui,uint16_t x,uint16_t y,uint8_t level,uint8_t state) const{
    char value[4];
    uint16_t color=state==0U?UI_COLOR_MUTED:(state==2U?UI_COLOR_GOLD:UI_COLOR_PRIMARY);
    ui.filled_circle((int16_t)x,(int16_t)y,15U,color);
    Lcd_draw_circle((int16_t)x,(int16_t)y,15U,UI_COLOR_LINE);
    Lcd_draw_circle((int16_t)x,(int16_t)y,16U,UI_COLOR_LINE);
    format_u32(level,value);
    uint16_t width=Ui::text_width(value,12U);
    ui.ascii((uint16_t)(x-width/2U),(uint16_t)(y-6U),value,UI_COLOR_BACKGROUND,color,12U);
}

void SnakeGame::draw_level_focus(Ui& ui,uint16_t x,uint16_t y,bool selected) const{
    uint16_t color=selected?UI_COLOR_SNAKE:UI_COLOR_BACKGROUND;
    ui.fill((uint16_t)(x-22U),(uint16_t)(y-22U),8U,2U,color);ui.fill((uint16_t)(x-22U),(uint16_t)(y-22U),2U,8U,color);
    ui.fill((uint16_t)(x+15U),(uint16_t)(y-22U),8U,2U,color);ui.fill((uint16_t)(x+21U),(uint16_t)(y-22U),2U,8U,color);
    ui.fill((uint16_t)(x-22U),(uint16_t)(y+21U),8U,2U,color);ui.fill((uint16_t)(x-22U),(uint16_t)(y+15U),2U,8U,color);
    ui.fill((uint16_t)(x+15U),(uint16_t)(y+21U),8U,2U,color);ui.fill((uint16_t)(x+21U),(uint16_t)(y+15U),2U,8U,color);
}

void SnakeGame::draw_level_track(Ui& ui,uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1) const{
    const uint16_t edge_color=0x6B4DU;
    const uint16_t track_light=0xC618U;
    const uint16_t track_dark=0x9CD3U;
    if(y0==y1){
        uint16_t first=x0<x1?x0:x1,last=x0<x1?x1:x0;
        Lcd_draw_line((int16_t)first,(int16_t)(y0-5U),(int16_t)last,(int16_t)(y0-5U),edge_color);
        Lcd_draw_line((int16_t)first,(int16_t)(y0+5U),(int16_t)last,(int16_t)(y0+5U),edge_color);
        for(uint16_t x=first;x<=last;x=(uint16_t)(x+4U)){
            uint16_t width=(uint16_t)(last-x+1U);if(width>4U){width=4U;}
            ui.fill(x,(uint16_t)(y0-4U),width,9U,((x-first)/4U&1U)==0U?track_light:track_dark);
        }
    }
    else if(x0==x1){
        uint16_t first=y0<y1?y0:y1,last=y0<y1?y1:y0;
        Lcd_draw_line((int16_t)(x0-5U),(int16_t)first,(int16_t)(x0-5U),(int16_t)last,edge_color);
        Lcd_draw_line((int16_t)(x0+5U),(int16_t)first,(int16_t)(x0+5U),(int16_t)last,edge_color);
        for(uint16_t y=first;y<=last;y=(uint16_t)(y+4U)){
            uint16_t height=(uint16_t)(last-y+1U);if(height>4U){height=4U;}
            ui.fill((uint16_t)(x0-4U),y,9U,height,((y-first)/4U&1U)==0U?track_light:track_dark);
        }
    }
}

void SnakeGame::draw_game_frame(Ui& ui) const{
    char value[4];
    ui.fill(240U,0U,80U,240U,UI_COLOR_PANEL);ui.fill(239U,0U,1U,240U,UI_COLOR_LINE);
    ui.text(246U,4U,UiTextId::Level,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    format_u32((uint32_t)map_+1U,value);ui.ascii(294U,4U,value,UI_COLOR_PRIMARY,UI_COLOR_PANEL,16U);
    ui.text(246U,32U,UiTextId::Score,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    ui.text(246U,72U,UiTextId::TargetScore,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    ui.text(246U,112U,UiTextId::Length,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    ui.text(246U,152U,UiTextId::Lives,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
}

void SnakeGame::draw_game_status(Ui& ui,uint32_t now_ms) const{
    char value[12];
    UiTextId special=UiTextId::Count;uint32_t remaining=0U;
    if(special_food_!=InvalidCell){special=special_type_==SpecialFood::Golden?UiTextId::GoldenFruit:UiTextId::ClockFruit;remaining=remaining_ms(now_ms,special_deadline_ms_);}
    else if(remaining_ms(now_ms,slow_deadline_ms_)!=0U){special=UiTextId::ClockFruit;remaining=remaining_ms(now_ms,slow_deadline_ms_);}
    ui.fill(244U,50U,74U,20U,UI_COLOR_PANEL);format_u32(score_,value);ui.ascii_fit(246U,52U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.fill(244U,90U,74U,20U,UI_COLOR_PANEL);format_u32(map_target(map_),value);ui.ascii_fit(246U,92U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.fill(244U,130U,74U,20U,UI_COLOR_PANEL);format_u32(length_,value);ui.ascii_fit(246U,132U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.fill(242U,192U,77U,46U,UI_COLOR_PANEL);
    if(special!=UiTextId::Count){
        ui.text(244U,194U,special,UI_COLOR_FOCUS,UI_COLOR_PANEL,16U);
        format_u32((remaining+999U)/1000U,value);
        uint8_t end=0U;while(value[end]!='\0'){end++;}value[end++]='s';value[end]='\0';
        ui.ascii_fit(246U,216U,70U,value,UI_COLOR_FOCUS,UI_COLOR_PANEL);
    }
}

void SnakeGame::draw_game_lives(Ui& ui) const{
    ui.fill(244U,170U,74U,18U,UI_COLOR_PANEL);
    for(uint8_t i=0U;i<3U;i++){ui.filled_circle((int16_t)(252U+i*20U),179,5U,i<lives_?UI_COLOR_GOLD:UI_COLOR_LINE);}
}

void SnakeGame::draw_wall_cell(Ui& ui,uint8_t x,uint8_t y) const{ui.fill((uint16_t)x*10U,(uint16_t)y*10U,10U,10U,UI_COLOR_WALL);}

void SnakeGame::draw_belt_cell(Ui& ui,uint8_t x,uint8_t y,uint8_t direction) const{
    uint16_t px=(uint16_t)x*10U,py=(uint16_t)y*10U;ui.fill(px,py,10U,10U,UI_COLOR_PANEL);
    int16_t cx=(int16_t)px+5,cy=(int16_t)py+5;
    if(direction==0U){Lcd_draw_line(cx,cy+2,cx,cy-2,UI_COLOR_PRIMARY);Lcd_draw_line(cx,cy-2,cx-2,cy,UI_COLOR_PRIMARY);Lcd_draw_line(cx,cy-2,cx+2,cy,UI_COLOR_PRIMARY);}
    else if(direction==1U){Lcd_draw_line(cx,cy-2,cx,cy+2,UI_COLOR_PRIMARY);Lcd_draw_line(cx,cy+2,cx-2,cy,UI_COLOR_PRIMARY);Lcd_draw_line(cx,cy+2,cx+2,cy,UI_COLOR_PRIMARY);}
    else if(direction==2U){Lcd_draw_line(cx+2,cy,cx-2,cy,UI_COLOR_PRIMARY);Lcd_draw_line(cx-2,cy,cx,cy-2,UI_COLOR_PRIMARY);Lcd_draw_line(cx-2,cy,cx,cy+2,UI_COLOR_PRIMARY);}
    else{Lcd_draw_line(cx-2,cy,cx+2,cy,UI_COLOR_PRIMARY);Lcd_draw_line(cx+2,cy,cx,cy-2,UI_COLOR_PRIMARY);Lcd_draw_line(cx+2,cy,cx,cy+2,UI_COLOR_PRIMARY);}
}

void SnakeGame::draw_cell(Ui& ui,uint16_t cell) const{
    uint8_t x=cell_x(cell),y=cell_y(cell);
    if(map_obstacle(map_,x,y)){draw_wall_cell(ui,x,y);return;}
    uint16_t color=cell_color(cell);
    if(color!=UI_COLOR_BACKGROUND){ui.grid_cell(x,y,10U,color);return;}
    uint8_t belt=map_belt(map_,x,y);
    if(belt!=SNAKE_BELT_NONE){draw_belt_cell(ui,x,y,belt);}
    else{ui.grid_cell_clear(x,y,10U);}
}

void SnakeGame::draw(Ui& ui,bool full_redraw){
    uint32_t now=HAL_GetTick();
    if(full_redraw||full_dirty_){
        draw_game_frame(ui);
        for(uint8_t y=0U;y<Height;y++){for(uint8_t x=0U;x<Width;x++){draw_cell(ui,make_cell(x,y));}}
        full_dirty_=false;dirty_count_=0U;status_dirty_=true;
        lives_dirty_=true;
    }
    else{for(uint8_t i=0U;i<dirty_count_;i++){draw_cell(ui,dirty_[i]);}dirty_count_=0U;}
    if(status_dirty_){
        draw_game_status(ui,now);
        status_dirty_=false;
    }
    if(lives_dirty_){
        draw_game_lives(ui);
        lives_dirty_=false;
    }
}

uint16_t SnakeGame::make_snapshot(uint8_t* data,uint16_t capacity) const{
    SnakeSnapshot& snapshot=snake_snapshot_work;uint32_t now=HAL_GetTick();
    if(data==NULL||capacity<sizeof(snapshot)||!running_){return 0U;}
    snapshot.magic=SNAKE_SNAPSHOT_MAGIC;snapshot.version=SNAKE_SNAPSHOT_VERSION;snapshot.level=map_;snapshot.lives=lives_;
    snapshot.direction=(uint8_t)direction_;snapshot.queue_count=direction_queue_count_;
    for(uint8_t i=0U;i<2U;i++){snapshot.queue[i]=(uint8_t)direction_queue_[i];}
    snapshot.special_type=(uint8_t)special_type_;snapshot.completed=completed_?1U:0U;snapshot.forward_held=forward_held_?1U:0U;snapshot.reserved=0U;snapshot.length=length_;
    snapshot.growth_pending=growth_pending_;snapshot.normal_food_count=normal_food_count_;snapshot.food=food_;snapshot.special_food=special_food_;
    snapshot.score=score_;snapshot.special_remaining_ms=remaining_ms(now,special_deadline_ms_);snapshot.slow_remaining_ms=remaining_ms(now,slow_deadline_ms_);snapshot.recovery_remaining_ms=remaining_ms(now,recovery_deadline_ms_);
    for(uint16_t i=0U;i<length_;i++){snapshot.segments[i]=segments_[(uint16_t)((tail_index_+i)%MaxCells)];}
    for(uint16_t i=length_;i<MaxCells;i++){snapshot.segments[i]=InvalidCell;}
    const uint8_t* source=(const uint8_t*)&snapshot;for(uint16_t i=0U;i<sizeof(snapshot);i++){data[i]=source[i];}
    return sizeof(snapshot);
}

bool SnakeGame::restore(const uint8_t* data,uint16_t length){
    SnakeSnapshot& snapshot=snake_snapshot_work;uint8_t* target=(uint8_t*)&snapshot;
    if(data==NULL||length!=sizeof(snapshot)){return false;}
    for(uint16_t i=0U;i<sizeof(snapshot);i++){target[i]=data[i];}
    if(snapshot.magic!=SNAKE_SNAPSHOT_MAGIC||snapshot.version!=SNAKE_SNAPSHOT_VERSION||snapshot.level>=SNAKE_LEVEL_COUNT||snapshot.lives==0U||snapshot.lives>3U){return false;}
    if(snapshot.direction>3U||snapshot.queue_count>2U||snapshot.special_type>2U||snapshot.completed>1U||snapshot.forward_held>1U||snapshot.length<4U||snapshot.length>MaxCells||snapshot.food>=MaxCells||snapshot.growth_pending>MaxCells){return false;}
    if((snapshot.completed!=0U)!=(snapshot.score>=map_target(snapshot.level))){return false;}
    if(snapshot.special_remaining_ms>6000U||snapshot.slow_remaining_ms>5000U||snapshot.recovery_remaining_ms>SNAKE_RECOVERY_MS){return false;}
    for(uint16_t i=0U;i<snapshot.length;i++){
        uint16_t cell=snapshot.segments[i];
        if(cell>=MaxCells||map_obstacle(snapshot.level,cell_x(cell),cell_y(cell))){return false;}
        for(uint16_t j=0U;j<i;j++){if(snapshot.segments[j]==cell){return false;}}
    }
    if(map_obstacle(snapshot.level,cell_x(snapshot.food),cell_y(snapshot.food))||map_belt(snapshot.level,cell_x(snapshot.food),cell_y(snapshot.food))!=SNAKE_BELT_NONE){return false;}
    if(snapshot.special_food!=InvalidCell){
        if(snapshot.special_food>=MaxCells||snapshot.special_type==0U||snapshot.special_food==snapshot.food||map_obstacle(snapshot.level,cell_x(snapshot.special_food),cell_y(snapshot.special_food))||map_belt(snapshot.level,cell_x(snapshot.special_food),cell_y(snapshot.special_food))!=SNAKE_BELT_NONE){return false;}
    }
    else if(snapshot.special_type!=0U){return false;}
    for(uint16_t i=0U;i<snapshot.length;i++){if(snapshot.segments[i]==snapshot.food||snapshot.segments[i]==snapshot.special_food){return false;}}
    map_=snapshot.level;lives_=snapshot.lives;direction_=(Direction)snapshot.direction;direction_queue_count_=snapshot.queue_count;
    for(uint8_t i=0U;i<2U;i++){if(snapshot.queue[i]>3U){return false;}direction_queue_[i]=(Direction)snapshot.queue[i];}
    special_type_=(SpecialFood)snapshot.special_type;completed_=snapshot.completed!=0U;length_=snapshot.length;tail_index_=0U;growth_pending_=snapshot.growth_pending;
    normal_food_count_=snapshot.normal_food_count;food_=snapshot.food;special_food_=snapshot.special_food;score_=snapshot.score;
    for(uint16_t i=0U;i<length_;i++){segments_[i]=snapshot.segments[i];}
    uint32_t now=HAL_GetTick();special_deadline_ms_=snapshot.special_remaining_ms!=0U?now+snapshot.special_remaining_ms:0U;slow_deadline_ms_=snapshot.slow_remaining_ms!=0U?now+snapshot.slow_remaining_ms:0U;recovery_deadline_ms_=snapshot.recovery_remaining_ms!=0U?now+snapshot.recovery_remaining_ms:0U;
    last_step_ms_=now;running_=true;forward_held_=snapshot.forward_held!=0U;pending_signal_=GameSignal::None;full_dirty_=true;status_dirty_=true;lives_dirty_=true;dirty_count_=0U;
    return true;
}

GameResult SnakeGame::result() const{
    GameOutcome outcome=completed_?GameOutcome::LevelComplete:(!running_?GameOutcome::Failure:GameOutcome::InProgress);
    GameResult value={score_,length_,map_target(map_),map_,lives_,outcome,UiTextId::Score,UiTextId::Length,1U};
    return value;
}

UiTextId SnakeGame::display_name() const{return UiTextId::Snake;}
UiTextId SnakeGame::menu_goal_text() const{return UiTextId::GoalSnake;}
void SnakeGame::format_menu_mode(char* text,uint8_t capacity) const{
    if(text==NULL||capacity<9U){if(text!=NULL&&capacity!=0U){text[0]='\0';}return;}
    const char prefix[]="LEVEL ";for(uint8_t i=0U;i<6U;i++){text[i]=prefix[i];}format_u32((uint32_t)map_+1U,&text[6]);
}
uint8_t SnakeGame::menu_control_count() const{return 2U;}
UiTextId SnakeGame::menu_control_text(uint8_t index) const{return index==0U?UiTextId::ControlSnakeTurn:UiTextId::ControlSnakeBoost;}

void SnakeGame::draw_home_thumbnail(Ui& ui,uint16_t x,uint16_t y,uint16_t width,uint16_t height) const{
    if(width<72U||height<72U){return;}
    int16_t center_x=(int16_t)(x+width/2U),center_y=(int16_t)(y+height/2U);
    auto draw_scaled=[&ui,center_x,center_y](uint8_t local_x,uint8_t local_y,uint8_t local_width,uint8_t local_height,uint16_t color){
        uint16_t scaled_width=(uint16_t)(((uint16_t)local_width*6U+2U)/5U),scaled_height=(uint16_t)(((uint16_t)local_height*6U+2U)/5U);
        int16_t local_center_x=(int16_t)local_x+(int16_t)(local_width/2U),local_center_y=(int16_t)local_y+(int16_t)(local_height/2U);
        int16_t target_x=(int16_t)(center_x+(local_center_x-36)*6/5-(int16_t)(scaled_width/2U));
        int16_t target_y=(int16_t)(center_y+(local_center_y-36)*6/5-(int16_t)(scaled_height/2U));
        ui.fill((uint16_t)target_x,(uint16_t)target_y,scaled_width,scaled_height,color);
    };
    draw_scaled(6U,8U,24U,6U,UI_COLOR_WALL);
    draw_scaled(52U,52U,14U,6U,UI_COLOR_WALL);
    draw_scaled(56U,12U,7U,7U,UI_COLOR_FOOD);
    static const uint8_t pixels[][2]={{0U,4U},{1U,4U},{2U,4U},{3U,4U},{3U,3U},{3U,2U},{4U,2U},{5U,2U}};
    for(uint8_t i=0U;i<sizeof(pixels)/sizeof(pixels[0]);i++){draw_scaled((uint8_t)(10U+pixels[i][0]*8U),(uint8_t)(20U+pixels[i][1]*8U),7U,7U,i+1U==sizeof(pixels)/sizeof(pixels[0])?UI_COLOR_SNAKE_HEAD:UI_COLOR_SNAKE);}
}

void SnakeGame::draw_entry_page(Ui& ui,const GameActivity& activity,const GameOverview& overview) const{
    static const uint8_t scene_snake[][2]={{1U,7U},{2U,7U},{3U,7U},{4U,7U},{5U,7U},{5U,6U},{5U,5U},{6U,5U},{7U,5U},{8U,5U},{8U,4U},{8U,3U},{7U,3U},{6U,3U}};
    char value[16];
    ui.fill(16U,38U,288U,126U,UI_COLOR_PANEL);Lcd_draw_rect(16U,38U,288U,126U,UI_COLOR_LINE);
    ui.fill(27U,49U,88U,88U,UI_COLOR_BACKGROUND);Lcd_draw_rect(27U,49U,88U,88U,UI_COLOR_LINE);
    ui.fill(35U,57U,12U,6U,UI_COLOR_WALL);ui.fill(91U,105U,16U,6U,UI_COLOR_WALL);ui.fill(98U,63U,7U,7U,UI_COLOR_FOOD);
    for(uint8_t i=0U;i<sizeof(scene_snake)/sizeof(scene_snake[0]);i++){ui.fill((uint16_t)(31U+scene_snake[i][0]*8U),(uint16_t)(53U+scene_snake[i][1]*8U),7U,7U,i==sizeof(scene_snake)/sizeof(scene_snake[0])-1U?UI_COLOR_SNAKE_HEAD:UI_COLOR_SNAKE);}
    ui.text(132U,45U,UiTextId::Snake,UI_COLOR_PRIMARY,UI_COLOR_PANEL,24U);
    ui.text(132U,78U,UiTextId::Unlocked,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    uint8_t index=0U;value[0]='\0';snake_append_u32(value,&index,overview.unlocked_count);value[index++]='/';snake_append_u32(value,&index,overview.variant_count);ui.ascii_fit(240U,78U,56U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(132U,101U,UiTextId::HighScore,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);index=0U;value[0]='\0';snake_append_u32(value,&index,overview.high_score);ui.ascii_fit(240U,101U,56U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(132U,124U,UiTextId::HighLength,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);index=0U;value[0]='\0';snake_append_u32(value,&index,overview.high_progress);ui.ascii_fit(240U,124U,56U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(28U,143U,UiTextId::RecentPlay,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    if(activity.valid!=0U){format_activity(activity,value);ui.ascii_fit(100U,143U,136U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);}else{ui.text(100U,143U,UiTextId::NoSave,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);}
}

void SnakeGame::draw_entry_control(Ui& ui,uint8_t control,bool selected,bool snapshot_available) const{
    if(control==0U){ui.large_button(16U,180U,140U,snapshot_available?UiTextId::ContinueGame:UiTextId::StartGame,selected);}
    else if(control==1U){ui.large_button(164U,180U,140U,UiTextId::SelectLevel,selected);}
}
