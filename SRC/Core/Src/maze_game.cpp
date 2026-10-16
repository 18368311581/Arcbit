#include "maze_game.hpp"
#include "basic.h"
#include "lcd.h"
#include "save.hpp"
#include "ui.hpp"
#include "ui_sound.hpp"

#define MAZE_DATA_VERSION 1U

namespace {

#pragma pack(push,1)
struct MazePersistentData {uint32_t fastest_seconds[MazeRules::VariantCount];};
#pragma pack(pop)

struct ProjectedObject {
    int16_t center_x;
    int16_t top;
    int16_t bottom;
    int16_t half_width;
    uint16_t depth_q8;
    uint16_t color;
    uint8_t kind;
    uint8_t visible;
};

static_assert(sizeof(MazePersistentData)==24U,"Maze persistent format changed");
static_assert(sizeof(MazePersistentData)<=SAVE_GAME_DATA_CAPACITY,"Maze data exceeds game slot");
static_assert(sizeof(MazeSnapshot)<=SAVE_SNAPSHOT_CAPACITY,"Maze snapshot exceeds save capacity");

static MazeSnapshot snapshot_work;
static MazeState snapshot_state_work;

static constexpr uint8_t StatusMap=1U<<0;
static constexpr uint8_t StatusKey=1U<<1;
static constexpr uint8_t StatusTime=1U<<2;
static constexpr uint8_t StatusMapObjects=1U<<3;
static constexpr uint8_t StatusAll=StatusMap|StatusKey|StatusTime|StatusMapObjects;
static constexpr uint16_t MotionKeys=KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT|KEY_Y|KEY_X|KEY_A|KEY_B;
static constexpr uint16_t ViewWidth=240U;
static constexpr uint16_t ViewHeight=240U;
static constexpr uint16_t Horizon=120U;
static constexpr uint16_t RayWidth=2U;
static constexpr uint16_t RayCount=120U;
static constexpr uint16_t ProjectionFocal=143U;
static constexpr int16_t CameraPlaneQ15=27496;
static constexpr uint16_t PlayerHeightQ8=MazeRules::CoordinateOne/2U;
static constexpr uint16_t CameraHeightQ8=PlayerHeightQ8;
static constexpr uint8_t WallHeightWidthMultiple=3U;
static constexpr uint32_t WallHeightQ8=(uint32_t)MazeRules::CoordinateOne*WallHeightWidthMultiple;
static constexpr uint32_t TurnUnitsPerSecond=50972UL;
static constexpr uint16_t PitchDegreesPerSecond=90U;
static constexpr int16_t PitchLimitDegrees=75;
static constexpr uint32_t ViewBobUnitsPerSecond=98304UL;
static constexpr int8_t ViewBobAmplitudePixels=2;
static constexpr uint16_t MinimumTapMs=16U;
static constexpr uint16_t KeyHeightQ8=128U;
static constexpr uint16_t KeyColor=0xFFE0U;
static constexpr uint16_t ExitLockedColor=0xF800U;
static constexpr uint16_t ExitReadyColor=0xFFE0U;
static uint16_t ray_depth_q8[RayCount];

static constexpr uint16_t rgb565(uint8_t red,uint8_t green,uint8_t blue){return (uint16_t)(((uint16_t)(red>>3)<<11)|((uint16_t)(green>>2)<<5)|(blue>>3));}

static const uint16_t wall_dark[4]={rgb565(112U,118U,124U),rgb565(142U,147U,152U),rgb565(92U,98U,104U),rgb565(126U,132U,138U)};

static uint16_t shade_color(uint16_t color,uint8_t percent){
    uint32_t red=(uint32_t)((color>>11)&0x1FU)*percent/100U,green=(uint32_t)((color>>5)&0x3FU)*percent/100U,blue=(uint32_t)(color&0x1FU)*percent/100U;
    return (uint16_t)((red<<11)|(green<<5)|blue);
}

static uint16_t wall_color(const MazeState& state,const MazeRayHit& hit){
    (void)state;uint8_t direction=hit.face_direction<4U?hit.face_direction:0U;return wall_dark[direction];
}

static uint8_t color_channel(uint8_t start,uint8_t end,uint16_t position,uint16_t span){return (uint8_t)((int32_t)start+((int32_t)end-start)*position/span);}

static uint16_t floor_color(uint8_t hue){
    uint8_t red,green,blue;if(hue<32U){red=color_channel(68U,34U,hue,31U);green=172U;blue=color_channel(56U,100U,hue,31U);}
    else if(hue<144U){uint16_t position=(uint16_t)(hue-32U);red=color_channel(34U,28U,position,111U);green=color_channel(172U,166U,position,111U);blue=color_channel(100U,172U,position,111U);}
    else{uint16_t position=(uint16_t)(hue-144U);red=color_channel(28U,36U,position,111U);green=color_channel(166U,116U,position,111U);blue=172U;}return rgb565(red,green,blue);
}

static int8_t view_bob_offset(uint16_t phase){int32_t value=(int32_t)math_sin_q15(phase)*ViewBobAmplitudePixels;value+=value>=0?16384:-16384;return (int8_t)(value/32768);}

static int32_t view_horizon(const MazeState& state,int8_t bob_offset){int32_t degrees=state.view_pitch_degrees;if(degrees<0){degrees+=360;}uint16_t angle=(uint16_t)((degrees*65536L+359L)/360L);int32_t cosine=math_cos_q15(angle);if(cosine<1){cosine=1;}return (int32_t)Horizon+(int32_t)ProjectionFocal*math_sin_q15(angle)/cosine+bob_offset;}

static uint16_t map_cell_x(uint16_t cell,uint8_t width){return (uint16_t)(252U+(uint32_t)MazeRules::cell_x(cell,width)*55U/(width-1U));}
static uint16_t map_cell_y(uint16_t cell,uint8_t width){return (uint16_t)(12U+(uint32_t)MazeRules::cell_y(cell,width)*55U/(width-1U));}

static uint16_t map_player_axis(uint16_t position_q8,uint8_t width,uint16_t origin){
    uint16_t first=384U,span=(uint16_t)((width-1U)*512U),offset=position_q8<=first?0U:(position_q8>=first+span?span:(uint16_t)(position_q8-first));
    return (uint16_t)(origin+(uint32_t)offset*55U/span);
}

static void clear_map_marker(Ui& ui,uint16_t x,uint16_t y){ui.fill((uint16_t)(x-3U),(uint16_t)(y-3U),7U,7U,UI_COLOR_PANEL);}

static bool map_markers_overlap(uint16_t first_x,uint16_t first_y,uint16_t second_x,uint16_t second_y){
    uint16_t dx=first_x>second_x?(uint16_t)(first_x-second_x):(uint16_t)(second_x-first_x),dy=first_y>second_y?(uint16_t)(first_y-second_y):(uint16_t)(second_y-first_y);return dx<=6U&&dy<=6U;
}

static void draw_map_entry(uint16_t x,uint16_t y){
    Lcd_draw_line((int16_t)(x-2U),(int16_t)(y+2U),(int16_t)(x-2U),(int16_t)(y-2U),UI_COLOR_PRIMARY);Lcd_draw_line((int16_t)(x-2U),(int16_t)(y-2U),(int16_t)(x+2U),(int16_t)(y-2U),UI_COLOR_PRIMARY);Lcd_draw_line((int16_t)(x+2U),(int16_t)(y-2U),(int16_t)(x+2U),(int16_t)(y+2U),UI_COLOR_PRIMARY);
}

static void draw_map_key(Ui& ui,uint16_t x,uint16_t y){
    ui.filled_circle((int16_t)(x-1U),(int16_t)(y-1U),1U,UI_COLOR_GOLD);Lcd_draw_line((int16_t)x,(int16_t)y,(int16_t)(x+2U),(int16_t)(y+2U),UI_COLOR_GOLD);ui.fill((uint16_t)(x+1U),(uint16_t)(y+2U),2U,1U,UI_COLOR_GOLD);
}

static void draw_map_exit(Ui& ui,uint16_t x,uint16_t y,bool unlocked){
    uint16_t color=unlocked?UI_COLOR_FOCUS:UI_COLOR_DANGER;Lcd_draw_circle((int16_t)x,(int16_t)y,3U,color);ui.filled_circle((int16_t)x,(int16_t)y,1U,color);
}

static void draw_map_player(Ui& ui,uint16_t x,uint16_t y){ui.filled_circle((int16_t)x,(int16_t)y,2U,UI_COLOR_TEXT);}

static ProjectedObject project_object(const MazeState& state,uint16_t cell,int16_t facing_x,int16_t facing_y,int32_t horizon,uint16_t height_q8,uint8_t kind,uint16_t color){
    ProjectedObject object={0,0,0,0,0U,color,kind,0U};uint8_t width=MazeRules::width_for_variant(state.variant);int32_t relative_x=(int32_t)MazeRules::cell_center_x_q8(cell,width)-state.player_x_q8,relative_y=(int32_t)MazeRules::cell_center_y_q8(cell,width)-state.player_y_q8;
    int32_t depth=(relative_x*facing_x+relative_y*facing_y)/32768,lateral=(-relative_x*facing_y+relative_y*facing_x)/32768;if(depth<=16||depth>0xFFFF){return object;}
    int32_t center_x=(int32_t)ViewWidth/2+lateral*ProjectionFocal/depth,ground=horizon+(int32_t)ProjectionFocal*CameraHeightQ8/depth,height=(int32_t)ProjectionFocal*height_q8/depth;if(height<4){height=4;}if(height>160){height=160;}int32_t projected_top=ground-height;if(ground<0||projected_top>=(int32_t)ViewHeight){return object;}
    int32_t half_width=height/2;if(center_x+half_width<0||center_x-half_width>=(int32_t)ViewWidth){return object;}
    object.center_x=(int16_t)center_x;object.bottom=(int16_t)(ground>239?239:ground);object.top=(int16_t)(projected_top<0?0:projected_top);object.half_width=(int16_t)(half_width<2?2:half_width);object.depth_q8=(uint16_t)depth;object.visible=1U;return object;
}

static void draw_projected_object(const ProjectedObject& object){
    if(object.visible==0U||object.bottom<object.top){return;}int16_t left=(int16_t)(object.center_x-object.half_width),right=(int16_t)(object.center_x+object.half_width);if(left<0){left=0;}if(right>=ViewWidth){right=(int16_t)(ViewWidth-1U);}
    uint16_t inner=shade_color(object.color,58U);
    for(int16_t x=left;x<=right;x=(int16_t)(x+RayWidth)){
        uint16_t stripe=(uint16_t)x/RayWidth;if(stripe>=RayCount||object.depth_q8>=ray_depth_q8[stripe]){continue;}int16_t top=object.top,bottom=object.bottom;
        int16_t distance=x>object.center_x?(int16_t)(x-object.center_x):(int16_t)(object.center_x-x);if(object.kind==0U){int16_t cut=(int16_t)((int32_t)distance*(bottom-top)/(object.half_width+1));top=(int16_t)(top+cut/2);bottom=(int16_t)(bottom-cut/2);}else{static const uint8_t sphere_profile[9]={8U,8U,8U,7U,7U,6U,5U,3U,1U};uint8_t index=(uint8_t)((int32_t)distance*8/(object.half_width+1));if(index>8U){index=8U;}int16_t center=(int16_t)((top+bottom)/2),radius=(int16_t)((bottom-top+1)/2),slice=(int16_t)((int32_t)radius*sphere_profile[index]/8);top=(int16_t)(center-slice);bottom=(int16_t)(center+slice);}
        uint16_t color=object.kind==1U&&(x<=left+RayWidth||x+RayWidth>=right)?inner:object.color;if(bottom>=top){Lcd_fill_rect((uint16_t)x,(uint16_t)top,(uint16_t)((x+RayWidth<=ViewWidth)?RayWidth:(ViewWidth-x)),(uint16_t)(bottom-top+1),color);}
    }
}

}

