#include "ui.hpp"

static const uint16_t ui_themes[2][(uint8_t)UiColorRole::Count]={
    {0x0883U,0x10C5U,0x2A4CU,0x7E5FU,0x1A90U,0xE79FU,0xADFAU,0xFE4AU,0x29C2U,0x7454U,0x34FAU,0x9EFFU,0xEACBU,0xFE4AU,0xF800U},
    {0xEF9FU,0xFFFFU,0x8D57U,0x1B75U,0x12CCU,0x1105U,0x5390U,0xDC40U,0xDF7FU,0x5B90U,0x24CEU,0x1329U,0xDA69U,0xDCE2U,0xF800U}
};
static UiThemeMode ui_theme_mode=UiThemeMode::Dark;

uint16_t Ui_color(UiColorRole role){return ui_themes[(uint8_t)ui_theme_mode][(uint8_t)role];}

uint8_t Ui::init(uint8_t brightness_percent){
    if(Lcd_init()!=0U){return 1U;}
    if(Lcd_set_spi_speed(LCD_SPI_SPEED_NORMAL_HZ)!=0U){return 1U;}
    Lcd_set_backlight(brightness_percent);
    clear();
    return 0U;
}

void Ui::set_theme(UiThemeMode mode){ui_theme_mode=mode==UiThemeMode::Light?UiThemeMode::Light:UiThemeMode::Dark;}
UiThemeMode Ui::theme(void) const{return ui_theme_mode;}
void Ui::set_brightness(uint8_t percent){Lcd_set_backlight(percent);}
void Ui::clear(void){Lcd_clear(UI_COLOR_BACKGROUND);}
void Ui::fill(uint16_t x,uint16_t y,uint16_t width,uint16_t height,uint16_t color){Lcd_fill_rect(x,y,width,height,color);}
void Ui::focus_frame(uint16_t x,uint16_t y,uint16_t width,uint16_t height,bool selected){Lcd_draw_rect(x,y,width,height,selected?UI_COLOR_FOCUS:UI_COLOR_LINE);}

void Ui::text(uint16_t x,uint16_t y,UiTextId id,uint16_t color,uint16_t background,uint8_t size){Lcd_draw_text(x,y,Ui_text_get(id),color,background,size);}
void Ui::text_transparent(uint16_t x,uint16_t y,UiTextId id,uint16_t color,uint8_t size){Lcd_draw_text_transparent(x,y,Ui_text_get(id),color,size);}
void Ui::ascii(uint16_t x,uint16_t y,const char* value,uint16_t color,uint16_t background,uint8_t size){Lcd_draw_text(x,y,value,color,background,size);}

void Ui::ascii_fit(uint16_t x,uint16_t y,uint16_t width,const char* value,uint16_t color,uint16_t background){
    uint8_t size=text_width(value,16U)<=width?16U:12U;
    ascii(x,y,value,color,background,size);
}

uint16_t Ui::text_width(const char* value,uint8_t size){
    uint16_t width=0U,index=0U;
    uint8_t ascii_width=(uint8_t)(size/2U);
    if(value==NULL){return 0U;}
    while(value[index]!='\0'){
        uint8_t first=(uint8_t)value[index];
        if(first<0x80U){width=(uint16_t)(width+ascii_width);index++;}
        else if(value[index+1U]!='\0'){width=(uint16_t)(width+size);index=(uint16_t)(index+2U);}
        else{break;}
    }
    return width;
}

void Ui::centered(uint16_t y,UiTextId id,uint16_t color,uint8_t size){
    const char* value=Ui_text_get(id);uint16_t width=text_width(value,size);
    text(width<LCD_WIDTH?(uint16_t)((LCD_WIDTH-width)/2U):0U,y,id,color,UI_COLOR_BACKGROUND,size);
}

void Ui::centered_ascii(uint16_t y,const char* value,uint16_t color,uint8_t size){
    uint16_t width=text_width(value,size);
    ascii(width<LCD_WIDTH?(uint16_t)((LCD_WIDTH-width)/2U):0U,y,value,color,UI_COLOR_BACKGROUND,size);
}

void Ui::battery_icon(uint16_t x,uint16_t y,uint8_t level){
    uint16_t outline=level==0U?UI_COLOR_DANGER:UI_COLOR_MUTED;
    if(level>5U){level=5U;}
    fill(x,y,31U,12U,UI_COLOR_PANEL);
    Lcd_draw_rect(x,y,28U,12U,outline);
    fill((uint16_t)(x+28U),(uint16_t)(y+3U),3U,6U,outline);
    for(uint8_t i=0U;i<5U;i++){
        fill((uint16_t)(x+2U+i*5U),(uint16_t)(y+2U),4U,8U,i<level?UI_COLOR_PRIMARY:UI_COLOR_PANEL);
    }
}

