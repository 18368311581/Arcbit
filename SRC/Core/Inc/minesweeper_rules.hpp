#ifndef __MINESWEEPER_RULES_HPP__
#define __MINESWEEPER_RULES_HPP__

#include <stdint.h>

enum class MinesweeperCellState : uint8_t {Hidden=0U,Open,Flagged};
enum class MinesweeperPhase : uint8_t {Running=0U,LossReview};
enum class MinesweeperActionResult : uint8_t {None=0U,Changed,Victory,Loss,Error};

struct MinesweeperState {
    uint8_t contents[225];
    uint8_t states[225];
    uint32_t elapsed_ms;
    uint8_t difficulty;
    MinesweeperPhase phase;
    uint8_t selected_cell;
    uint8_t remaining_flags;
    uint8_t probes_remaining;
    uint8_t detonated_cell;
};

struct MinesweeperChangeList {
    uint8_t cells[225];
    uint16_t count;
};

#pragma pack(push,1)
struct MinesweeperSnapshot {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t elapsed_ms;
    uint8_t difficulty;
    uint8_t phase;
    uint8_t selected_cell;
    uint8_t remaining_flags;
    uint8_t probes_remaining;
    uint8_t detonated_cell;
    uint8_t reserved[6];
    uint8_t contents[225];
    uint8_t states[225];
};
#pragma pack(pop)

static_assert(sizeof(MinesweeperSnapshot)==474U,"Minesweeper snapshot format changed");

namespace MinesweeperRules {

static constexpr uint8_t Width=15U;
static constexpr uint8_t Height=15U;
static constexpr uint16_t CellCount=225U;
static constexpr uint8_t DifficultyCount=4U;
static constexpr uint8_t Mine=9U;
static constexpr uint8_t NoCell=0xFFU;
static constexpr uint32_t SnapshotMagic=0x31574D41UL;
static constexpr uint16_t SnapshotVersion=1U;

uint8_t mine_count(uint8_t difficulty);
uint8_t probe_count(uint8_t difficulty);
uint8_t cell_index(uint8_t column,uint8_t row);
uint8_t cell_column(uint8_t cell);
uint8_t cell_row(uint8_t cell);
void clear_changes(MinesweeperChangeList* changes);
void add_change(MinesweeperChangeList* changes,uint8_t cell);
bool flags_correct(const MinesweeperState& state);
bool state_valid(const MinesweeperState& state);
bool update_fastest_time(uint32_t* fastest_times,uint8_t difficulty,uint32_t seconds,bool victory);
void add_elapsed_ms(MinesweeperState* state,uint32_t elapsed_ms);
void start(MinesweeperState* state,uint8_t difficulty);
MinesweeperActionResult reveal(MinesweeperState* state,uint8_t cell,MinesweeperChangeList* changes);
MinesweeperActionResult toggle_flag(MinesweeperState* state,uint8_t cell,MinesweeperChangeList* changes);
MinesweeperActionResult probe(MinesweeperState* state,uint8_t cell,MinesweeperChangeList* changes);
MinesweeperActionResult quick_action(MinesweeperState* state,uint8_t cell,MinesweeperChangeList* changes);
bool make_snapshot(const MinesweeperState& state,MinesweeperSnapshot* snapshot);
bool restore_snapshot(const MinesweeperSnapshot& snapshot,MinesweeperState* state);

}

#endif