MazeGame::MazeGame():last_update_ms_(0U),last_motion_ms_(0U),drawn_seconds_(0U),held_motion_keys_(0U),pressed_motion_keys_(0U),view_bob_phase_(0U),drawn_map_player_x_(0U),drawn_map_player_y_(0U),status_dirty_(StatusAll),view_bob_offset_(0),select_focus_(0U),timer_running_(false),full_dirty_(true),view_dirty_(false),map_drawn_(false),exit_contact_(false),finish_pending_(false),generation_failed_(false){
    for(uint16_t i=0U;i<MazeRules::MaxCells;i++){state_.walls[i]=0U;}state_.elapsed_ms=0U;state_.variant=default_variant();state_.loop_count=MazeRules::loops_for_variant(state_.variant);state_.key_collected=0U;state_.gradient_mode=0U;state_.floor_palette=0U;state_.entry_cell=0U;state_.key_cell=1U;state_.exit_cell=2U;state_.player_cell=0U;state_.player_x_q8=384U;state_.player_y_q8=384U;state_.view_angle=0U;state_.view_pitch_degrees=0;for(uint8_t i=0U;i<MazeRules::MaxLoopCount;i++){state_.loop_edges[i]=0U;state_.loop_lengths[i]=0U;}persistent_defaults();
}

