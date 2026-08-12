#include "ui_sound.hpp"
#include "audio.h"

namespace {

static const Audio_note sound_move[]={{880U,130U}};
static const Audio_note sound_page_left[]={{988U,75U},{784U,75U}};
static const Audio_note sound_page_right[]={{784U,75U},{988U,75U}};
static const Audio_note sound_confirm[]={{1047U,75U},{1319U,90U}};
static const Audio_note sound_back[]={{784U,70U},{659U,80U}};
static const Audio_note sound_decrease[]={{740U,130U}};
static const Audio_note sound_increase[]={{988U,130U}};
static const Audio_note sound_start[]={{784U,60U},{1047U,65U},{1319U,90U}};
static const Audio_note sound_pause_saved[]={{988U,70U},{0U,60U},{988U,80U}};
static const Audio_note sound_saved[]={{1047U,85U},{1568U,110U}};
static const Audio_note sound_resume[]={{784U,70U},{988U,90U}};
static const Audio_note sound_wake[]={{659U,70U},{988U,90U}};
static const Audio_note sound_turn[]={{1200U,125U}};
static const Audio_note sound_food[]={{1175U,125U}};
static const Audio_note sound_clock_fruit[]={{988U,70U},{1319U,95U}};
static const Audio_note sound_golden_fruit[]={{1319U,70U},{1760U,105U}};
static const Audio_note sound_locked[]={{330U,150U}};
static const Audio_note sound_error[]={{220U,90U},{0U,60U},{180U,130U}};
static const Audio_note sound_game_over[]={{659U,80U},{494U,90U},{330U,130U}};
static const Audio_note sound_complete[]={{1047U,75U},{1319U,85U},{1568U,120U}};
static const Audio_note sound_record[]={{1319U,70U},{1568U,80U},{2093U,60U}};
static const Audio_note sound_unlock[]={{1047U,75U},{1319U,85U},{1760U,130U}};
static const Audio_note sound_life_lost[]={{440U,90U},{220U,130U}};
static const Audio_note sound_level_complete[]={{1047U,75U},{1319U,85U},{1760U,110U}};
static const Audio_note sound_2048_merge_low[]={{1047U,55U},{1319U,70U}};
static const Audio_note sound_2048_merge_medium[]={{988U,45U},{1319U,55U},{1568U,75U}};
static const Audio_note sound_2048_merge_high[]={{784U,35U},{1047U,40U},{1319U,45U},{1568U,50U},{2093U,70U}};
static const Audio_note sound_game_action[]={{988U,90U}};
static const Audio_note sound_score[]={{1047U,65U},{1568U,90U}};
static const Audio_note sound_shutdown[]={{659U,150U}};
static UiSoundDensity sound_density=UiSoundDensity::High;

static void play(const Audio_note* notes,uint8_t count){
    if(Audio_get_volume()!=0U){Audio_play_sequence(notes,count);}
}

static bool density_allows(UiSoundId id){
    if(sound_density==UiSoundDensity::High){return true;}
    switch(id){
    case UiSoundId::Food:
    case UiSoundId::ClockFruit:
    case UiSoundId::GoldenFruit:
    case UiSoundId::GameOver:
    case UiSoundId::Complete:
    case UiSoundId::Record:
    case UiSoundId::Unlock:
    case UiSoundId::LifeLost:
    case UiSoundId::LevelComplete:
    case UiSoundId::Game2048MergeLow:
    case UiSoundId::Game2048MergeMedium:
    case UiSoundId::Game2048MergeHigh:
    case UiSoundId::Score:
        return true;
    default:
        break;
    }
    if(sound_density==UiSoundDensity::Low){return false;}
    switch(id){
    case UiSoundId::Confirm:
    case UiSoundId::Back:
    case UiSoundId::Start:
    case UiSoundId::PauseSaved:
    case UiSoundId::Saved:
    case UiSoundId::Resume:
    case UiSoundId::Wake:
    case UiSoundId::Locked:
    case UiSoundId::Error:
    case UiSoundId::TetrisLand:
    case UiSoundId::MinesweeperFlag:
        return true;
    default:
        return false;
    }
}

}

void UiSound_set_volume(uint8_t volume){Audio_set_volume(volume);}
void UiSound_set_density(UiSoundDensity density){sound_density=density<=UiSoundDensity::High?density:UiSoundDensity::High;}

void UiSound_play(UiSoundId id){
    if(!density_allows(id)){return;}
    switch(id){
    case UiSoundId::Move:play(sound_move,1U);break;
    case UiSoundId::PageLeft:play(sound_page_left,2U);break;
    case UiSoundId::PageRight:play(sound_page_right,2U);break;
    case UiSoundId::Confirm:play(sound_confirm,2U);break;
    case UiSoundId::Back:play(sound_back,2U);break;
    case UiSoundId::Decrease:play(sound_decrease,1U);break;
    case UiSoundId::Increase:play(sound_increase,1U);break;
    case UiSoundId::Start:play(sound_start,3U);break;
    case UiSoundId::PauseSaved:play(sound_pause_saved,3U);break;
    case UiSoundId::Saved:play(sound_saved,2U);break;
    case UiSoundId::Resume:play(sound_resume,2U);break;
    case UiSoundId::Wake:play(sound_wake,2U);break;
    case UiSoundId::Turn:play(sound_turn,1U);break;
    case UiSoundId::Food:play(sound_food,1U);break;
    case UiSoundId::ClockFruit:play(sound_clock_fruit,2U);break;
    case UiSoundId::GoldenFruit:play(sound_golden_fruit,2U);break;
    case UiSoundId::Locked:play(sound_locked,1U);break;
    case UiSoundId::Error:play(sound_error,3U);break;
    case UiSoundId::GameOver:play(sound_game_over,3U);break;
    case UiSoundId::Complete:play(sound_complete,3U);break;
    case UiSoundId::Record:play(sound_record,3U);break;
    case UiSoundId::Unlock:play(sound_unlock,3U);break;
    case UiSoundId::LifeLost:play(sound_life_lost,2U);break;
    case UiSoundId::LevelComplete:play(sound_level_complete,3U);break;
    case UiSoundId::Game2048MergeLow:play(sound_2048_merge_low,2U);break;
    case UiSoundId::Game2048MergeMedium:play(sound_2048_merge_medium,3U);break;
    case UiSoundId::Game2048MergeHigh:play(sound_2048_merge_high,5U);break;
    case UiSoundId::GameAction:play(sound_game_action,1U);break;
    case UiSoundId::TetrisLand:play(sound_game_action,1U);break;
    case UiSoundId::MinesweeperFlag:play(sound_game_action,1U);break;
    case UiSoundId::Score:play(sound_score,2U);break;
    }
}

void UiSound_play_shutdown(void){
    uint8_t previous_volume=Audio_get_volume();
    uint32_t started=HAL_GetTick();
    Audio_set_volume(2U);
    if(Audio_play_sequence(sound_shutdown,1U)==0U){
        while(Audio_is_busy()!=0U&&(uint32_t)(HAL_GetTick()-started)<250UL){}
    }
    Audio_stop();
    Audio_set_volume(previous_volume);
}

void UiSound_stop(void){Audio_stop();}
