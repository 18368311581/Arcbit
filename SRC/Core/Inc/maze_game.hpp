#ifndef __MAZE_GAME_HPP__
#define __MAZE_GAME_HPP__

#include "game.hpp"
#include "maze_rules.hpp"

class MazeGame : public Game {
public:
    MazeGame();
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

private:
    MazeState state_;
    uint32_t fastest_seconds_[MazeRules::VariantCount];
    uint32_t last_update_ms_;
    uint32_t last_motion_ms_;
    uint32_t drawn_seconds_;
    uint16_t held_motion_keys_;
    uint16_t pressed_motion_keys_;
    uint16_t view_bob_phase_;
    uint16_t drawn_map_player_x_;
    uint16_t drawn_map_player_y_;
    uint8_t status_dirty_;
    int8_t view_bob_offset_;
    mutable uint8_t select_focus_;
    bool timer_running_;
    bool full_dirty_;
    bool view_dirty_;
    bool map_drawn_;
    bool exit_contact_;
    bool finish_pending_;
    bool generation_failed_;

    static void size_text(uint8_t variant,char* text);
    static void mode_text(uint8_t variant,char* text,uint8_t capacity);
    void accumulate_time(uint32_t now_ms);
    void update_motion(uint32_t now_ms);
    void draw_view(Ui& ui) const;
    void draw_map(Ui& ui,bool full_redraw,bool objects_dirty);
    void draw_status(Ui& ui,bool full_redraw);
};

#endif
