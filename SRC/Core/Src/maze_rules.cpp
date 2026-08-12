#include "maze_rules.hpp"
#include "rng.h"
#include <limits.h>

namespace {

static constexpr uint8_t AllWalls=MazeRules::WallNorth|MazeRules::WallEast|MazeRules::WallSouth|MazeRules::WallWest;
static constexpr uint8_t Visited=0x80U;
static constexpr uint16_t NoDistance=0xFFFFU;
static constexpr uint8_t NormalAttempts=32U;
static constexpr uint16_t MotionStepQ8=32U;
static constexpr uint16_t MotionMaxMs=250U;
static constexpr uint16_t RayNearQ8=16U;
static constexpr int16_t ViewPitchLimitDegrees=75;

static uint16_t generation_stack[MazeRules::MaxCells];
static uint16_t bfs_queue[MazeRules::MaxCells];
static uint16_t distance_from_entry[MazeRules::MaxCells];
static uint16_t distance_work[MazeRules::MaxCells];
static uint16_t candidates[MazeRules::MaxCells*2U];
static MazeState restore_candidate;

static const int8_t direction_x[4]={0,1,0,-1};
static const int8_t direction_y[4]={-1,0,1,0};

static bool valid_variant(uint8_t variant){return variant<MazeRules::VariantCount;}

static bool neighbor(uint16_t cell,MazeDirection direction,uint8_t width,uint16_t* next){
    int16_t x=(int16_t)MazeRules::cell_x(cell,width)+direction_x[(uint8_t)direction];
    int16_t y=(int16_t)MazeRules::cell_y(cell,width)+direction_y[(uint8_t)direction];
    if(x<0||y<0||x>=width||y>=width||next==0){return false;}
    *next=MazeRules::cell_index((uint8_t)x,(uint8_t)y,width);return true;
}

static void remove_wall(MazeState* state,uint16_t first,MazeDirection direction,uint16_t second){
    state->walls[first]&=(uint8_t)~MazeRules::wall_for_direction(direction);
    state->walls[second]&=(uint8_t)~MazeRules::wall_for_direction(MazeRules::opposite(direction));
}

static uint16_t boundary_cell(uint16_t position,uint8_t width,MazeDirection* inward){
    uint16_t side=(uint16_t)(width-1U),x=0U,y=0U;
    if(position<width){x=position;y=0U;if(inward!=0){*inward=MazeDirection::South;}}
    else if(position<width+side){x=width-1U;y=(uint16_t)(position-width+1U);if(inward!=0){*inward=MazeDirection::West;}}
    else if(position<width+side*2U){x=(uint16_t)(width-2U-(position-(width+side)));y=width-1U;if(inward!=0){*inward=MazeDirection::North;}}
    else{x=0U;y=(uint16_t)(width-2U-(position-(width+side*2U)));if(inward!=0){*inward=MazeDirection::East;}}
    return MazeRules::cell_index((uint8_t)x,(uint8_t)y,width);
}

static uint16_t angle_for_direction(MazeDirection direction){
    static const uint16_t angles[4]={49152U,0U,16384U,32768U};
    return angles[(uint8_t)direction&3U];
}

static void place_player(MazeState* state,uint16_t cell,MazeDirection direction){
    uint8_t width=MazeRules::width_for_variant(state->variant);state->player_cell=cell;
    state->player_x_q8=MazeRules::cell_center_x_q8(cell,width);state->player_y_q8=MazeRules::cell_center_y_q8(cell,width);state->view_angle=angle_for_direction(direction);state->view_pitch_degrees=0;
}

static void initialize_state(MazeState* state,uint8_t variant){
    uint8_t width=MazeRules::width_for_variant(variant);uint16_t count=MazeRules::cell_count(width);
    for(uint16_t i=0U;i<MazeRules::MaxCells;i++){state->walls[i]=i<count?AllWalls:0U;}
    state->elapsed_ms=0U;state->entry_cell=0U;state->key_cell=0U;state->exit_cell=0U;state->player_cell=0U;state->player_x_q8=0U;state->player_y_q8=0U;state->view_angle=0U;state->view_pitch_degrees=0;
    state->variant=variant;state->loop_count=MazeRules::loops_for_variant(variant);state->key_collected=0U;state->gradient_mode=(uint8_t)Rng_get_range_int(0,3);state->floor_palette=(uint8_t)Rng_get_range_int(0,255);
    for(uint8_t i=0U;i<MazeRules::MaxLoopCount;i++){state->loop_edges[i]=0U;state->loop_lengths[i]=0U;}
}

static void bfs(const MazeState& state,uint16_t start,uint16_t* distances){
    uint8_t width=MazeRules::width_for_variant(state.variant);uint16_t count=MazeRules::cell_count(width),read=0U,write=0U;
    for(uint16_t i=0U;i<count;i++){distances[i]=NoDistance;}
    if(start>=count){return;}distances[start]=0U;bfs_queue[write++]=start;
    while(read<write){
        uint16_t cell=bfs_queue[read++];
        for(uint8_t value=0U;value<4U;value++){
            MazeDirection direction=(MazeDirection)value;uint16_t next;
            if(MazeRules::has_wall(state,cell,direction)||!neighbor(cell,direction,width,&next)||distances[next]!=NoDistance){continue;}
            distances[next]=(uint16_t)(distances[cell]+1U);bfs_queue[write++]=next;
        }
    }
}

static bool generate_tree(MazeState* state){
    uint8_t width=MazeRules::width_for_variant(state->variant);uint16_t count=MazeRules::cell_count(width),perimeter=(uint16_t)(4U*width-4U);
    MazeDirection inward=MazeDirection::South;state->entry_cell=boundary_cell((uint16_t)Rng_get_range_int(0,(int)perimeter-1),width,&inward);place_player(state,state->entry_cell,inward);
    uint16_t depth=1U;generation_stack[0]=state->entry_cell;state->walls[state->entry_cell]|=Visited;
    while(depth!=0U){
        uint16_t cell=generation_stack[depth-1U],next_cells[4];MazeDirection directions[4];uint8_t available=0U;
        for(uint8_t value=0U;value<4U;value++){
            uint16_t next;MazeDirection direction=(MazeDirection)value;
            if(neighbor(cell,direction,width,&next)&&(state->walls[next]&Visited)==0U){next_cells[available]=next;directions[available]=direction;available++;}
        }
        if(available==0U){depth--;continue;}
        uint8_t selected=(uint8_t)Rng_get_range_int(0,(int)available-1);remove_wall(state,cell,directions[selected],next_cells[selected]);
        state->walls[next_cells[selected]]|=Visited;generation_stack[depth++]=next_cells[selected];
    }
    for(uint16_t i=0U;i<count;i++){state->walls[i]&=AllWalls;}
    return true;
}

static uint16_t build_loop_candidates(const MazeState& state){
    uint8_t width=MazeRules::width_for_variant(state.variant);uint16_t count=0U;
    for(uint8_t y=0U;y<width;y++){for(uint8_t x=0U;x<width;x++){
        uint16_t cell=MazeRules::cell_index(x,y,width);
        if(x+1U<width&&MazeRules::has_wall(state,cell,MazeDirection::East)){candidates[count++]=(uint16_t)(cell*2U);}
        if(y+1U<width&&MazeRules::has_wall(state,cell,MazeDirection::South)){candidates[count++]=(uint16_t)(cell*2U+1U);}
    }}
    for(uint16_t i=count;i>1U;i--){uint16_t selected=(uint16_t)Rng_get_range_int(0,(int)i-1),temporary=candidates[i-1U];candidates[i-1U]=candidates[selected];candidates[selected]=temporary;}
    return count;
}

static bool add_random_loops(MazeState* state){
    uint8_t width=MazeRules::width_for_variant(state->variant),added=0U;uint16_t count=build_loop_candidates(*state);
    for(uint16_t i=0U;i<count&&added<state->loop_count;i++){
        uint16_t encoded=candidates[i],cell=(uint16_t)(encoded/2U),next;MazeDirection direction=(encoded&1U)!=0U?MazeDirection::South:MazeDirection::East;
        if(!neighbor(cell,direction,width,&next)||!MazeRules::has_wall(*state,cell,direction)){continue;}
        uint16_t path=MazeRules::shortest_distance(*state,cell,next);if(path<5U){continue;}
        state->loop_edges[added]=encoded;state->loop_lengths[added]=(uint8_t)(path>255U?255U:path);remove_wall(state,cell,direction,next);added++;
    }
    return added==state->loop_count;
}

static bool choose_targets(MazeState* state){
    uint8_t width=MazeRules::width_for_variant(state->variant);uint16_t count=MazeRules::cell_count(width),exit_count=0U;
    bfs(*state,state->entry_cell,distance_from_entry);
    for(uint16_t cell=0U;cell<count;cell++){if(cell!=state->entry_cell&&distance_from_entry[cell]>=width&&distance_from_entry[cell]!=NoDistance){candidates[exit_count++]=cell;}}
    for(uint16_t i=exit_count;i>1U;i--){uint16_t selected=(uint16_t)Rng_get_range_int(0,(int)i-1),temporary=candidates[i-1U];candidates[i-1U]=candidates[selected];candidates[selected]=temporary;}
    for(uint16_t exit_index=0U;exit_index<exit_count;exit_index++){
        uint16_t exit_cell=candidates[exit_index],key_cell=0U,key_count=0U;bfs(*state,exit_cell,distance_work);
        for(uint16_t cell=0U;cell<count;cell++){
            if(cell==state->entry_cell||cell==exit_cell||distance_from_entry[cell]<width||distance_work[cell]<width||distance_from_entry[cell]==NoDistance||distance_work[cell]==NoDistance){continue;}
            key_count++;if(Rng_get_range_int(1,(int)key_count)==1){key_cell=cell;}
        }
        if(key_count!=0U){state->exit_cell=exit_cell;state->key_cell=key_cell;return true;}
    }
    return false;
}

static uint16_t transformed_cell(uint16_t base,uint8_t width,uint8_t transform){
    uint8_t x=(uint8_t)(base%width),y=(uint8_t)(base/width),tx=x,ty=y;transform&=7U;
    if(transform>=4U){x=(uint8_t)(width-1U-x);transform=(uint8_t)(transform-4U);}
    if(transform==0U){tx=x;ty=y;}
    else if(transform==1U){tx=(uint8_t)(width-1U-y);ty=x;}
    else if(transform==2U){tx=(uint8_t)(width-1U-x);ty=(uint8_t)(width-1U-y);}
    else{tx=y;ty=(uint8_t)(width-1U-x);}
    return MazeRules::cell_index(tx,ty,width);
}

static uint16_t snake_cell(uint16_t order,uint8_t width,uint8_t transform){
    uint8_t y=(uint8_t)(order/width),offset=(uint8_t)(order%width),x=(y&1U)==0U?offset:(uint8_t)(width-1U-offset);
    return transformed_cell(MazeRules::cell_index(x,y,width),width,transform);
}

static MazeDirection direction_between(uint16_t first,uint16_t second,uint8_t width){
    uint8_t x1=MazeRules::cell_x(first,width),y1=MazeRules::cell_y(first,width),x2=MazeRules::cell_x(second,width),y2=MazeRules::cell_y(second,width);
    if(x2>x1){return MazeDirection::East;}if(x2<x1){return MazeDirection::West;}if(y2>y1){return MazeDirection::South;}return MazeDirection::North;
}

static void copy_state(const MazeState& source,MazeState* target){const uint8_t* from=(const uint8_t*)&source;uint8_t* to=(uint8_t*)target;for(uint16_t i=0U;i<sizeof(MazeState);i++){to[i]=from[i];}}

static uint16_t player_cell_nearest(const MazeState& state,uint16_t x_q8,uint16_t y_q8){
    uint8_t width=MazeRules::width_for_variant(state.variant);int32_t x=((int32_t)x_q8-128)/512,y=((int32_t)y_q8-128)/512;
    if(x<0){x=0;}if(y<0){y=0;}if(x>=width){x=width-1;}if(y>=width){y=width-1;}
    return MazeRules::cell_index((uint8_t)x,(uint8_t)y,width);
}

static bool player_cell_exact(const MazeState& state,uint16_t x_q8,uint16_t y_q8,uint16_t* cell){
    uint8_t vx=(uint8_t)(x_q8>>8),vy=(uint8_t)(y_q8>>8),width=MazeRules::width_for_variant(state.variant);
    if((vx&1U)==0U||(vy&1U)==0U||vx>=(uint8_t)(width*2U+1U)||vy>=(uint8_t)(width*2U+1U)){return false;}
    if(cell!=0){*cell=MazeRules::cell_index((uint8_t)((vx-1U)/2U),(uint8_t)((vy-1U)/2U),width);}return true;
}

static bool circle_clear(const MazeState& state,int32_t x_q8,int32_t y_q8){
    int16_t minimum_x=(int16_t)((x_q8-MazeRules::PlayerRadiusQ8)>>8),maximum_x=(int16_t)((x_q8+MazeRules::PlayerRadiusQ8)>>8);
    int16_t minimum_y=(int16_t)((y_q8-MazeRules::PlayerRadiusQ8)>>8),maximum_y=(int16_t)((y_q8+MazeRules::PlayerRadiusQ8)>>8);
    int32_t radius_squared=(int32_t)MazeRules::PlayerRadiusQ8*MazeRules::PlayerRadiusQ8;
    for(int16_t tile_y=minimum_y;tile_y<=maximum_y;tile_y++){for(int16_t tile_x=minimum_x;tile_x<=maximum_x;tile_x++){
        if(!MazeRules::virtual_cell_solid(state,tile_x,tile_y)){continue;}
        int32_t left=(int32_t)tile_x<<8,right=(int32_t)(tile_x+1)<<8,top=(int32_t)tile_y<<8,bottom=(int32_t)(tile_y+1)<<8;
        int32_t nearest_x=x_q8<left?left:(x_q8>right?right:x_q8),nearest_y=y_q8<top?top:(y_q8>bottom?bottom:y_q8),dx=x_q8-nearest_x,dy=y_q8-nearest_y;
        if(dx*dx+dy*dy<radius_squared){return false;}
    }}
    return true;
}

static uint32_t ray_distance(uint16_t boundary_q8,uint32_t direction_q15){
    if(direction_q15==0U){return UINT32_MAX/4U;}return ((uint32_t)boundary_q8<<15)/direction_q15;
}

}