GameId MazeGame::id() const{return GameId::Maze;}
const char* MazeGame::title() const{return "3D Maze";}
uint8_t MazeGame::variant_count() const{return MazeRules::VariantCount;}
uint8_t MazeGame::default_variant() const{return 1U;}
const char* MazeGame::variant_page_title() const{return "Maze Settings";}
UiTextId MazeGame::variant_action_text() const{return UiTextId::AdjustDifficulty;}
uint16_t MazeGame::persistent_data_version() const{return MAZE_DATA_VERSION;}

void MazeGame::persistent_defaults(){for(uint8_t i=0U;i<MazeRules::VariantCount;i++){fastest_seconds_[i]=0U;}}

bool MazeGame::persistent_data_valid(const uint8_t* data,uint16_t length,uint16_t version) const{
    if(data==NULL||length!=sizeof(MazePersistentData)||version!=MAZE_DATA_VERSION){return false;}for(uint8_t variant=0U;variant<MazeRules::VariantCount;variant++){uint8_t offset=(uint8_t)(variant*4U);uint32_t value=(uint32_t)data[offset]|((uint32_t)data[offset+1U]<<8)|((uint32_t)data[offset+2U]<<16)|((uint32_t)data[offset+3U]<<24);if(value>UINT32_MAX/1000U){return false;}}return true;
}

void MazeGame::load_persistent_data(const uint8_t* data,uint16_t length){if(!persistent_data_valid(data,length,MAZE_DATA_VERSION)){return;}for(uint8_t variant=0U;variant<MazeRules::VariantCount;variant++){uint8_t offset=(uint8_t)(variant*4U);fastest_seconds_[variant]=(uint32_t)data[offset]|((uint32_t)data[offset+1U]<<8)|((uint32_t)data[offset+2U]<<16)|((uint32_t)data[offset+3U]<<24);}}

