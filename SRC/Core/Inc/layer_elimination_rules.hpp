#ifndef __LAYER_ELIMINATION_RULES_HPP__
#define __LAYER_ELIMINATION_RULES_HPP__

#include <stdint.h>

enum class LayerEliminationPhase : uint8_t {Playing=0U,ResetConfirm,AnswerConfirm,AnswerReview};

struct LayerEliminationState {
    uint8_t current[18];
    uint8_t target[18];
    uint8_t answer[18];
    uint32_t elapsed_ms;
    uint8_t variant;
    uint8_t selected_cell;
    LayerEliminationPhase phase;
    uint8_t reset_focus;
};

#pragma pack(push,1)
struct LayerEliminationSnapshot {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t elapsed_ms;
    uint8_t current[18];
    uint8_t target[18];
    uint8_t answer[18];
    uint8_t variant;
    uint8_t selected_cell;
    uint8_t phase;
    uint8_t reset_focus;
    uint8_t reserved[4];
};
#pragma pack(pop)

static_assert(sizeof(LayerEliminationSnapshot)==74U,"Layer elimination snapshot format changed");

namespace LayerEliminationRules {

static constexpr uint8_t VariantCount=6U;
static constexpr uint8_t SizeCount=2U;
static constexpr uint8_t DifficultyCount=3U;
static constexpr uint8_t MaxSize=12U;
static constexpr uint8_t MaxCells=144U;
static constexpr uint8_t BitsetBytes=18U;
static constexpr uint32_t SnapshotMagic=0x4C455231UL;
static constexpr uint16_t SnapshotVersion=2U;

uint8_t grid_size(uint8_t variant);
uint8_t difficulty(uint8_t variant);
uint8_t generation_count(uint8_t variant);
uint8_t cell_index(uint8_t column,uint8_t row,uint8_t size);
uint8_t cell_column(uint8_t cell,uint8_t size);
uint8_t cell_row(uint8_t cell,uint8_t size);
bool bit_get(const uint8_t* bits,uint8_t cell);
void bit_set(uint8_t* bits,uint8_t cell,bool value);
void toggle_cross(uint8_t* bits,uint8_t size,uint8_t cell);
bool complete(const LayerEliminationState& state);
bool all_white(const LayerEliminationState& state);
bool state_valid(const LayerEliminationState& state);
bool update_fastest_time(uint32_t* fastest_seconds,uint8_t variant,uint32_t seconds,bool victory);
void add_elapsed_ms(LayerEliminationState* state,uint32_t elapsed_ms);
void reset_current(LayerEliminationState* state);
void start(LayerEliminationState* state,uint8_t variant);
bool make_snapshot(const LayerEliminationState& state,LayerEliminationSnapshot* snapshot);
bool restore_snapshot(const LayerEliminationSnapshot& snapshot,LayerEliminationState* state);

}

#endif