namespace MazeRules {

uint8_t width_for_variant(uint8_t variant){static const uint8_t widths[SizeCount]={16U,24U,32U};return widths[valid_variant(variant)?variant/2U:0U];}
uint8_t loops_for_variant(uint8_t variant){static const uint8_t loops[SizeCount]={12U,24U,40U};return loops[valid_variant(variant)?variant/2U:0U];}
bool map_enabled(uint8_t variant){return valid_variant(variant)&&(variant&1U)!=0U;}
uint16_t cell_count(uint8_t width){return (uint16_t)width*width;}
uint16_t cell_index(uint8_t x,uint8_t y,uint8_t width){return (uint16_t)y*width+x;}
uint8_t cell_x(uint16_t cell,uint8_t width){return (uint8_t)(cell%width);}
uint8_t cell_y(uint16_t cell,uint8_t width){return (uint8_t)(cell/width);}
uint16_t cell_center_x_q8(uint16_t cell,uint8_t width){return (uint16_t)(((uint16_t)cell_x(cell,width)*2U+1U)*CoordinateOne+CoordinateOne/2U);}
uint16_t cell_center_y_q8(uint16_t cell,uint8_t width){return (uint16_t)(((uint16_t)cell_y(cell,width)*2U+1U)*CoordinateOne+CoordinateOne/2U);}
uint8_t virtual_width(const MazeState& state){return (uint8_t)(width_for_variant(state.variant)*2U+1U);}
uint8_t wall_for_direction(MazeDirection direction){static const uint8_t walls[4]={WallNorth,WallEast,WallSouth,WallWest};return walls[(uint8_t)direction&3U];}
MazeDirection opposite(MazeDirection direction){return (MazeDirection)(((uint8_t)direction+2U)&3U);}

bool has_wall(const MazeState& state,uint16_t cell,MazeDirection direction){uint8_t width=width_for_variant(state.variant);return cell>=cell_count(width)||(state.walls[cell]&wall_for_direction(direction))!=0U;}

bool virtual_cell_solid(const MazeState& state,int16_t x,int16_t y){
    if(!valid_variant(state.variant)){return true;}uint8_t width=width_for_variant(state.variant);int16_t world=(int16_t)(width*2U+1U);
    if(x<0||y<0||x>=world||y>=world){return true;}
    if((x&1)!=0&&(y&1)!=0){return false;}if((x&1)==0&&(y&1)==0){return true;}
    if((x&1)==0){
        uint8_t logical_y=(uint8_t)((y-1)/2);if(x==0){return has_wall(state,cell_index(0U,logical_y,width),MazeDirection::West);}if(x+1==world){return has_wall(state,cell_index((uint8_t)(width-1U),logical_y,width),MazeDirection::East);}
        return has_wall(state,cell_index((uint8_t)(x/2-1),logical_y,width),MazeDirection::East);
    }
    uint8_t logical_x=(uint8_t)((x-1)/2);if(y==0){return has_wall(state,cell_index(logical_x,0U,width),MazeDirection::North);}if(y+1==world){return has_wall(state,cell_index(logical_x,(uint8_t)(width-1U),width),MazeDirection::South);}
    return has_wall(state,cell_index(logical_x,(uint8_t)(y/2-1),width),MazeDirection::South);
}

bool position_valid(const MazeState& state,uint16_t x_q8,uint16_t y_q8){return valid_variant(state.variant)&&circle_clear(state,x_q8,y_q8);}

uint16_t passage_count(const MazeState& state){
    if(!valid_variant(state.variant)){return 0U;}uint8_t width=width_for_variant(state.variant);uint16_t passages=0U;
    for(uint8_t y=0U;y<width;y++){for(uint8_t x=0U;x<width;x++){uint16_t cell=cell_index(x,y,width);if(x+1U<width&&!has_wall(state,cell,MazeDirection::East)){passages++;}if(y+1U<width&&!has_wall(state,cell,MazeDirection::South)){passages++;}}}
    return passages;
}

uint16_t shortest_distance(const MazeState& state,uint16_t start,uint16_t target){if(!valid_variant(state.variant)){return NoDistance;}bfs(state,start,distance_work);return target<cell_count(width_for_variant(state.variant))?distance_work[target]:NoDistance;}

bool state_valid(const MazeState& state){
    if(!valid_variant(state.variant)||state.loop_count>MaxLoopCount||state.key_collected>1U||state.gradient_mode>3U||state.view_pitch_degrees<-ViewPitchLimitDegrees||state.view_pitch_degrees>ViewPitchLimitDegrees){return false;}
    uint8_t width=width_for_variant(state.variant);uint16_t count=cell_count(width);
    if(state.entry_cell>=count||state.key_cell>=count||state.exit_cell>=count||state.player_cell>=count||state.entry_cell==state.key_cell||state.entry_cell==state.exit_cell||state.key_cell==state.exit_cell||!position_valid(state,state.player_x_q8,state.player_y_q8)){return false;}
    uint16_t exact_cell;if(player_cell_exact(state,state.player_x_q8,state.player_y_q8,&exact_cell)&&exact_cell!=state.player_cell){return false;}
    uint8_t entry_x=cell_x(state.entry_cell,width),entry_y=cell_y(state.entry_cell,width);if(entry_x!=0U&&entry_x+1U!=width&&entry_y!=0U&&entry_y+1U!=width){return false;}
    for(uint8_t y=0U;y<width;y++){for(uint8_t x=0U;x<width;x++){
        uint16_t cell=cell_index(x,y,width);if((state.walls[cell]&~AllWalls)!=0U){return false;}
        if(y==0U&&!has_wall(state,cell,MazeDirection::North)){return false;}if(x==0U&&!has_wall(state,cell,MazeDirection::West)){return false;}
        if(y+1U==width&&!has_wall(state,cell,MazeDirection::South)){return false;}if(x+1U==width&&!has_wall(state,cell,MazeDirection::East)){return false;}
        if(x+1U<width&&has_wall(state,cell,MazeDirection::East)!=has_wall(state,(uint16_t)(cell+1U),MazeDirection::West)){return false;}
        if(y+1U<width&&has_wall(state,cell,MazeDirection::South)!=has_wall(state,(uint16_t)(cell+width),MazeDirection::North)){return false;}
    }}
    for(uint16_t i=count;i<MaxCells;i++){if(state.walls[i]!=0U){return false;}}
    if(passage_count(state)!=(uint16_t)(count-1U+state.loop_count)){return false;}
    bool has_loop_metadata=false;for(uint8_t i=0U;i<MaxLoopCount;i++){if(state.loop_edges[i]!=0U||state.loop_lengths[i]!=0U){has_loop_metadata=true;break;}}
    if(has_loop_metadata){
        for(uint8_t i=0U;i<state.loop_count;i++){
            uint16_t encoded=state.loop_edges[i],cell=(uint16_t)(encoded/2U),next;MazeDirection direction=(encoded&1U)!=0U?MazeDirection::South:MazeDirection::East;
            if(cell>=count||state.loop_lengths[i]<5U||!neighbor(cell,direction,width,&next)||has_wall(state,cell,direction)){return false;}
            for(uint8_t j=0U;j<i;j++){if(state.loop_edges[j]==encoded){return false;}}
        }
        for(uint8_t i=state.loop_count;i<MaxLoopCount;i++){if(state.loop_edges[i]!=0U||state.loop_lengths[i]!=0U){return false;}}
    }
    bfs(state,state.entry_cell,distance_from_entry);for(uint16_t i=0U;i<count;i++){if(distance_from_entry[i]==NoDistance){return false;}}
    if(distance_from_entry[state.key_cell]<width||distance_from_entry[state.exit_cell]<width){return false;}
    bfs(state,state.exit_cell,distance_work);if(distance_work[state.key_cell]<width){return false;}
    return true;
}

bool generate_fallback(MazeState* state,uint8_t variant,uint8_t transform){
    if(state==0||!valid_variant(variant)){return false;}uint8_t width=width_for_variant(variant);uint16_t count=cell_count(width);
    for(uint8_t attempt=0U;attempt<NormalAttempts;attempt++){
        initialize_state(state,variant);
        for(uint16_t order=1U;order<count;order++){uint16_t previous=snake_cell((uint16_t)(order-1U),width,transform),current=snake_cell(order,width,transform);remove_wall(state,previous,direction_between(previous,current,width),current);}
        state->entry_cell=snake_cell(0U,width,transform);place_player(state,state->entry_cell,direction_between(state->entry_cell,snake_cell(1U,width,transform),width));
        if(add_random_loops(state)&&choose_targets(state)&&state_valid(*state)){return true;}
    }
    return false;
}

bool generate(MazeState* state,uint8_t variant){
    if(state==0||!valid_variant(variant)){return false;}
    for(uint8_t attempt=0U;attempt<NormalAttempts;attempt++){
        initialize_state(state,variant);if(!generate_tree(state)||!add_random_loops(state)||!choose_targets(state)){continue;}if(state_valid(*state)){return true;}
    }
    return generate_fallback(state,variant,(uint8_t)Rng_get_range_int(0,7));
}

MazeMotionResult move_continuous(MazeState* state,int8_t forward_axis,int8_t strafe_axis,int16_t facing_x_q15,int16_t facing_y_q15,uint16_t elapsed_ms){
    MazeMotionResult result={MazeMoveResult::None,0U};if(state==0||!valid_variant(state->variant)||elapsed_ms==0U){return result;}if(elapsed_ms>MotionMaxMs){elapsed_ms=MotionMaxMs;}
    if(forward_axis>1){forward_axis=1;}if(forward_axis<-1){forward_axis=-1;}if(strafe_axis>1){strafe_axis=1;}if(strafe_axis<-1){strafe_axis=-1;}if(forward_axis==0&&strafe_axis==0){return result;}
    int32_t forward_distance=(int32_t)ForwardSpeedQ8PerSecond*elapsed_ms/1000*forward_axis,strafe_distance=(int32_t)StrafeSpeedQ8PerSecond*elapsed_ms/1000*strafe_axis;
    if(forward_axis!=0&&strafe_axis!=0){forward_distance=forward_distance*DiagonalScaleQ15/32768;strafe_distance=strafe_distance*DiagonalScaleQ15/32768;}
    int32_t total_x=((int32_t)facing_x_q15*forward_distance-(int32_t)facing_y_q15*strafe_distance)/32768,total_y=((int32_t)facing_y_q15*forward_distance+(int32_t)facing_x_q15*strafe_distance)/32768;
    uint32_t absolute_x=total_x<0?(uint32_t)-total_x:(uint32_t)total_x,absolute_y=total_y<0?(uint32_t)-total_y:(uint32_t)total_y,maximum=absolute_x>absolute_y?absolute_x:absolute_y;
    uint16_t steps=(uint16_t)((maximum+MotionStepQ8-1U)/MotionStepQ8);if(steps==0U){steps=1U;}
    int32_t start_x=state->player_x_q8,start_y=state->player_y_q8,applied_x=0,applied_y=0;
    for(uint16_t step=1U;step<=steps;step++){
        int32_t target_x=total_x*step/steps,target_y=total_y*step/steps,delta_x=target_x-applied_x,delta_y=target_y-applied_y;applied_x=target_x;applied_y=target_y;
        int32_t proposed_x=(int32_t)state->player_x_q8+delta_x;if(proposed_x>=0&&proposed_x<=0xFFFF&&circle_clear(*state,proposed_x,state->player_y_q8)){state->player_x_q8=(uint16_t)proposed_x;}
        int32_t proposed_y=(int32_t)state->player_y_q8+delta_y;if(proposed_y>=0&&proposed_y<=0xFFFF&&circle_clear(*state,state->player_x_q8,proposed_y)){state->player_y_q8=(uint16_t)proposed_y;}
    }
    if(state->player_x_q8==start_x&&state->player_y_q8==start_y){return result;}result.moved=1U;
    uint16_t exact_cell;if(player_cell_exact(*state,state->player_x_q8,state->player_y_q8,&exact_cell)){state->player_cell=exact_cell;}
    if(state->key_collected==0U&&state->player_cell==state->key_cell){state->key_collected=1U;result.event=MazeMoveResult::KeyCollected;}
    else if(state->player_cell==state->exit_cell){result.event=state->key_collected!=0U?MazeMoveResult::Finished:MazeMoveResult::ExitLocked;}
    else{result.event=MazeMoveResult::Moved;}
    return result;
}

MazeRayHit cast_ray(const MazeState& state,int32_t ray_x_q15,int32_t ray_y_q15){
    MazeRayHit hit={0xFFFFU,0U,0U,0U,0U};if(!valid_variant(state.variant)||!position_valid(state,state.player_x_q8,state.player_y_q8)||(ray_x_q15==0&&ray_y_q15==0)){return hit;}
    int16_t map_x=(int16_t)(state.player_x_q8>>8),map_y=(int16_t)(state.player_y_q8>>8),step_x=ray_x_q15<0?-1:1,step_y=ray_y_q15<0?-1:1;
    uint32_t ray_abs_x=(uint32_t)(ray_x_q15<0?-ray_x_q15:ray_x_q15),ray_abs_y=(uint32_t)(ray_y_q15<0?-ray_y_q15:ray_y_q15);
    uint16_t fraction_x=(uint16_t)(state.player_x_q8&0xFFU),fraction_y=(uint16_t)(state.player_y_q8&0xFFU);
    uint16_t first_x=ray_x_q15<0?fraction_x:(uint16_t)(CoordinateOne-fraction_x),first_y=ray_y_q15<0?fraction_y:(uint16_t)(CoordinateOne-fraction_y);
    uint32_t delta_x=ray_distance(CoordinateOne,ray_abs_x),delta_y=ray_distance(CoordinateOne,ray_abs_y),side_x=ray_distance(first_x,ray_abs_x),side_y=ray_distance(first_y,ray_abs_y),distance=0U;uint8_t vertical=0U;
    uint16_t maximum_steps=(uint16_t)(virtual_width(state)*2U+4U);
    for(uint16_t step=0U;step<maximum_steps;step++){
        if(side_x<side_y){distance=side_x;side_x+=delta_x;map_x=(int16_t)(map_x+step_x);vertical=1U;}
        else{distance=side_y;side_y+=delta_y;map_y=(int16_t)(map_y+step_y);vertical=0U;}
        if(!virtual_cell_solid(state,map_x,map_y)){continue;}
        if(distance<RayNearQ8){distance=RayNearQ8;}if(distance>0xFFFFU){distance=0xFFFFU;}
        int16_t world=virtual_width(state);hit.distance_q8=(uint16_t)distance;hit.virtual_x=(uint8_t)(map_x<0?0:(map_x>=world?world-1:map_x));hit.virtual_y=(uint8_t)(map_y<0?0:(map_y>=world?world-1:map_y));hit.vertical_side=vertical;hit.face_direction=vertical!=0U?(uint8_t)(step_x>0?MazeDirection::West:MazeDirection::East):(uint8_t)(step_y>0?MazeDirection::North:MazeDirection::South);return hit;
    }
    return hit;
}

void add_elapsed_ms(MazeState* state,uint32_t elapsed_ms){if(state==0){return;}state->elapsed_ms=UINT32_MAX-state->elapsed_ms<elapsed_ms?UINT32_MAX:state->elapsed_ms+elapsed_ms;}
bool update_fastest_time(uint32_t* fastest_times,uint8_t variant,uint32_t seconds,bool victory){if(fastest_times==0||variant>=VariantCount||seconds==0U||!victory){return false;}if(fastest_times[variant]!=0U&&seconds>=fastest_times[variant]){return false;}fastest_times[variant]=seconds;return true;}

bool make_snapshot(const MazeState& state,MazeSnapshot* snapshot){
    if(snapshot==0||!state_valid(state)){return false;}snapshot->magic=SnapshotMagic;snapshot->version=SnapshotVersion;snapshot->payload_size=sizeof(MazeSnapshot);snapshot->elapsed_ms=state.elapsed_ms;
    snapshot->entry_cell=state.entry_cell;snapshot->key_cell=state.key_cell;snapshot->exit_cell=state.exit_cell;snapshot->player_x_q8=state.player_x_q8;snapshot->player_y_q8=state.player_y_q8;snapshot->view_angle=state.view_angle;snapshot->view_pitch_degrees=state.view_pitch_degrees;
    for(uint8_t i=0U;i<8U;i++){snapshot->loop_edges[i]=0U;snapshot->loop_lengths[i]=0U;}
    snapshot->variant=state.variant;snapshot->loop_count=state.loop_count;snapshot->key_collected=state.key_collected;snapshot->gradient_mode=state.gradient_mode;snapshot->floor_palette=state.floor_palette;
    for(uint8_t i=0U;i<sizeof(snapshot->reserved);i++){snapshot->reserved[i]=0U;}for(uint16_t i=0U;i<MaxCells;i++){snapshot->walls[i]=state.walls[i];}return true;
}

bool restore_snapshot(const MazeSnapshot& snapshot,MazeState* state){
    if(state==0||snapshot.magic!=SnapshotMagic||snapshot.payload_size!=sizeof(MazeSnapshot)||snapshot.version!=SnapshotVersion){return false;}
    for(uint8_t i=0U;i<sizeof(snapshot.reserved);i++){if(snapshot.reserved[i]!=0U){return false;}}
    for(uint8_t i=0U;i<8U;i++){if(snapshot.loop_edges[i]!=0U||snapshot.loop_lengths[i]!=0U){return false;}}
    restore_candidate.elapsed_ms=snapshot.elapsed_ms;restore_candidate.entry_cell=snapshot.entry_cell;restore_candidate.key_cell=snapshot.key_cell;restore_candidate.exit_cell=snapshot.exit_cell;restore_candidate.player_x_q8=snapshot.player_x_q8;restore_candidate.player_y_q8=snapshot.player_y_q8;restore_candidate.view_angle=snapshot.view_angle;restore_candidate.view_pitch_degrees=snapshot.view_pitch_degrees;
    restore_candidate.variant=snapshot.variant;restore_candidate.loop_count=snapshot.loop_count;restore_candidate.key_collected=snapshot.key_collected;restore_candidate.gradient_mode=snapshot.gradient_mode;restore_candidate.floor_palette=snapshot.floor_palette;
    if(!valid_variant(snapshot.variant)){return false;}restore_candidate.player_cell=player_cell_nearest(restore_candidate,snapshot.player_x_q8,snapshot.player_y_q8);
    for(uint8_t i=0U;i<MaxLoopCount;i++){restore_candidate.loop_edges[i]=0U;restore_candidate.loop_lengths[i]=0U;}for(uint16_t i=0U;i<MaxCells;i++){restore_candidate.walls[i]=snapshot.walls[i];}
    if(!state_valid(restore_candidate)){return false;}copy_state(restore_candidate,state);return true;
}

}