uint16_t MazeGame::make_persistent_data(uint8_t* data,uint16_t capacity) const{if(data==NULL||capacity<sizeof(MazePersistentData)){return 0U;}for(uint8_t variant=0U;variant<MazeRules::VariantCount;variant++){uint8_t offset=(uint8_t)(variant*4U);uint32_t value=fastest_seconds_[variant];data[offset]=(uint8_t)value;data[offset+1U]=(uint8_t)(value>>8);data[offset+2U]=(uint8_t)(value>>16);data[offset+3U]=(uint8_t)(value>>24);}return sizeof(MazePersistentData);}

GameOverview MazeGame::persistent_overview(uint8_t selected_variant) const{uint8_t variant=selected_variant<MazeRules::VariantCount?selected_variant:default_variant();return {fastest_seconds_[variant],0U,MazeRules::VariantCount,MazeRules::VariantCount,variant,{0U,0U,0U}};}

GameRecordSummary MazeGame::record_summary(const uint8_t* data,uint16_t length,uint16_t version) const{GameRecordSummary summary={0U,0U,0U,1U,0U};if(!persistent_data_valid(data,length,version)){return summary;}const MazePersistentData& value=*(const MazePersistentData*)data;for(uint8_t i=0U;i<MazeRules::VariantCount;i++){uint32_t seconds=value.fastest_seconds[i];if(seconds!=0U&&(summary.valid==0U||seconds<summary.value)){summary.value=seconds;summary.variant=i;summary.valid=1U;}}return summary;}

void MazeGame::format_record_variant(uint8_t variant,char* text,uint8_t capacity) const{mode_text(variant,text,capacity);}
void MazeGame::apply_result(const GameResult& value,bool* new_record,bool* new_unlock){bool record=MazeRules::update_fastest_time(fastest_seconds_,value.variant,value.score,value.outcome==GameOutcome::PlayerVictory);if(new_record!=NULL){*new_record=record;}if(new_unlock!=NULL){*new_unlock=false;}}

void MazeGame::start_new(uint8_t variant){
    generation_failed_=!MazeRules::generate(&state_,variant<MazeRules::VariantCount?variant:default_variant());last_update_ms_=0U;last_motion_ms_=0U;drawn_seconds_=0U;held_motion_keys_=0U;pressed_motion_keys_=0U;view_bob_phase_=0U;drawn_map_player_x_=0U;drawn_map_player_y_=0U;status_dirty_=StatusAll;view_bob_offset_=0;timer_running_=false;full_dirty_=true;view_dirty_=false;map_drawn_=false;exit_contact_=false;finish_pending_=false;
}

void MazeGame::accumulate_time(uint32_t now_ms){
    if(!timer_running_||finish_pending_||generation_failed_){return;}uint32_t previous=state_.elapsed_ms/1000U;MazeRules::add_elapsed_ms(&state_,now_ms-last_update_ms_);last_update_ms_=now_ms;if(state_.elapsed_ms/1000U!=previous){status_dirty_|=StatusTime;}
}

void MazeGame::handle_input(const InputEvent& input){
    if(finish_pending_||generation_failed_){held_motion_keys_=0U;pressed_motion_keys_=0U;return;}held_motion_keys_=(uint16_t)(input.state&MotionKeys);pressed_motion_keys_|=(uint16_t)(input.pressed&MotionKeys);
}

