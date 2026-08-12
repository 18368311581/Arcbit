#ifndef __FLAPPY_BIRD_RULES_HPP__
#define __FLAPPY_BIRD_RULES_HPP__

#include <stdint.h>

enum class FlappyBirdPhase : uint8_t {Ready=0U,Running,GameOver};

struct FlappyBirdDifficulty {
    uint16_t speed_px_s;
    uint16_t gap_height;
    uint16_t spacing;
    uint16_t max_center_change;
};

#pragma pack(push,1)
struct FlappyBirdPipeState {
    int32_t x_q8;
    uint16_t gap_top;
    uint8_t scored;
    uint8_t reserved;
};

struct FlappyBirdSnapshot {
    uint32_t magic;
    uint16_t version;
    uint8_t difficulty;
    uint8_t phase;
    uint32_t score;
    uint32_t reserved_random;
    uint16_t accumulator_ms;
    uint16_t reserved;
    int32_t bird_y_q8;
    int32_t bird_velocity_q8;
    FlappyBirdPipeState pipes[3];
};
#pragma pack(pop)

static_assert(sizeof(FlappyBirdPipeState)==8U,"Flappy Bird pipe format changed");
static_assert(sizeof(FlappyBirdSnapshot)==52U,"Flappy Bird snapshot format changed");

struct FlappyBirdState {
    uint32_t score;
    int32_t bird_y_q8;
    int32_t bird_velocity_q8;
    FlappyBirdPipeState pipes[3];
    uint16_t accumulator_ms;
    uint8_t difficulty;
    FlappyBirdPhase phase;
};

namespace FlappyBirdRules {

static constexpr uint8_t DifficultyCount=4U;
static constexpr uint8_t PipeCount=3U;
static constexpr uint16_t PlayWidth=240U;
static constexpr uint16_t PlayHeight=240U;
static constexpr uint16_t BirdX=48U;
static constexpr uint16_t BirdWidth=14U;
static constexpr uint16_t BirdHeight=10U;
static constexpr uint16_t HitboxInset=1U;
static constexpr uint16_t PipeWidth=26U;
static constexpr uint16_t EdgeMargin=20U;
static constexpr uint16_t StepMs=20U;
static constexpr uint8_t MaxCatchUpSteps=5U;
static constexpr int32_t FixedOne=256;
static constexpr uint32_t SnapshotMagic=0x464C5031UL;
static constexpr uint16_t SnapshotVersion=2U;

const FlappyBirdDifficulty& difficulty(uint8_t index);
uint16_t next_gap_top(uint8_t difficulty_index,uint16_t previous_gap_top);
void start(FlappyBirdState* state,uint8_t difficulty_index);
void flap(FlappyBirdState* state);
bool step(FlappyBirdState* state,bool* scored);
bool collides(const FlappyBirdState& state);
bool state_valid(const FlappyBirdState& state);
bool make_snapshot(const FlappyBirdState& state,FlappyBirdSnapshot* snapshot);
bool restore_snapshot(const FlappyBirdSnapshot& snapshot,FlappyBirdState* state);

}

#endif
