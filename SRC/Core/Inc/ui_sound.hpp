#ifndef __UI_SOUND_HPP__
#define __UI_SOUND_HPP__

#include "basic.h"

enum class UiSoundDensity : uint8_t {Low=0U,Medium,High};

enum class UiSoundId : uint8_t {
    Move=0U,
    PageLeft,
    PageRight,
    Confirm,
    Back,
    Decrease,
    Increase,
    Start,
    PauseSaved,
    Saved,
    Resume,
    Wake,
    Turn,
    Food,
    ClockFruit,
    GoldenFruit,
    Locked,
    Error,
    GameOver,
    Complete,
    Record,
    Unlock,
    LifeLost,
    LevelComplete,
    Game2048MergeLow,
    Game2048MergeMedium,
    Game2048MergeHigh,
    GameAction,
    TetrisLand,
    MinesweeperFlag,
    Score
};

void UiSound_set_volume(uint8_t volume);
void UiSound_set_density(UiSoundDensity density);
void UiSound_play(UiSoundId id);
void UiSound_play_shutdown(void);
void UiSound_stop(void);

#endif