void MazeGame::update_motion(uint32_t now_ms){
    if(last_motion_ms_==0U){last_motion_ms_=now_ms;pressed_motion_keys_=0U;return;}uint32_t raw_elapsed=now_ms-last_motion_ms_;last_motion_ms_=now_ms;uint16_t pressed=pressed_motion_keys_;pressed_motion_keys_=0U;
    if(raw_elapsed==0U&&pressed==0U){return;}uint16_t elapsed=(uint16_t)(raw_elapsed>250U?250U:raw_elapsed);if(pressed!=0U&&elapsed<MinimumTapMs){elapsed=MinimumTapMs;}
    uint16_t active=held_motion_keys_,forward_held=(uint16_t)(active&(KEY_UP|KEY_DOWN)),strafe_held=(uint16_t)(active&(KEY_LEFT|KEY_RIGHT)),turn_held=(uint16_t)(active&(KEY_Y|KEY_X)),pitch_held=(uint16_t)(active&(KEY_A|KEY_B));int8_t forward=forward_held==KEY_UP?1:(forward_held==KEY_DOWN?-1:0),strafe=strafe_held==KEY_RIGHT?1:(strafe_held==KEY_LEFT?-1:0),turn=turn_held==KEY_X?1:(turn_held==KEY_Y?-1:0),pitch=pitch_held==KEY_A?1:(pitch_held==KEY_B?-1:0);
    if(forward_held==0U){uint16_t edge=(uint16_t)(pressed&(KEY_UP|KEY_DOWN));forward=edge==KEY_UP?1:(edge==KEY_DOWN?-1:0);}if(strafe_held==0U){uint16_t edge=(uint16_t)(pressed&(KEY_LEFT|KEY_RIGHT));strafe=edge==KEY_RIGHT?1:(edge==KEY_LEFT?-1:0);}if(turn_held==0U){uint16_t edge=(uint16_t)(pressed&(KEY_Y|KEY_X));turn=edge==KEY_X?1:(edge==KEY_Y?-1:0);}if(pitch_held==0U){uint16_t edge=(uint16_t)(pressed&(KEY_A|KEY_B));pitch=edge==KEY_A?1:(edge==KEY_B?-1:0);}
    bool turned=false;if(turn!=0){uint32_t delta=TurnUnitsPerSecond*elapsed/1000U;if(delta==0U){delta=1U;}state_.view_angle=(uint16_t)(state_.view_angle+(int32_t)turn*delta);turned=true;}if(pitch!=0){int32_t delta=(int32_t)PitchDegreesPerSecond*elapsed/1000;if(delta==0){delta=1;}int32_t next=(int32_t)state_.view_pitch_degrees+pitch*delta;if(next>PitchLimitDegrees){next=PitchLimitDegrees;}if(next<-PitchLimitDegrees){next=-PitchLimitDegrees;}if(next!=state_.view_pitch_degrees){state_.view_pitch_degrees=(int16_t)next;turned=true;}}
    int16_t facing_x=math_cos_q15(state_.view_angle),facing_y=math_sin_q15(state_.view_angle);MazeMotionResult movement=MazeRules::move_continuous(&state_,forward,strafe,facing_x,facing_y,elapsed);bool moved=movement.moved!=0U;
    if(moved){view_bob_phase_=(uint16_t)(view_bob_phase_+ViewBobUnitsPerSecond*elapsed/1000U);view_bob_offset_=view_bob_offset(view_bob_phase_);}
    else if(view_bob_offset_!=0){view_bob_phase_=0U;view_bob_offset_=0;view_dirty_=true;}
    if(turned||moved){view_dirty_=true;if(moved){status_dirty_|=StatusMap;}}
    bool on_locked_exit=state_.player_cell==state_.exit_cell&&state_.key_collected==0U;
    if(movement.event==MazeMoveResult::KeyCollected){status_dirty_|=(uint8_t)(StatusKey|StatusMap|StatusMapObjects);exit_contact_=false;UiSound_play(UiSoundId::Unlock);}
    else if(movement.event==MazeMoveResult::ExitLocked){if(!exit_contact_){UiSound_play(UiSoundId::Error);}exit_contact_=true;}
    else if(movement.event==MazeMoveResult::Finished){finish_pending_=true;}
    else{if(!on_locked_exit){exit_contact_=false;}if(turned&&(pressed&(KEY_Y|KEY_X|KEY_A|KEY_B))!=0U){UiSound_play(UiSoundId::Turn);}else if(moved&&(pressed&(KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT))!=0U){UiSound_play(UiSoundId::Move);}}
}

GameSignal MazeGame::update(uint32_t now_ms){
    if(generation_failed_){return GameSignal::GameOver;}accumulate_time(now_ms);update_motion(now_ms);if(view_dirty_||status_dirty_!=0U){return GameSignal::NeedsDraw;}if(finish_pending_){return GameSignal::GameOver;}return GameSignal::None;
}

void MazeGame::draw_view(Ui& ui) const{
    (void)ui;uint16_t sky=0xFFFFU,floor=floor_color(state_.floor_palette);int32_t horizon=view_horizon(state_,view_bob_offset_);
    int16_t facing_x=math_cos_q15(state_.view_angle),facing_y=math_sin_q15(state_.view_angle);int32_t plane_x=-(int32_t)facing_y*CameraPlaneQ15/32768,plane_y=(int32_t)facing_x*CameraPlaneQ15/32768;
    for(uint16_t stripe=0U;stripe<RayCount;stripe++){
        int32_t camera_x=((int32_t)(stripe*2U+1U)*32768/RayCount)-32768,ray_x=(int32_t)facing_x+plane_x*camera_x/32768,ray_y=(int32_t)facing_y+plane_y*camera_x/32768;MazeRayHit hit=MazeRules::cast_ray(state_,ray_x,ray_y);ray_depth_q8[stripe]=hit.distance_q8;
        uint32_t distance=hit.distance_q8<16U?16U:hit.distance_q8,bottom_offset=(uint32_t)ProjectionFocal*CameraHeightQ8/distance,top_offset=(uint32_t)ProjectionFocal*(WallHeightQ8-CameraHeightQ8)/distance;int32_t projected_top=horizon-(int32_t)top_offset,projected_bottom=horizon+(int32_t)bottom_offset;uint16_t column_x=(uint16_t)(stripe*RayWidth);bool wall_visible=projected_bottom>=0&&projected_top<(int32_t)ViewHeight;
        if(wall_visible){int32_t top=projected_top<0?0:projected_top,bottom=projected_bottom>=(int32_t)ViewHeight?(int32_t)ViewHeight-1:projected_bottom;if(top>0){Lcd_fill_rect(column_x,0U,RayWidth,(uint16_t)top,sky);}Lcd_fill_rect(column_x,(uint16_t)top,RayWidth,(uint16_t)(bottom-top+1),wall_color(state_,hit));if(bottom+1<(int32_t)ViewHeight){Lcd_fill_rect(column_x,(uint16_t)(bottom+1),RayWidth,(uint16_t)(ViewHeight-bottom-1),floor);}}
        else if(horizon<=0){Lcd_fill_rect(column_x,0U,RayWidth,ViewHeight,floor);}
        else if(horizon>=(int32_t)ViewHeight){Lcd_fill_rect(column_x,0U,RayWidth,ViewHeight,sky);}
        else{Lcd_fill_rect(column_x,0U,RayWidth,(uint16_t)horizon,sky);Lcd_fill_rect(column_x,(uint16_t)horizon,RayWidth,(uint16_t)(ViewHeight-horizon),floor);}
    }
    ProjectedObject key=project_object(state_,state_.key_cell,facing_x,facing_y,horizon,KeyHeightQ8,0U,KeyColor),exit=project_object(state_,state_.exit_cell,facing_x,facing_y,horizon,KeyHeightQ8,1U,state_.key_collected!=0U?ExitReadyColor:ExitLockedColor);if(state_.key_collected!=0U){key.visible=0U;}
    if(key.visible!=0U&&exit.visible!=0U&&key.depth_q8<exit.depth_q8){draw_projected_object(exit);draw_projected_object(key);}else{draw_projected_object(key);draw_projected_object(exit);}
}

