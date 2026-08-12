#ifndef __MAZE_RULES_HPP__
#define __MAZE_RULES_HPP__

#include <stdint.h>

enum class MazeDirection : uint8_t {North=0U,East,South,West};
enum class MazeMoveResult : uint8_t {None=0U,Moved,KeyCollected,ExitLocked,Finished};

namespace MazeRules {static constexpr uint8_t MaxLoopCount=40U;}

struct MazeMotionResult {
    MazeMoveResult event;
    uint8_t moved;
};

struct MazeRayHit {
    uint16_t distance_q8;
    uint8_t virtual_x;
    uint8_t virtual_y;
    uint8_t vertical_side;
    uint8_t face_direction;
};

struct MazeState {
    uint8_t walls[1024];
    uint32_t elapsed_ms;
    uint16_t entry_cell;
    uint16_t key_cell;
    uint16_t exit_cell;
    uint16_t player_cell;
    uint16_t player_x_q8;
    uint16_t player_y_q8;
    uint16_t view_angle;
    int16_t view_pitch_degrees;
    uint16_t loop_edges[MazeRules::MaxLoopCount];
    uint8_t variant;
    uint8_t loop_count;
    uint8_t key_collected;
    uint8_t gradient_mode;
    uint8_t floor_palette;
    uint8_t loop_lengths[MazeRules::MaxLoopCount];
};

#pragma pack(push,1)
struct MazeSnapshot {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t elapsed_ms;
    uint16_t entry_cell;
    uint16_t key_cell;
    uint16_t exit_cell;
    uint16_t player_x_q8;
    uint16_t player_y_q8;
    uint16_t view_angle;
    uint16_t loop_edges[8];
    uint8_t variant;
    uint8_t loop_count;
    uint8_t key_collected;
    uint8_t gradient_mode;
    uint8_t floor_palette;
    uint8_t loop_lengths[8];
    int16_t view_pitch_degrees;
    uint8_t reserved[5];
    uint8_t walls[1024];
};
#pragma pack(pop)

static_assert(sizeof(MazeSnapshot)==1084U,"Maze snapshot format changed");

namespace MazeRules {

static constexpr uint8_t VariantCount=6U;
static constexpr uint8_t SizeCount=3U;
static constexpr uint8_t MaxWidth=32U;
static constexpr uint16_t MaxCells=1024U;
static constexpr uint8_t WallNorth=1U<<0;
static constexpr uint8_t WallEast=1U<<1;
static constexpr uint8_t WallSouth=1U<<2;
static constexpr uint8_t WallWest=1U<<3;
static constexpr uint32_t SnapshotMagic=0x315A4D41UL;
static constexpr uint16_t SnapshotVersion=4U;
static constexpr uint16_t CoordinateOne=256U;
static constexpr uint16_t PlayerRadiusQ8=32U;
static constexpr uint16_t ForwardSpeedQ8PerSecond=615U;
static constexpr uint16_t StrafeSpeedQ8PerSecond=435U;
static constexpr int16_t DiagonalScaleQ15=23170;

uint8_t width_for_variant(uint8_t variant);
uint8_t loops_for_variant(uint8_t variant);
bool map_enabled(uint8_t variant);
uint16_t cell_count(uint8_t width);
uint16_t cell_index(uint8_t x,uint8_t y,uint8_t width);
uint8_t cell_x(uint16_t cell,uint8_t width);
uint8_t cell_y(uint16_t cell,uint8_t width);
uint16_t cell_center_x_q8(uint16_t cell,uint8_t width);
uint16_t cell_center_y_q8(uint16_t cell,uint8_t width);
uint8_t virtual_width(const MazeState& state);
uint8_t wall_for_direction(MazeDirection direction);
MazeDirection opposite(MazeDirection direction);
bool has_wall(const MazeState& state,uint16_t cell,MazeDirection direction);
bool virtual_cell_solid(const MazeState& state,int16_t x,int16_t y);
bool position_valid(const MazeState& state,uint16_t x_q8,uint16_t y_q8);
uint16_t passage_count(const MazeState& state);
uint16_t shortest_distance(const MazeState& state,uint16_t start,uint16_t target);
bool state_valid(const MazeState& state);
bool generate(MazeState* state,uint8_t variant);
bool generate_fallback(MazeState* state,uint8_t variant,uint8_t transform);
MazeMotionResult move_continuous(MazeState* state,int8_t forward_axis,int8_t strafe_axis,int16_t facing_x_q15,int16_t facing_y_q15,uint16_t elapsed_ms);
MazeRayHit cast_ray(const MazeState& state,int32_t ray_x_q15,int32_t ray_y_q15);
void add_elapsed_ms(MazeState* state,uint32_t elapsed_ms);
bool update_fastest_time(uint32_t* fastest_times,uint8_t variant,uint32_t seconds,bool victory);
bool make_snapshot(const MazeState& state,MazeSnapshot* snapshot);
bool restore_snapshot(const MazeSnapshot& snapshot,MazeState* state);

}

#endif
