#ifndef __GOBANG_GAME_HPP__
#define __GOBANG_GAME_HPP__

#include "game.hpp"

class GobangGame : public Game {
public:
    static constexpr uint8_t BoardSize=15U;
    static constexpr uint16_t CellCount=225U;
    static constexpr uint8_t RoleCount=2U;

    GobangGame();
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
    enum class Phase : uint8_t {
        InitialDraw=0U,
        HumanTurn,
        HumanMoveDraw,
        AiPending,
        AiMoveDraw,
        TerminalPending,
        UndoConfirm,
        Count
    };

    uint8_t board_[CellCount];
    uint8_t history_[CellCount];
    uint8_t dirty_[6];
    InputRepeatState repeat_;
    uint16_t fastest_total_moves_[RoleCount];
    uint8_t history_count_;
    uint8_t selected_cell_;
    uint8_t role_;
    Phase phase_;
    GameOutcome outcome_;
    uint8_t dirty_count_;
    uint8_t undo_focus_;
    uint8_t last_ai_cell_;
    bool undo_used_;
    bool full_dirty_;
    bool status_dirty_;
    bool modal_dirty_;
    bool dialog_restore_dirty_;
    bool draw_requested_;

    static uint8_t row_of(uint8_t cell);
    static uint8_t column_of(uint8_t cell);
    static uint8_t make_cell(uint8_t row,uint8_t column);
    static uint8_t history_stone(uint8_t role,uint8_t move_index);
    static bool star_point(uint8_t row,uint8_t column);
    static void draw_corner_marker(Ui& ui,int16_t x,int16_t y,uint16_t color);
    void add_dirty(uint8_t cell);
    void move_selection(int8_t row_delta,int8_t column_delta);
    bool can_undo() const;
    void undo_pair();
    void perform_ai();
    uint8_t latest_ai_cell() const;
    bool board_full() const;
    UiTextId turn_text() const;
    uint16_t stone_color(uint8_t stone) const;
    void draw_stone(Ui& ui,uint8_t cell) const;
    void draw_intersection(Ui& ui,uint8_t cell,bool selected) const;
    void draw_board(Ui& ui) const;
    void draw_status(Ui& ui,bool full) const;
    void restore_dialog_area(Ui& ui) const;
};

#endif