void MazeGame::draw_map(Ui& ui,bool full_redraw,bool objects_dirty){
    uint8_t width=MazeRules::width_for_variant(state_.variant);uint16_t entry_x=map_cell_x(state_.entry_cell,width),entry_y=map_cell_y(state_.entry_cell,width),key_x=map_cell_x(state_.key_cell,width),key_y=map_cell_y(state_.key_cell,width),exit_x=map_cell_x(state_.exit_cell,width),exit_y=map_cell_y(state_.exit_cell,width),player_x=map_player_axis(state_.player_x_q8,width,252U),player_y=map_player_axis(state_.player_y_q8,width,12U);
    if(full_redraw||!map_drawn_){
        ui.fill(247U,7U,66U,66U,UI_COLOR_PANEL);Lcd_draw_rect(248U,8U,64U,64U,UI_COLOR_LINE);draw_map_entry(entry_x,entry_y);if(state_.key_collected==0U){draw_map_key(ui,key_x,key_y);}draw_map_exit(ui,exit_x,exit_y,state_.key_collected!=0U);
    }
    else{
        clear_map_marker(ui,drawn_map_player_x_,drawn_map_player_y_);
        if(objects_dirty){
            clear_map_marker(ui,key_x,key_y);clear_map_marker(ui,exit_x,exit_y);draw_map_entry(entry_x,entry_y);if(state_.key_collected==0U){draw_map_key(ui,key_x,key_y);}draw_map_exit(ui,exit_x,exit_y,state_.key_collected!=0U);
        }
        else{
            if(map_markers_overlap(drawn_map_player_x_,drawn_map_player_y_,entry_x,entry_y)){draw_map_entry(entry_x,entry_y);}
            if(state_.key_collected==0U&&map_markers_overlap(drawn_map_player_x_,drawn_map_player_y_,key_x,key_y)){draw_map_key(ui,key_x,key_y);}
            if(map_markers_overlap(drawn_map_player_x_,drawn_map_player_y_,exit_x,exit_y)){draw_map_exit(ui,exit_x,exit_y,state_.key_collected!=0U);}
        }
    }
    draw_map_player(ui,player_x,player_y);drawn_map_player_x_=player_x;drawn_map_player_y_=player_y;map_drawn_=true;
}

void MazeGame::draw_status(Ui& ui,bool full_redraw){
    char value[16];if(full_redraw){ui.fill(240U,0U,80U,240U,UI_COLOR_PANEL);ui.fill(239U,0U,1U,240U,UI_COLOR_LINE);ui.text(246U,122U,UiTextId::MazeKey,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);ui.text(246U,174U,UiTextId::ElapsedSeconds,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);size_text(state_.variant,value);ui.ascii_fit(246U,88U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);status_dirty_=StatusAll;}
    if(MazeRules::map_enabled(state_.variant)&&(status_dirty_&(uint8_t)(StatusMap|StatusMapObjects))!=0U){draw_map(ui,full_redraw,(status_dirty_&StatusMapObjects)!=0U);}else if(full_redraw){map_drawn_=false;}
    if((status_dirty_&StatusKey)!=0U){ui.fill(246U,142U,70U,16U,UI_COLOR_PANEL);ui.text(246U,142U,state_.key_collected!=0U?UiTextId::KeyFound:UiTextId::KeyMissing,state_.key_collected!=0U?UI_COLOR_FOCUS:UI_COLOR_MUTED,UI_COLOR_PANEL,16U);}
    if((status_dirty_&StatusTime)!=0U){ui.fill(246U,194U,70U,16U,UI_COLOR_PANEL);drawn_seconds_=state_.elapsed_ms/1000U;format_u32(drawn_seconds_,value);ui.ascii_fit(246U,194U,70U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);}
    status_dirty_=0U;
}

void MazeGame::draw(Ui& ui,bool full_redraw){
    bool draw_scene=full_redraw||full_dirty_||view_dirty_;if(draw_scene){draw_view(ui);}draw_status(ui,full_redraw||full_dirty_);full_dirty_=false;view_dirty_=false;
    if(!timer_running_&&!generation_failed_){last_update_ms_=HAL_GetTick();last_motion_ms_=last_update_ms_;timer_running_=true;}
}

