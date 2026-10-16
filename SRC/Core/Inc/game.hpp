#ifndef __GAME_HPP__
#define __GAME_HPP__

#include "input.hpp"

class Ui;
enum class UiTextId : uint8_t;

enum class GameId : uint8_t {
    Snake=1U,
    CompetitiveSnake=2U,
    Gomoku=3U,
    Game2048=4U,
    FlappyBird=5U,
    Tetris=6U,
    Minesweeper=7U,
    Maze=8U,
    LayerElimination=9U
};

#define GAME_ACTIVITY_COUNT 4U
#define GAME_CAPACITY       10U

struct GameActivity {
    uint32_t score;
    uint16_t progress;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t variant;
    uint8_t valid;
    uint8_t game_id;
    uint8_t second;
};

enum class GameSignal : uint8_t {
    None=0U,
    NeedsDraw,
    LifeLost,
    LevelComplete,
    GameOver
};

enum class GameSelectSignal : uint8_t {
    None=0U,
    SelectionChanged,
    PageLeft,
    PageRight,
    Confirmed
};

struct GameProgressView {
    const uint8_t* completed_bits;
    uint8_t completed_bytes;
};

enum class GameOutcome : uint8_t {
    InProgress=0U,
    LevelComplete,
    Failure,
    PlayerVictory,
    AiVictory,
    Draw
};

struct GameResult {
    uint32_t score;
    uint16_t length;
    uint16_t target_score;
    uint8_t variant;
    uint8_t lives;
    GameOutcome outcome;
    UiTextId primary_label;
    UiTextId secondary_label;
    uint8_t score_change_sound;
};

struct GameOverview {
    uint32_t high_score;
    uint16_t high_progress;
    uint8_t unlocked_count;
    uint8_t variant_count;
    uint8_t last_variant;
    uint8_t reserved[3];
};
static_assert(sizeof(GameOverview)==12U,"Game overview format changed");

struct GameRecordSummary {
    uint32_t value;
    uint8_t variant;
    uint8_t valid;
    uint8_t lower_is_better;
    uint8_t reserved;
};
static_assert(sizeof(GameRecordSummary)==8U,"Game record summary format changed");

class Game {
public:
    virtual GameId id() const=0;
    virtual const char* title() const=0;
    virtual void start_new(uint8_t variant)=0;
    virtual bool restore(const uint8_t* data,uint16_t length)=0;
    virtual void handle_input(const InputEvent& input)=0;
    virtual GameSignal update(uint32_t now_ms)=0;
    virtual void draw(Ui& ui,bool full_redraw)=0;
    virtual uint16_t make_snapshot(uint8_t* data,uint16_t capacity) const=0;
    virtual GameResult result() const=0;
    virtual UiTextId display_name() const=0;
    virtual UiTextId menu_goal_text() const=0;
    virtual void format_menu_mode(char* text,uint8_t capacity) const=0;
    virtual uint8_t menu_control_count() const=0;
    virtual UiTextId menu_control_text(uint8_t index) const=0;
    virtual void draw_home_thumbnail(Ui& ui,uint16_t x,uint16_t y,uint16_t width,uint16_t height) const=0;
    virtual void draw_entry_page(Ui& ui,const GameActivity& activity,const GameOverview& overview) const=0;
    virtual void draw_entry_control(Ui& ui,uint8_t control,bool selected,bool snapshot_available) const=0;
    virtual uint8_t variant_count() const=0;
    virtual uint8_t default_variant() const=0;
    virtual const char* variant_page_title() const=0;
    virtual UiTextId variant_action_text() const=0;
    virtual uint16_t persistent_data_version() const=0;
    virtual void persistent_defaults()=0;
    virtual bool persistent_data_valid(const uint8_t* data,uint16_t length,uint16_t version) const=0;
    virtual void load_persistent_data(const uint8_t* data,uint16_t length)=0;
    virtual uint16_t make_persistent_data(uint8_t* data,uint16_t capacity) const=0;
    virtual GameOverview persistent_overview(uint8_t selected_variant) const=0;
    virtual GameRecordSummary record_summary(const uint8_t* data,uint16_t length,uint16_t version) const=0;
    virtual void format_record_variant(uint8_t variant,char* text,uint8_t capacity) const=0;
    virtual void apply_result(const GameResult& result,bool* new_record,bool* new_unlock)=0;
    virtual bool variant_unlocked(const GameProgressView& progress,uint8_t variant) const=0;
    virtual void draw_variant_select(Ui& ui,uint8_t selected,uint8_t previous,const GameProgressView& progress,bool page_redraw) const=0;
    virtual GameSelectSignal handle_variant_select(const InputEvent& input,uint8_t* variant) const=0;
    static void format_u32(uint32_t value,char* text);
    static void format_activity(const GameActivity& activity,char* text);
    static void copy_ascii(const char* source,char* target,uint8_t capacity);

protected:
    ~Game()=default;
};

#endif
