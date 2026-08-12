#ifndef __TETRIS_GAME_HPP__
#define __TETRIS_GAME_HPP__

#include "game.hpp"
#include "tetris_rules.hpp"

class TetrisGame : public Game {
public:
    TetrisGame();
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
    TetrisState state_;
    uint8_t drawn_board_[TetrisRules::Height][TetrisRules::Width];
    uint32_t high_scores_[TetrisRules::DifficultyCount];
    InputRepeatState horizontal_repeat_;
    InputRepeatState soft_drop_repeat_;
    uint32_t last_update_ms_;
    uint32_t drawn_score_;
    uint16_t drawn_lines_;
    int16_t drawn_piece_cells_[4];
    TetrisPiece drawn_next_piece_;
    uint8_t drawn_piece_count_;
    uint8_t drawn_level_;
    bool full_dirty_;
    bool draw_requested_;
    bool game_over_pending_;

    static uint16_t piece_color(uint8_t value);
    static void difficulty_text(uint8_t difficulty,char* text);
    static uint8_t collect_piece_cells(const TetrisState& state,int16_t* cells);
    static void draw_thumbnail_block(Ui& ui,uint16_t x,uint16_t y,uint8_t size,uint16_t color);
    static void draw_thumbnail_piece(Ui& ui,uint16_t x,uint16_t y,uint8_t cell_size,TetrisPiece piece,uint8_t rotation);
    void draw_board_cell(Ui& ui,uint8_t x,uint8_t y) const;
    void draw_active_piece(Ui& ui);
    void draw_next_piece(Ui& ui) const;
    void draw_status(Ui& ui,bool full_redraw);
    void process_event(const TetrisEvent& event,bool hard_drop);
};

#endif
