#ifndef __SNAKE_GAME_HPP__
#define __SNAKE_GAME_HPP__

#include "game.hpp"
#include "ui_text.hpp"

#define SNAKE_LEVEL_COUNT 72U
#define SNAKE_BELT_NONE   0xFFU

class SnakeGame : public Game {
public:
    static constexpr uint8_t Width=24U;
    static constexpr uint8_t Height=24U;
    static constexpr uint16_t MaxCells=576U;
    static constexpr uint16_t InvalidCell=0xFFFFU;

    SnakeGame();
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

    static bool map_obstacle(uint8_t map,uint8_t x,uint8_t y);
    static uint8_t map_belt(uint8_t map,uint8_t x,uint8_t y);
    static uint16_t map_base_step(uint8_t map);
    static uint16_t map_target(uint8_t map);
    static uint8_t map_lives(uint8_t map);
    static UiTextId map_name(uint8_t map);
private:
    enum class Direction : uint8_t {Up=0U,Down,Left,Right};
    enum class SpecialFood : uint8_t {None=0U,Golden,Clock};

    uint16_t segments_[MaxCells];
    uint16_t dirty_[16];
    uint16_t length_;
    uint16_t tail_index_;
    uint16_t food_;
    uint16_t special_food_;
    uint16_t growth_pending_;
    uint16_t normal_food_count_;
    uint32_t score_;
    uint32_t last_step_ms_;
    uint32_t special_deadline_ms_;
    uint32_t slow_deadline_ms_;
    uint32_t recovery_deadline_ms_;
    uint8_t map_;
    uint8_t lives_;
    Direction direction_;
    Direction direction_queue_[2];
    uint8_t direction_queue_count_;
    SpecialFood special_type_;
    uint8_t dirty_count_;
    GameSignal pending_signal_;
    bool running_;
    bool completed_;
    bool forward_held_;
    bool full_dirty_;
    bool status_dirty_;
    bool lives_dirty_;
    uint8_t completed_levels_[(SNAKE_LEVEL_COUNT+7U)/8U];
    uint16_t high_length_[SNAKE_LEVEL_COUNT];
    uint32_t high_score_[SNAKE_LEVEL_COUNT];

    static bool opposite(Direction first,Direction second);
    static uint16_t make_cell(uint8_t x,uint8_t y);
    static uint8_t cell_x(uint16_t cell);
    static uint8_t cell_y(uint16_t cell);
    static uint32_t remaining_ms(uint32_t now,uint32_t deadline);
    uint16_t head_cell(void) const;
    bool body_contains(uint16_t cell,bool exclude_tail) const;
    bool occupied(uint16_t cell) const;
    bool find_free_cell(uint16_t* cell) const;
    bool spawn_food(void);
    void spawn_special(uint32_t now_ms);
    void add_dirty(uint16_t cell);
    bool queue_direction(Direction direction);
    void pop_direction(void);
    void lose_life(uint32_t now_ms);
    Direction planned_direction(void) const;
    static Direction input_direction(InputAction action);
    static InputAction direction_action(Direction direction);
    uint16_t current_step(uint32_t now_ms) const;
    uint16_t cell_color(uint16_t cell) const;
    void draw_level_node(Ui& ui,uint16_t x,uint16_t y,uint8_t level,uint8_t state) const;
    void draw_level_focus(Ui& ui,uint16_t x,uint16_t y,bool selected) const;
    void draw_level_track(Ui& ui,uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1) const;
    void draw_game_frame(Ui& ui) const;
    void draw_game_status(Ui& ui,uint32_t now_ms) const;
    void draw_game_lives(Ui& ui) const;
    void draw_wall_cell(Ui& ui,uint8_t x,uint8_t y) const;
    void draw_belt_cell(Ui& ui,uint8_t x,uint8_t y,uint8_t direction) const;
    void draw_cell(Ui& ui,uint16_t cell) const;
    bool level_completed(uint8_t level) const;
    void set_level_completed(uint8_t level);
};

#endif
