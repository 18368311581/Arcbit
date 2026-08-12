#ifndef __COMPETITIVE_SNAKE_GAME_HPP__
#define __COMPETITIVE_SNAKE_GAME_HPP__

#include "game.hpp"

class CompetitiveSnakeGame : public Game {
public:
    static constexpr uint8_t Width=24U;
    static constexpr uint8_t Height=24U;
    static constexpr uint16_t MaxCells=576U;
    static constexpr uint16_t InvalidCell=0xFFFFU;
    static constexpr uint8_t SpeedCount=5U;

    CompetitiveSnakeGame();
    GameId id() const override;
    const char* title() const override;
    void start_new(uint8_t variant) override;
    bool restore(const uint8_t* data,uint16_t length) override;
    void handle_input(const InputEvent& input) override;
    GameSignal update(uint32_t now_ms) override;
    void draw(Ui& ui,bool full_redraw) override;
    uint16_t make_snapshot(uint8_t* data,uint16_t capacity) const override;
    GameResult result() const override;
    UiTextId display_name() const override;
    UiTextId menu_goal_text() const override;
    void format_menu_mode(char* text,uint8_t capacity) const override;
    uint8_t menu_control_count() const override;
    UiTextId menu_control_text(uint8_t index) const override;
    void draw_home_thumbnail(Ui& ui,uint16_t x,uint16_t y,uint16_t width,uint16_t height) const override;
    void draw_entry_page(Ui& ui,const GameActivity& activity,const GameOverview& overview) const override;
    void draw_entry_control(Ui& ui,uint8_t control,bool selected,bool snapshot_available) const override;
    uint8_t variant_count() const override;
    uint8_t default_variant() const override;
    const char* variant_page_title() const override;
    UiTextId variant_action_text() const override;
    uint16_t persistent_data_version() const override;
    void persistent_defaults() override;
    bool persistent_data_valid(const uint8_t* data,uint16_t length,uint16_t version) const override;
    void load_persistent_data(const uint8_t* data,uint16_t length) override;
    uint16_t make_persistent_data(uint8_t* data,uint16_t capacity) const override;
    GameOverview persistent_overview(uint8_t selected_variant) const override;
    GameRecordSummary record_summary(const uint8_t* data,uint16_t length,uint16_t version) const override;
    void format_record_variant(uint8_t variant,char* text,uint8_t capacity) const override;
    void apply_result(const GameResult& result,bool* new_record,bool* new_unlock) override;
    bool variant_unlocked(const GameProgressView& progress,uint8_t variant) const override;
    void draw_variant_select(Ui& ui,uint8_t selected,uint8_t previous,const GameProgressView& progress,bool page_redraw) const override;
    GameSelectSignal handle_variant_select(const InputEvent& input,uint8_t* variant) const override;

    static uint16_t speed_interval(uint8_t variant);
private:
    enum class Direction : uint8_t {Up=0U,Down,Left,Right};

    uint16_t segments_[MaxCells];
    uint16_t dirty_[8];
    uint16_t length_;
    uint16_t tail_index_;
    uint16_t food_;
    uint32_t score_;
    uint32_t last_step_ms_;
    uint8_t speed_index_;
    Direction direction_;
    Direction direction_queue_[2];
    uint8_t direction_queue_count_;
    uint8_t dirty_count_;
    bool running_;
    bool full_dirty_;
    bool status_dirty_;
    uint32_t high_score_[SpeedCount];

    static bool opposite(Direction first,Direction second);
    static uint16_t make_cell(uint8_t x,uint8_t y);
    static uint8_t cell_x(uint16_t cell);
    static uint8_t cell_y(uint16_t cell);
    uint16_t head_cell() const;
    bool body_contains(uint16_t cell,bool exclude_tail) const;
    bool spawn_food();
    void add_dirty(uint16_t cell);
    Direction planned_direction() const;
    bool queue_direction(Direction direction);
    void pop_direction();
    void draw_cell(Ui& ui,uint16_t cell) const;
};

#endif