void Ui::header(const char* title,const char* time,uint8_t battery_level){
    fill(0U,0U,LCD_WIDTH,28U,UI_COLOR_PANEL);
    ascii(6U,6U,title,UI_COLOR_PRIMARY,UI_COLOR_PANEL,16U);
    uint16_t width=text_width(time,16U);
    ascii((uint16_t)((LCD_WIDTH-width)/2U),6U,time,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    battery_icon(282U,8U,battery_level);
    fill(0U,27U,LCD_WIDTH,1U,UI_COLOR_LINE);
}

void Ui::menu_item(uint16_t y,UiTextId text_id,bool selected,bool enabled){
    uint16_t background=selected?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL;
    uint16_t border=selected?UI_COLOR_FOCUS:UI_COLOR_LINE;
    uint16_t foreground=enabled?(selected?UI_COLOR_FOCUS:UI_COLOR_TEXT):UI_COLOR_MUTED;
    fill(54U,y,212U,34U,background);Lcd_draw_rect(54U,y,212U,34U,border);
    const char* value=Ui_text_get(text_id);uint16_t width=text_width(value,16U);
    Lcd_draw_text(width<208U?(uint16_t)(56U+(208U-width)/2U):56U,(uint16_t)(y+9U),value,foreground,background,16U);
}

void Ui::menu_item_ascii(uint16_t y,const char* value,bool selected,bool enabled){
    uint16_t background=selected?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL;
    uint16_t border=selected?UI_COLOR_FOCUS:UI_COLOR_LINE;
    uint16_t foreground=enabled?(selected?UI_COLOR_FOCUS:UI_COLOR_TEXT):UI_COLOR_MUTED;
    fill(54U,y,212U,34U,background);Lcd_draw_rect(54U,y,212U,34U,border);
    uint16_t width=text_width(value,16U);
    Lcd_draw_text(width<208U?(uint16_t)(56U+(208U-width)/2U):56U,(uint16_t)(y+9U),value,foreground,background,16U);
}

void Ui::large_button(uint16_t x,uint16_t y,uint16_t width,UiTextId text_id,bool selected){
    uint16_t background=selected?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL;
    uint16_t border=selected?UI_COLOR_FOCUS:UI_COLOR_LINE;
    uint16_t foreground=selected?UI_COLOR_FOCUS:UI_COLOR_TEXT;
    fill(x,y,width,44U,background);Lcd_draw_rect(x,y,width,44U,border);
    const char* value=Ui_text_get(text_id);uint16_t text_width_value=text_width(value,24U);
    Lcd_draw_text(text_width_value<width?(uint16_t)(x+(width-text_width_value)/2U):x,(uint16_t)(y+10U),value,foreground,background,24U);
}

void Ui::dialog(UiTextId title_id,uint8_t selected){
    fill(30U,42U,260U,156U,UI_COLOR_PANEL);Lcd_draw_rect(30U,42U,260U,156U,UI_COLOR_FOCUS);
    const char* title=Ui_text_get(title_id);uint16_t width=text_width(title,16U);
    Lcd_draw_text(width<256U?(uint16_t)(32U+(256U-width)/2U):32U,58U,title,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    menu_item(104U,UiTextId::Cancel,selected==0U,true);
    menu_item(146U,UiTextId::Confirm,selected==1U,true);
}

void Ui::lr_hint(uint16_t x,uint16_t y,uint16_t width,uint16_t background,bool left_enabled,bool right_enabled){
    ascii(x,y,"<",left_enabled?UI_COLOR_FOCUS:UI_COLOR_MUTED,background,16U);
    ascii((uint16_t)(x+width-8U),y,">",right_enabled?UI_COLOR_FOCUS:UI_COLOR_MUTED,background,16U);
}

void Ui::filled_circle(int16_t x,int16_t y,uint8_t radius,uint16_t color){
    for(int16_t row=-(int16_t)radius;row<=(int16_t)radius;row++){
        int16_t half=radius;
        while(half>0&&half*half+row*row>(int16_t)radius*(int16_t)radius){half--;}
        fill((uint16_t)(x-half),(uint16_t)(y+row),(uint16_t)(half*2+1),1U,color);
    }
}

static char ui_keyboard_char(uint8_t key){
    if(key<26U){return (char)('A'+key);}
    if(key<36U){return (char)('0'+key-26U);}
    return ' ';
}

void Ui::keyboard_frame(const char* value){
    fill(8U,34U,304U,38U,UI_COLOR_PANEL);Lcd_draw_rect(8U,34U,304U,38U,UI_COLOR_LINE);
    ascii(16U,46U,value,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    for(uint8_t key=0U;key<37U;key++){keyboard_key(key,false);}
}

void Ui::keyboard_key(uint8_t key,bool selected){
    if(key>=37U){return;}
    uint8_t row=(uint8_t)(key/10U),column=(uint8_t)(key%10U);
    uint16_t x=(uint16_t)(7U+column*31U),y=(uint16_t)(82U+row*37U);
    uint16_t background=selected?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL;
    fill(x,y,28U,31U,background);Lcd_draw_rect(x,y,28U,31U,selected?UI_COLOR_FOCUS:UI_COLOR_LINE);
    char label[2]={ui_keyboard_char(key),'\0'};
    if(key==36U){label[0]='_';}
    ascii((uint16_t)(x+10U),(uint16_t)(y+9U),label,selected?UI_COLOR_FOCUS:UI_COLOR_TEXT,background,12U);
}

void Ui::password_line(uint16_t y,uint8_t count){
    char value[16];uint8_t index=0U;
    for(uint8_t i=0U;i<8U;i++){value[index++]=i<count?'.':'_';if(i!=7U){value[index++]=' ';}}
    value[index]='\0';
    fill(70U,y,180U,24U,UI_COLOR_PANEL);focus_frame(70U,y,180U,24U,false);ascii(100U,(uint16_t)(y+5U),value,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
}

void Ui::grid_cell(uint8_t column,uint8_t row,uint8_t cell_size,uint16_t color){
    if(cell_size<3U){return;}
    uint16_t x=(uint16_t)column*cell_size,y=(uint16_t)row*cell_size;
    fill(x,y,cell_size,cell_size,UI_COLOR_BACKGROUND);
    fill((uint16_t)(x+1U),(uint16_t)(y+1U),(uint16_t)(cell_size-2U),(uint16_t)(cell_size-2U),color);
}

void Ui::grid_cell_clear(uint8_t column,uint8_t row,uint8_t cell_size){
    if(cell_size==0U){return;}
    fill((uint16_t)column*cell_size,(uint16_t)row*cell_size,cell_size,cell_size,UI_COLOR_BACKGROUND);
}