uint16_t MazeGame::make_snapshot(uint8_t* data,uint16_t capacity) const{
    if(data==NULL||capacity<sizeof(MazeSnapshot)||generation_failed_||finish_pending_){return 0U;}const uint8_t* source_state=(const uint8_t*)&state_;uint8_t* target_state=(uint8_t*)&snapshot_state_work;for(uint16_t i=0U;i<sizeof(MazeState);i++){target_state[i]=source_state[i];}
    if(timer_running_){MazeRules::add_elapsed_ms(&snapshot_state_work,HAL_GetTick()-last_update_ms_);}if(!MazeRules::make_snapshot(snapshot_state_work,&snapshot_work)){return 0U;}const uint8_t* source=(const uint8_t*)&snapshot_work;for(uint16_t i=0U;i<sizeof(snapshot_work);i++){data[i]=source[i];}return sizeof(snapshot_work);
}

bool MazeGame::restore(const uint8_t* data,uint16_t length){
    if(data==NULL||length!=sizeof(MazeSnapshot)){return false;}uint8_t* target=(uint8_t*)&snapshot_work;for(uint16_t i=0U;i<sizeof(snapshot_work);i++){target[i]=data[i];}if(!MazeRules::restore_snapshot(snapshot_work,&state_)){return false;}
    last_update_ms_=0U;last_motion_ms_=0U;drawn_seconds_=state_.elapsed_ms/1000U;held_motion_keys_=0U;pressed_motion_keys_=0U;view_bob_phase_=0U;drawn_map_player_x_=0U;drawn_map_player_y_=0U;status_dirty_=StatusAll;view_bob_offset_=0;timer_running_=false;full_dirty_=true;view_dirty_=false;map_drawn_=false;exit_contact_=state_.player_cell==state_.exit_cell&&state_.key_collected==0U;finish_pending_=false;generation_failed_=false;return true;
}

GameResult MazeGame::result() const{uint32_t seconds=state_.elapsed_ms/1000U;if(finish_pending_&&seconds==0U){seconds=1U;}return {seconds,0U,0U,state_.variant,0U,generation_failed_?GameOutcome::Failure:(finish_pending_?GameOutcome::PlayerVictory:GameOutcome::InProgress),UiTextId::ElapsedSeconds,UiTextId::ElapsedSeconds,0U};}
UiTextId MazeGame::display_name() const{return UiTextId::Maze3D;}
UiTextId MazeGame::menu_goal_text() const{return UiTextId::GoalMaze;}

void MazeGame::size_text(uint8_t variant,char* text){uint8_t width=MazeRules::width_for_variant(variant);text[0]=(char)('0'+width/10U);text[1]=(char)('0'+width%10U);text[2]='x';text[3]=text[0];text[4]=text[1];text[5]='\0';}
void MazeGame::mode_text(uint8_t variant,char* text,uint8_t capacity){char size[8];size_text(variant,size);copy_ascii(size,text,capacity);uint8_t used=0U;while(used+1U<capacity&&text[used]!='\0'){used++;}const char* suffix=MazeRules::map_enabled(variant)?" Map":" NoMap";uint8_t index=0U;while(used+1U<capacity&&suffix[index]!='\0'){text[used++]=suffix[index++];}text[used]='\0';}
void MazeGame::format_menu_mode(char* text,uint8_t capacity) const{if(text==NULL||capacity==0U){return;}mode_text(state_.variant,text,capacity);}
uint8_t MazeGame::menu_control_count() const{return 3U;}
UiTextId MazeGame::menu_control_text(uint8_t index) const{return index==0U?UiTextId::ControlMazeMove:(index==1U?UiTextId::ControlMazeTurn:UiTextId::ControlMazeLook);}

void MazeGame::draw_home_thumbnail(Ui& ui,uint16_t x,uint16_t y,uint16_t width,uint16_t height) const{
    uint16_t size=width<height?width:height;if(size<48U){return;}
    static const uint8_t walls[64]={
        12U,5U,3U,9U,5U,3U,9U,3U,9U,3U,10U,8U,3U,12U,6U,10U,
        10U,12U,6U,14U,10U,9U,5U,6U,10U,13U,1U,5U,6U,10U,13U,3U,
        12U,5U,6U,9U,3U,12U,3U,10U,9U,5U,3U,10U,12U,3U,12U,2U,
        10U,13U,4U,6U,13U,6U,9U,6U,12U,5U,5U,5U,5U,5U,4U,3U
    };
    uint16_t cell=(uint16_t)((size-10U)/8U),maze_size=(uint16_t)(cell*8U),left=(uint16_t)(x+(width-maze_size)/2U),top=(uint16_t)(y+(height-maze_size)/2U);
    for(uint8_t row=0U;row<8U;row++){for(uint8_t column=0U;column<8U;column++){
        uint8_t wall=walls[(uint8_t)(row*8U+column)];uint16_t px=(uint16_t)(left+column*cell),py=(uint16_t)(top+row*cell);
        if((wall&MazeRules::WallNorth)!=0U){ui.fill(px,py,(uint16_t)(cell+2U),2U,UI_COLOR_PRIMARY);}
        if((wall&MazeRules::WallWest)!=0U){ui.fill(px,py,2U,(uint16_t)(cell+2U),UI_COLOR_PRIMARY);}
        if(column==7U&&(wall&MazeRules::WallEast)!=0U){ui.fill((uint16_t)(px+cell),py,2U,(uint16_t)(cell+2U),UI_COLOR_PRIMARY);}
        if(row==7U&&(wall&MazeRules::WallSouth)!=0U){ui.fill(px,(uint16_t)(py+cell),(uint16_t)(cell+2U),2U,UI_COLOR_PRIMARY);}
    }}
}

