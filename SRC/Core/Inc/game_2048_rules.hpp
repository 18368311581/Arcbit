#ifndef __GAME_2048_RULES_HPP__
#define __GAME_2048_RULES_HPP__

#include <stdint.h>

enum class Game2048Direction : uint8_t {Up=0U,Down,Left,Right};

struct Game2048State {
    uint8_t cells[36];
    uint32_t score;
    uint8_t size_index;
    uint8_t running;
};

struct Game2048MoveInfo {
    uint32_t score_gain;
    uint8_t merge_count;
    uint8_t highest_depth;
};

#pragma pack(push,1)
struct Game2048Snapshot {
    uint32_t magic;
    uint16_t version;
    uint8_t size_index;
    uint8_t reserved;
    uint32_t score;
    uint32_t reserved_random;
    uint8_t cells[36];
};
#pragma pack(pop)

static_assert(sizeof(Game2048Snapshot)==52U,"2048 snapshot format changed");

namespace Game2048Rules {

static constexpr uint8_t VariantCount=3U;
static constexpr uint8_t MaxSize=6U;
static constexpr uint8_t MaxCells=36U;
static constexpr uint8_t MaxExponent=63U;
static constexpr uint32_t SnapshotMagic=0x32303438UL;
static constexpr uint16_t SnapshotVersion=2U;

uint8_t board_size(uint8_t size_index);
uint8_t cell_index(uint8_t x,uint8_t y);
uint32_t tile_value(uint8_t exponent);
uint8_t sound_tier(uint32_t score_gain);
bool update_high_score(uint32_t* high_scores,uint8_t size_index,uint32_t score);
bool can_move(const Game2048State& state);
bool state_valid(const Game2048State& state);
bool spawn(Game2048State* state);
void start(Game2048State* state,uint8_t size_index);
bool slide(Game2048State* state,Game2048Direction direction,Game2048MoveInfo* info);
bool move(Game2048State* state,Game2048Direction direction,Game2048MoveInfo* info);
bool make_snapshot(const Game2048State& state,Game2048Snapshot* snapshot);
bool restore_snapshot(const Game2048Snapshot& snapshot,Game2048State* state);

}

#endif
