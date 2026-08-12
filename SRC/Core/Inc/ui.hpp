#ifndef __UI_HPP__
#define __UI_HPP__

#include "lcd.h"
#include "ui_text.hpp"

enum class UiThemeMode : uint8_t {Dark=0U,Light=1U};
enum class UiColorRole : uint8_t {
    Background=0U,Panel,Line,Primary,PrimaryDark,Text,Muted,Focus,FocusBackground,
    Wall,Snake,SnakeHead,Food,Gold,Danger,Count
};

uint16_t Ui_color(UiColorRole role);

#define UI_COLOR_BACKGROUND       Ui_color(UiColorRole::Background)
#define UI_COLOR_PANEL            Ui_color(UiColorRole::Panel)
#define UI_COLOR_LINE             Ui_color(UiColorRole::Line)
#define UI_COLOR_PRIMARY          Ui_color(UiColorRole::Primary)
#define UI_COLOR_PRIMARY_DARK     Ui_color(UiColorRole::PrimaryDark)
#define UI_COLOR_TEXT             Ui_color(UiColorRole::Text)
#define UI_COLOR_MUTED            Ui_color(UiColorRole::Muted)
#define UI_COLOR_FOCUS            Ui_color(UiColorRole::Focus)
#define UI_COLOR_FOCUS_BG         Ui_color(UiColorRole::FocusBackground)
#define UI_COLOR_WALL             Ui_color(UiColorRole::Wall)
#define UI_COLOR_SNAKE            Ui_color(UiColorRole::Snake)
#define UI_COLOR_SNAKE_HEAD       Ui_color(UiColorRole::SnakeHead)
#define UI_COLOR_FOOD             Ui_color(UiColorRole::Food)
#define UI_COLOR_GOLD             Ui_color(UiColorRole::Gold)
#define UI_COLOR_DANGER           Ui_color(UiColorRole::Danger)

class Ui {
public:
    uint8_t init(uint8_t brightness_percent);
    void set_theme(UiThemeMode mode);
    UiThemeMode theme(void) const;
    void set_brightness(uint8_t percent);
    void clear(void);
    void fill(uint16_t x,uint16_t y,uint16_t width,uint16_t height,uint16_t color);
    void focus_frame(uint16_t x,uint16_t y,uint16_t width,uint16_t height,bool selected);
    void text(uint16_t x,uint16_t y,UiTextId id,uint16_t color,uint16_t background,uint8_t size);
    void text_transparent(uint16_t x,uint16_t y,UiTextId id,uint16_t color,uint8_t size);
    void ascii(uint16_t x,uint16_t y,const char* value,uint16_t color,uint16_t background,uint8_t size);
    void ascii_fit(uint16_t x,uint16_t y,uint16_t width,const char* value,uint16_t color,uint16_t background);
    void centered(uint16_t y,UiTextId id,uint16_t color,uint8_t size);
    void centered_ascii(uint16_t y,const char* value,uint16_t color,uint8_t size);
    void header(const char* title,const char* time,uint8_t battery_level);
    void menu_item(uint16_t y,UiTextId text_id,bool selected,bool enabled);
    void menu_item_ascii(uint16_t y,const char* value,bool selected,bool enabled);
    void large_button(uint16_t x,uint16_t y,uint16_t width,UiTextId text_id,bool selected);
    void dialog(UiTextId title_id,uint8_t selected);
    void lr_hint(uint16_t x,uint16_t y,uint16_t width,uint16_t background,bool left_enabled=true,bool right_enabled=true);
    void filled_circle(int16_t x,int16_t y,uint8_t radius,uint16_t color);
    void keyboard_frame(const char* value);
    void keyboard_key(uint8_t key,bool selected);
    void password_line(uint16_t y,uint8_t count);
    void grid_cell(uint8_t column,uint8_t row,uint8_t cell_size,uint16_t color);
    void grid_cell_clear(uint8_t column,uint8_t row,uint8_t cell_size);
    void battery_icon(uint16_t x,uint16_t y,uint8_t level);
    static uint16_t text_width(const char* text,uint8_t size);
};

#endif
