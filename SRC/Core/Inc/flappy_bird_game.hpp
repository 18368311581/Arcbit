#ifndef __FLAPPY_BIRD_GAME_HPP__
#define __FLAPPY_BIRD_GAME_HPP__

#include "flappy_bird_rules.hpp"
#include "game.hpp"

class FlappyBirdGame : public Game {
public:
    FlappyBirdGame();
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
    FlappyBirdState state_;
    uint32_t high_scores_[FlappyBirdRules::DifficultyCount];
    uint32_t last_update_ms_;
    int16_t drawn_bird_y_;
    int16_t drawn_pipe_x_[FlappyBirdRules::PipeCount];
    bool full_dirty_;
    bool status_dirty_;
    bool draw_requested_;
    bool game_over_pending_;
    bool prompt_dirty_;

    static int16_t pixel_position(int32_t value_q8);
    static void difficulty_text(uint8_t difficulty,char* text);
    void draw_bird(Ui& ui,int16_t y) const;
    void draw_pipe_slice(Ui& ui,uint8_t index,int16_t x,uint16_t width) const;
    void draw_full_pipe(Ui& ui,uint8_t index,int16_t x) const;
    void draw_status(Ui& ui) const;
    void restore_pipes_in_rect(Ui& ui,int16_t x,int16_t y,uint16_t width,uint16_t height) const;
};

#endif
