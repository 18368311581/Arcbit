#ifndef __TETRIS_RULES_HPP__
#define __TETRIS_RULES_HPP__

#include <stdint.h>

enum class TetrisPiece : uint8_t {I=0U,O,T,S,Z,J,L};

struct TetrisEvent {
    uint8_t moved;
    uint8_t locked;
    uint8_t lines_cleared;
    uint8_t game_over;
};

struct TetrisState {
    uint8_t board[24][18];
    uint32_t score;
    uint16_t lines;
    uint16_t fall_elapsed_ms;
    uint16_t lock_elapsed_ms;
    int8_t piece_x;
    int8_t piece_y;
    TetrisPiece current_piece;
    TetrisPiece next_piece;
    uint8_t rotation;
    uint8_t difficulty;
    uint8_t lock_resets;
    uint8_t grounded;
    uint8_t game_over;
};

#pragma pack(push,1)
struct TetrisSnapshot {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t score;
    uint16_t lines;
    uint16_t fall_remaining_ms;
    uint16_t lock_remaining_ms;
    uint8_t difficulty;
    uint8_t current_piece;
    uint8_t next_piece;
    int8_t piece_x;
    int8_t piece_y;
    uint8_t rotation;
    uint8_t grounded;
    uint8_t lock_resets;
    uint8_t reserved[6];
    uint8_t board[24][18];
};
#pragma pack(pop)

static_assert(sizeof(TetrisSnapshot)==464U,"Tetris snapshot format changed");

namespace TetrisRules {

static constexpr uint8_t Width=18U;
static constexpr uint8_t Height=24U;
static constexpr uint8_t PieceCount=7U;
static constexpr uint8_t DifficultyCount=3U;
static constexpr uint8_t RotationCount=4U;
static constexpr uint8_t LockResetLimit=15U;
static constexpr uint16_t LockDelayMs=500U;
static constexpr uint16_t MaximumElapsedMs=1000U;
static constexpr uint32_t SnapshotMagic=0x31525441UL;
static constexpr uint16_t SnapshotVersion=1U;

uint8_t starting_level(uint8_t difficulty);
uint8_t level(const TetrisState& state);
uint16_t gravity_period_ms(uint8_t level);
bool shape_cell(TetrisPiece piece,uint8_t rotation,uint8_t x,uint8_t y);
bool collides(const TetrisState& state,TetrisPiece piece,uint8_t rotation,int8_t x,int8_t y);
void start(TetrisState* state,uint8_t difficulty);
bool move_horizontal(TetrisState* state,int8_t delta_x);
bool rotate_clockwise(TetrisState* state);
bool soft_drop(TetrisState* state);
uint8_t hard_drop(TetrisState* state,TetrisEvent* event);
void advance(TetrisState* state,uint32_t elapsed_ms,TetrisEvent* event);
bool state_valid(const TetrisState& state);
bool make_snapshot(const TetrisState& state,TetrisSnapshot* snapshot);
bool restore_snapshot(const TetrisSnapshot& snapshot,TetrisState* state);

}

#endif