void MazeGame::draw_entry_page(Ui& ui,const GameActivity& activity,const GameOverview& overview) const{
    char value[24];ui.fill(16U,38U,288U,126U,UI_COLOR_PANEL);Lcd_draw_rect(16U,38U,288U,126U,UI_COLOR_LINE);draw_home_thumbnail(ui,27U,49U,88U,88U);Lcd_draw_rect(27U,49U,88U,88U,UI_COLOR_LINE);ui.text(132U,45U,UiTextId::Maze3D,UI_COLOR_PRIMARY,UI_COLOR_PANEL,24U);ui.text(132U,78U,UiTextId::Mode,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);mode_text(overview.last_variant,value,sizeof(value));ui.ascii_fit(208U,78U,102U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);ui.text(132U,101U,UiTextId::FastestSeconds,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);if(overview.high_score==0U){value[0]='-';value[1]='-';value[2]='\0';}else{format_u32(overview.high_score,value);}ui.ascii_fit(238U,101U,54U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);ui.text(28U,143U,UiTextId::RecentPlay,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);format_activity(activity,value);ui.ascii_fit(100U,143U,166U,value,UI_COLOR_TEXT,UI_COLOR_PANEL);
}

void MazeGame::draw_entry_control(Ui& ui,uint8_t control,bool selected,bool snapshot_available) const{if(control==0U){ui.large_button(16U,180U,140U,snapshot_available?UiTextId::ContinueGame:UiTextId::StartGame,selected);}else if(control==1U){ui.large_button(164U,180U,140U,UiTextId::AdjustDifficulty,selected);}}
bool MazeGame::variant_unlocked(const GameProgressView&,uint8_t variant) const{return variant<MazeRules::VariantCount;}

void MazeGame::draw_variant_select(Ui& ui,uint8_t selected,uint8_t previous,const GameProgressView&,bool full_redraw) const{
    if(selected>=MazeRules::VariantCount){selected=0U;}bool redraw_both=full_redraw||selected==previous;if(full_redraw){ui.fill(16U,38U,288U,164U,UI_COLOR_PANEL);Lcd_draw_rect(16U,38U,288U,164U,UI_COLOR_LINE);ui.text(112U,52U,UiTextId::AdjustDifficulty,UI_COLOR_MUTED,UI_COLOR_PANEL,24U);ui.text(104U,176U,UiTextId::PromptSaveCancel,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);}
    uint16_t first_bg=select_focus_==0U?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL,second_bg=select_focus_==1U?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL;
    if(redraw_both||select_focus_==0U){char value[12];size_text((uint8_t)(selected/2U*2U),value);ui.fill(18U,88U,284U,32U,first_bg);ui.text(28U,96U,UiTextId::GridSize,select_focus_==0U?UI_COLOR_FOCUS:UI_COLOR_TEXT,first_bg,16U);if(select_focus_==0U){ui.lr_hint(216U,96U,78U,first_bg);}ui.ascii(238U,96U,value,UI_COLOR_MUTED,first_bg,16U);}
    if(redraw_both||select_focus_==1U){ui.fill(18U,126U,284U,32U,second_bg);ui.text(28U,134U,UiTextId::PositionMap,select_focus_==1U?UI_COLOR_FOCUS:UI_COLOR_TEXT,second_bg,16U);if(select_focus_==1U){ui.lr_hint(216U,134U,78U,second_bg);}ui.text(242U,134U,MazeRules::map_enabled(selected)?UiTextId::MapShown:UiTextId::MapHidden,UI_COLOR_MUTED,second_bg,16U);}
}

GameSelectSignal MazeGame::handle_variant_select(const InputEvent& input,uint8_t* variant) const{
    if(variant==NULL||*variant>=MazeRules::VariantCount){return GameSelectSignal::None;}if(Input_is_pressed(input,InputAction::Up)||Input_is_pressed(input,InputAction::Down)){select_focus_=(uint8_t)(select_focus_==0U?1U:0U);return GameSelectSignal::SelectionChanged;}
    int8_t delta=Input_is_pressed(input,InputAction::Action1)?-1:(Input_is_pressed(input,InputAction::Action2)?1:0);if(delta!=0){uint8_t size=(uint8_t)(*variant/2U),map=(uint8_t)(*variant&1U);if(select_focus_==0U){size=delta<0?(size==0U?2U:(uint8_t)(size-1U)):(uint8_t)((size+1U)%3U);}else{map^=1U;}*variant=(uint8_t)(size*2U+map);return GameSelectSignal::SelectionChanged;}if(Input_is_pressed(input,InputAction::Confirm)){return GameSelectSignal::Confirmed;}return GameSelectSignal::None;
}
