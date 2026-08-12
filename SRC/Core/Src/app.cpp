#include "app.hpp"
#include "audio.h"
#include "competitive_snake_game.hpp"
#include "flash.h"
#include "flappy_bird_game.hpp"
#include "game_2048.hpp"
#include "gobang_game.hpp"
#include "input.hpp"
#include "maze_game.hpp"
#include "minesweeper_game.hpp"
#include "power.h"
#include "rng.h"
#include "rtc.h"
#include "save.hpp"
#include "snake_game.hpp"
#include "tetris_game.hpp"
#include "ui.hpp"
#include "ui_sound.hpp"

namespace {

enum class PageId : uint8_t {
    Startup=0U,
    Home,
    About,
    GameInfo,
    GameSelect,
    Playing,
    GameMenu,
    Pause,
    Result,
    Settings,
    DateTime,
    Clock,
    SystemStatus,
    Records,
    ConfirmNew,
    ConfirmRestart,
    ConfirmEndGame,
    ConfirmResetSettings,
    ConfirmFactoryReset,
    ConfirmShutdown,
    ProfileList,
    ProfileDetail,
    NameInput,
    PasswordInput,
    ConfirmDelete,
    StorageError
};

enum class PasswordPurpose : uint8_t {CreateFirst=0U,CreateConfirm,SwitchPlayer,DeletePlayer,FactoryReset};

static const uint8_t brightness_levels[5]={20U,40U,60U,80U,100U};
static const uint32_t auto_dim_ms[4]={30000UL,60000UL,120000UL,0UL};
static const uint32_t auto_off_ms[3]={60000UL,120000UL,300000UL};
static const char product_name[]="Arcbit";
static const char product_version[]="v3.0";
static const char product_revision_date[]="260812";
static const uint8_t settings_page_starts[3]={0U,6U,12U};
static const uint8_t settings_page_counts[3]={6U,6U,1U};
static const uint8_t settings_count=13U;
static const uint32_t system_password_salt=0xA6C2F19DUL;
static const uint32_t system_password_hash=0xA8360A65UL; // Fixed system password: 31424132
static Ui ui;
static SnakeGame snake;
static CompetitiveSnakeGame competitive_snake;
static GobangGame gobang;
static Game2048 game_2048;
static FlappyBirdGame flappy_bird;
static TetrisGame tetris;
static MinesweeperGame minesweeper;
static MazeGame maze;
static Game* const games[]={&snake,&competitive_snake,&gobang,&game_2048,&flappy_bird,&tetris,&minesweeper,&maze};
static_assert(sizeof(games)/sizeof(games[0])<=GAME_CAPACITY,"Registered games exceed game capacity");
static PlayerDirectory player_directory;
static PlayerDirectory directory_candidate;
static PlayerDirectory directory_verify;
static AppProfile profile;
static AppProfile profile_candidate;
static AppProfile profile_verify;
static GameCatalog game_catalog;
static GameCatalog game_catalog_candidate;
static GameCatalog game_catalog_verify;
static GameDataStore game_data;
static GameDataStore game_data_candidate;
static GameDataStore game_data_verify;
static uint8_t snapshot_buffer[SAVE_SNAPSHOT_CAPACITY];
static PageId page=PageId::Startup;
static PageId settings_return=PageId::Home;
static PageId datetime_return=PageId::Settings;
static PageId error_return=PageId::Home;
static uint8_t focus=0U;
static uint8_t previous_focus=0U;
static uint8_t dirty_field=0U;
static uint8_t entry_game_index=0U;
static uint8_t active_game_index=0U;
static uint8_t selected_variant=0U;
static uint8_t previous_variant=0U;
static bool select_page_dirty=false;
static uint8_t selected_profile=0U;
static uint8_t active_player=0xFFU;
static PasswordPurpose password_purpose=PasswordPurpose::CreateFirst;
static char name_buffer[SAVE_PLAYER_NAME_LENGTH+1U]={0};
static uint8_t name_length=0U;
static uint8_t keyboard_focus=0U;
static uint8_t previous_key=0U;
static uint8_t password_buffer[8]={0};
static uint8_t password_first[8]={0};
static uint8_t password_count=0U;
static uint8_t settings_row=0U;
static uint8_t datetime_field=0U;
static Rtc_datetime edit_time={2026U,1U,1U,4U,12U,0U,0U};
static bool flash_ready=false;
static bool font_ready=false;
static bool rtc_ready=false;
static bool ui_ready=false;
static uint16_t snapshot_available_mask=0U;
static bool game_active=false;
static bool profile_dirty=false;
static bool game_catalog_dirty=false;
static bool game_data_dirty=false;
static bool dimmed=false;
static bool shutdown_failed=false;
static bool startup_storage_error=false;
static bool factory_reset_pending=false;
static bool first_page_draw=true;
static bool focus_dirty=false;
static bool value_dirty=false;
static bool extra_value_dirty=false;
static bool variant_dirty=false;
static bool status_dirty=false;
static bool new_record=false;
static bool new_unlock=false;
static uint8_t pending_clear_game_id=0U;
static uint32_t startup_ms=0U;
static uint32_t last_activity_ms=0U;
static uint32_t last_status_ms=0U;
static char status_time[12]={'-','-',' ','-','-',':','-','-','\0'};
static uint8_t status_battery_level=0U;
static uint8_t extra_dirty_field=0U;
static InputRepeatState game_ring_repeat={0U,0U};
static InputRepeatState settings_repeat={0U,0U};
static uint32_t status_battery_mv=0U;
static uint32_t clock_last_tick=0U;
static Rtc_datetime clock_drawn_time={2026U,1U,1U,4U,12U,0U,0U};
static bool clock_drawn_rtc_valid=false;
static uint32_t system_last_tick=0U;
static uint8_t system_status_page=0U;
static uint16_t game_menu_snapshot_length=0U;
static GameRecordSummary record_cache[8][SAVE_PLAYER_COUNT];
static uint8_t records_page=0U;
static bool game_ring_dirty=false;
static uint8_t low_battery_count=0U;
static uint32_t low_battery_until_ms=0U;
static bool low_battery_warning=false;
static bool low_battery_warning_dirty=false;
static bool low_battery_restore_pending=false;

static void u32_text(uint32_t value,char* text){
    char reverse[10];
    uint8_t count=0U;
    if(value==0U){text[0]='0';text[1]='\0';return;}
    while(value!=0U&&count<10U){reverse[count++]=(char)('0'+value%10U);value/=10U;}
    for(uint8_t i=0U;i<count;i++){text[i]=reverse[count-1U-i];}
    text[count]='\0';
}

static void two_digits(uint8_t value,char* text){text[0]=(char)('0'+value/10U);text[1]=(char)('0'+value%10U);}

static uint8_t battery_level_from_mv(uint32_t millivolts){
    if(millivolts>=6200U){return 5U;}
    if(millivolts>=5800U){return 4U;}
    if(millivolts>=5400U){return 3U;}
    if(millivolts>=5000U){return 2U;}
    if(millivolts>=4600U){return 1U;}
    return 0U;
}

static void refresh_system_status(uint32_t now,bool force){
    bool battery_due=(uint32_t)(now-last_status_ms)>=30000UL;
    if(!force&&!battery_due){return;}
    Rtc_datetime current;
    if(rtc_ready&&Rtc_get_datetime(&current)==0U){
        two_digits(current.month,&status_time[0]);status_time[2]='-';
        two_digits(current.day,&status_time[3]);status_time[5]=' ';
        two_digits(current.hour,&status_time[6]);status_time[8]=':';
        two_digits(current.minute,&status_time[9]);status_time[11]='\0';
    }
    else{status_time[0]='-';status_time[1]='-';status_time[2]=' ';status_time[3]='-';status_time[4]='-';status_time[5]=':';status_time[6]='-';status_time[7]='-';status_time[8]='\0';}
    if(battery_due){
        uint32_t battery_mv;
        if(Power_read_battery_mv(&battery_mv)==0U){
            status_battery_mv=battery_mv;status_battery_level=battery_level_from_mv(battery_mv);
            if(battery_mv<4600U){
                if(low_battery_count<3U){low_battery_count++;}
                if(low_battery_count>=3U){low_battery_until_ms=now+3000UL;low_battery_warning=true;low_battery_warning_dirty=true;}
            }
            else{low_battery_count=0U;if(low_battery_warning){low_battery_warning=false;low_battery_until_ms=0U;low_battery_restore_pending=true;}}
        }
        else{status_battery_mv=0U;status_battery_level=0U;}
        last_status_ms=now;
    }
    status_dirty=true;
}

static const char* page_title(PageId value){
    switch(value){
    case PageId::Home:return product_name;
    case PageId::About:return "About";
    case PageId::GameInfo:return games[entry_game_index<sizeof(games)/sizeof(games[0])?entry_game_index:0U]->title();
    case PageId::GameSelect:return games[entry_game_index<sizeof(games)/sizeof(games[0])?entry_game_index:0U]->variant_page_title();
    case PageId::GameMenu:return "Game Menu";
    case PageId::Pause:return "Paused";
    case PageId::Result:return "Result";
    case PageId::Settings:return "Settings";
    case PageId::DateTime:return "Date/Time";
    case PageId::Clock:return "Clock";
    case PageId::SystemStatus:return "System";
    case PageId::Records:return "Records";
    case PageId::ProfileList:return "Players";
    case PageId::ProfileDetail:return "Player";
    case PageId::NameInput:return "Name";
    case PageId::PasswordInput:return "Password";
    default:return product_name;
    }
}

static bool page_has_header(PageId value){
    return value==PageId::Home||value==PageId::About||value==PageId::GameInfo||value==PageId::GameSelect||
           value==PageId::GameMenu||value==PageId::Pause||value==PageId::Result||value==PageId::Settings||value==PageId::DateTime||value==PageId::Clock||value==PageId::SystemStatus||value==PageId::Records||
           value==PageId::ProfileList||value==PageId::ProfileDetail||value==PageId::NameInput||value==PageId::PasswordInput;
}

static void draw_header(void){ui.header(page_title(page),status_time,status_battery_level);status_dirty=false;}

static bool maze_playing_page(PageId value){return value==PageId::Playing&&games[active_game_index<sizeof(games)/sizeof(games[0])?active_game_index:0U]->id()==GameId::Maze;}

static void set_page(PageId next){
    bool stay_in_game_ring=(page==PageId::Home||page==PageId::GameInfo)&&(next==PageId::Home||next==PageId::GameInfo);
    bool ring_transition=stay_in_game_ring&&(page!=next||page==PageId::GameInfo);
    if(!stay_in_game_ring){Input_repeat_reset(&game_ring_repeat);}
    if(page!=next&&(page==PageId::Settings||next==PageId::Settings)){Input_repeat_reset(&settings_repeat);}
    bool was_maze_playing=maze_playing_page(page),will_be_maze_playing=maze_playing_page(next);
    if(was_maze_playing!=will_be_maze_playing){(void)Lcd_set_spi_speed(will_be_maze_playing?LCD_SPI_SPEED_HIGH_HZ:LCD_SPI_SPEED_NORMAL_HZ);}
    page=next;
    focus=0U;
    if(next==PageId::Settings){focus=settings_row;}
    else if(next==PageId::DateTime){focus=datetime_field;}
    else if(next==PageId::ProfileList){focus=selected_profile;}
    previous_focus=0U;
    focus_dirty=false;
    value_dirty=false;
    extra_value_dirty=false;
    variant_dirty=false;
    game_ring_dirty=ring_transition;
    first_page_draw=!ring_transition;
}

static void set_focus(uint8_t next){
    if(next==focus){return;}
    previous_focus=focus;
    focus=next;
    focus_dirty=true;
}

static void open_start_page(void){
    if(!font_ready||startup_storage_error){set_page(PageId::StorageError);return;}
    PageId destination=active_player<SAVE_PLAYER_COUNT?PageId::Home:PageId::ProfileList;
    if(!rtc_ready){datetime_return=destination;set_page(PageId::DateTime);}
    else{set_page(destination);}
}

static void set_value_dirty(uint8_t field){dirty_field=field;value_dirty=true;}
static void set_extra_value_dirty(uint8_t field){extra_dirty_field=field;extra_value_dirty=true;}

static uint8_t game_index(GameId id){
    uint8_t value=(uint8_t)id;
    return value>=1U&&value<=GAME_CAPACITY?(uint8_t)(value-1U):0U;
}

static Game* game_for_id(uint8_t id){
    for(uint8_t i=0U;i<sizeof(games)/sizeof(games[0]);i++){if((uint8_t)games[i]->id()==id){return games[i];}}
    return games[0];
}

static uint8_t game_count(void){return (uint8_t)(sizeof(games)/sizeof(games[0]));}
static Game* entry_game(void){return games[entry_game_index<game_count()?entry_game_index:0U];}
static Game* active_game(void){return games[active_game_index<game_count()?active_game_index:0U];}
static uint8_t game_list_index(GameId id){
    for(uint8_t i=0U;i<game_count();i++){if(games[i]->id()==id){return i;}}
    return 0U;
}

static bool game_id_registered(uint8_t id){
    for(uint8_t i=0U;i<game_count();i++){if((uint8_t)games[i]->id()==id){return true;}}
    return false;
}

static GameCatalogEntry& catalog_entry(GameId id){return game_catalog.games[game_index(id)];}
static const GameCatalogEntry& catalog_entry_const(GameId id){return game_catalog.games[game_index(id)];}
static uint16_t game_bit(GameId id){return (uint16_t)(1U<<game_index(id));}
static bool game_snapshot_available(GameId id){return (snapshot_available_mask&game_bit(id))!=0U;}
static void set_game_snapshot_available(GameId id,bool available){if(available){snapshot_available_mask|=game_bit(id);}else{snapshot_available_mask&=(uint16_t)~game_bit(id);}}
static uint8_t saved_variant(GameId id){return catalog_entry_const(id).selected_variant;}

static void set_saved_variant(GameId id,uint8_t variant){
    GameCatalogEntry& entry=catalog_entry(id);entry.selected_variant=variant;game_catalog_dirty=true;
}

static GameActivity activity_for_game(GameId id){
    const GameCatalogEntry& source=catalog_entry_const(id);
    GameActivity value={source.activity_score,source.activity_progress,source.year,source.month,source.day,source.hour,source.minute,source.selected_variant,source.activity_valid,source.game_id,source.second};
    return value;
}

static void catalog_defaults_for_registered(GameCatalog* target_catalog){
    if(target_catalog==NULL){return;}
    Save_game_catalog_defaults(target_catalog);
    for(uint8_t i=0U;i<game_count();i++){target_catalog->games[game_index(games[i]->id())].selected_variant=games[i]->default_variant();}
}

static bool normalize_game_catalog(GameCatalog* catalog){
    if(catalog==NULL){return false;}bool changed=false;
    if(!game_id_registered(catalog->last_game_id)){catalog->last_game_id=(uint8_t)GameId::Snake;changed=true;}
    for(uint8_t i=0U;i<game_count();i++){
        Game* game=games[i];GameCatalogEntry& entry=catalog->games[game_index(game->id())];
        if(entry.selected_variant>=game->variant_count()){entry.selected_variant=game->default_variant();changed=true;}
    }
    return changed;
}

static GameProgressView progress_for_game(GameId){return {NULL,0U};}

static GameOverview game_overview(GameId id){
    Game* game=game_for_id((uint8_t)id);return game->persistent_overview(saved_variant(id));
}

static void scan_saved_snapshots(void){
    snapshot_available_mask=0U;game_active=false;
    if(active_player>=SAVE_PLAYER_COUNT){return;}
    for(uint8_t i=0U;i<game_count();i++){
        uint16_t length=0U;Game* game=games[i];
        if(Save_load_game_snapshot(active_player,game->id(),snapshot_buffer,sizeof(snapshot_buffer),&length)&&length!=0U&&game->restore(snapshot_buffer,length)){
            set_game_snapshot_available(game->id(),true);
            uint8_t variant=game->result().variant;
            if(catalog_entry_const(game->id()).selected_variant!=variant){catalog_entry(game->id()).selected_variant=variant;game_catalog_dirty=true;}
        }
    }
}

static bool font_resources_valid(void){
    uint8_t glyph16[32];
    uint8_t glyph24[72];
    uint32_t slot=((uint32_t)0xD6U-0x81U)*190UL+((uint32_t)0xD0U-0x80U+63U);
    if(Flash_read(slot*32UL,glyph16,sizeof(glyph16))!=0U||Flash_read(0x000BC000UL+slot*72UL,glyph24,sizeof(glyph24))!=0U){return false;}
    bool mixed16=false,mixed24=false;
    for(uint8_t i=0U;i<sizeof(glyph16);i++){if(glyph16[i]!=0x00U&&glyph16[i]!=0xFFU){mixed16=true;}}
    for(uint8_t i=0U;i<sizeof(glyph24);i++){if(glyph24[i]!=0x00U&&glyph24[i]!=0xFFU){mixed24=true;}}
    return mixed16&&mixed24;
}

static bool profile_equal(const AppProfile& left,const AppProfile& right){
    const uint8_t* a=(const uint8_t*)&left;
    const uint8_t* b=(const uint8_t*)&right;
    for(uint16_t i=0U;i<sizeof(AppProfile);i++){if(a[i]!=b[i]){return false;}}
    return true;
}

static bool game_catalog_equal(const GameCatalog& left,const GameCatalog& right){
    const uint8_t* a=(const uint8_t*)&left;const uint8_t* b=(const uint8_t*)&right;
    for(uint16_t i=0U;i<sizeof(GameCatalog);i++){if(a[i]!=b[i]){return false;}}
    return true;
}

static bool game_data_equal(const GameDataStore& left,const GameDataStore& right){
    const uint8_t* a=(const uint8_t*)&left;const uint8_t* b=(const uint8_t*)&right;
    for(uint16_t i=0U;i<sizeof(GameDataStore);i++){if(a[i]!=b[i]){return false;}}
    return true;
}

static bool apply_game_data(GameDataStore* source,bool* changed){
    if(source==NULL||source->version!=SAVE_GAME_DATA_VERSION){return false;}bool local_changed=false;
    for(uint8_t i=0U;i<game_count();i++){
        Game* game=games[i];GameDataEntry& entry=source->games[game_index(game->id())];
        if(entry.payload_length==0U||!game->persistent_data_valid(entry.payload,entry.payload_length,entry.data_version)){
            for(uint16_t byte=0U;byte<sizeof(entry.payload);byte++){entry.payload[byte]=0U;}
            game->persistent_defaults();entry.data_version=game->persistent_data_version();entry.payload_length=game->make_persistent_data(entry.payload,sizeof(entry.payload));
            if(entry.payload_length==0U){return false;}local_changed=true;
        }
        else{game->load_persistent_data(entry.payload,entry.payload_length);}
    }
    if(changed!=NULL){*changed=local_changed;}return true;
}

static bool capture_game_data(GameDataStore* target){
    if(target==NULL){return false;}if(target->version!=SAVE_GAME_DATA_VERSION){Save_game_data_defaults(target);}
    for(uint8_t i=0U;i<game_count();i++){
        Game* game=games[i];GameDataEntry& entry=target->games[game_index(game->id())];
        for(uint16_t byte=0U;byte<sizeof(entry.payload);byte++){entry.payload[byte]=0U;}
        entry.data_version=game->persistent_data_version();entry.payload_length=game->make_persistent_data(entry.payload,sizeof(entry.payload));
        if(entry.payload_length==0U){return false;}
    }
    return true;
}

static bool directory_equal(const PlayerDirectory& left,const PlayerDirectory& right){
    const uint8_t* a=(const uint8_t*)&left;
    const uint8_t* b=(const uint8_t*)&right;
    for(uint16_t i=0U;i<sizeof(PlayerDirectory);i++){if(a[i]!=b[i]){return false;}}
    return true;
}

static bool save_directory_checked(const PlayerDirectory& directory){
    return Save_store_directory(directory)&&Save_load_directory(&directory_verify)&&directory_equal(directory,directory_verify);
}

static bool save_profile_if_needed(bool force){
    if(!flash_ready||active_player>=SAVE_PLAYER_COUNT){return false;}
    if(!force&&!profile_dirty){return true;}
    if(!Save_store_profile(active_player,profile)){return false;}
    if(!Save_load_profile(active_player,&profile_verify)||!profile_equal(profile,profile_verify)){return false;}
    profile_dirty=false;
    return true;
}

static bool save_game_catalog_if_needed(bool force){
    if(!flash_ready||active_player>=SAVE_PLAYER_COUNT){return false;}
    if(!force&&!game_catalog_dirty){return true;}
    if(!Save_store_game_catalog(active_player,game_catalog)){return false;}
    if(!Save_load_game_catalog(active_player,&game_catalog_verify)||!game_catalog_equal(game_catalog,game_catalog_verify)){return false;}
    game_catalog_dirty=false;return true;
}

static bool save_game_data_if_needed(bool force){
    if(!flash_ready||active_player>=SAVE_PLAYER_COUNT){return false;}
    if(!force&&!game_data_dirty){return true;}
    if(!capture_game_data(&game_data)||!Save_store_game_data(active_player,game_data)){return false;}
    if(!Save_load_game_data(active_player,&game_data_verify)||!game_data_equal(game_data,game_data_verify)){return false;}
    game_data_dirty=false;return true;
}

static void update_game_activity(void){
    if(!game_active){return;}
    Game* game=active_game();
    GameResult result=game->result();
    GameCatalogEntry& entry=catalog_entry(game->id());
    entry.activity_score=result.score;entry.activity_progress=result.length;entry.selected_variant=result.variant;entry.game_id=(uint8_t)game->id();entry.activity_valid=1U;
    Rtc_datetime current;
    if(rtc_ready&&Rtc_get_datetime(&current)==0U){
        entry.year=current.year;entry.month=current.month;entry.day=current.day;entry.hour=current.hour;entry.minute=current.minute;entry.second=current.second;
    }
    else{entry.year=0U;entry.month=0U;entry.day=0U;entry.hour=0U;entry.minute=0U;entry.second=0U;}
    game_catalog.last_game_id=(uint8_t)game->id();game_catalog_dirty=true;
}

static bool save_game_snapshot(void){
    uint16_t length;
    if(!flash_ready||!game_active){return !game_active;}
    length=active_game()->make_snapshot(snapshot_buffer,sizeof(snapshot_buffer));
    GameId id=active_game()->id();
    if(length==0U||active_player>=SAVE_PLAYER_COUNT||!Save_store_game_snapshot(active_player,id,snapshot_buffer,length)){return false;}
    uint16_t verify_length=0U;
    if(!Save_load_game_snapshot(active_player,id,snapshot_buffer,sizeof(snapshot_buffer),&verify_length)||verify_length!=length){return false;}
    set_game_snapshot_available(id,true);
    update_game_activity();
    return save_game_catalog_if_needed(true);
}

static bool restore_game_snapshot(GameId id){
    uint16_t length=0U;
    if(!flash_ready||active_player>=SAVE_PLAYER_COUNT||!Save_load_game_snapshot(active_player,id,snapshot_buffer,sizeof(snapshot_buffer),&length)||length==0U){set_game_snapshot_available(id,false);return false;}
    active_game_index=game_list_index(id);
    if(!active_game()->restore(snapshot_buffer,length)){set_game_snapshot_available(id,false);return false;}
    selected_variant=active_game()->result().variant;
    game_active=true;
    set_game_snapshot_available(id,true);
    return true;
}

static bool restore_game_menu_state(void){
    if(page!=PageId::GameMenu){return true;}
    if(!game_active||game_menu_snapshot_length==0U||!active_game()->restore(snapshot_buffer,game_menu_snapshot_length)){return false;}
    game_menu_snapshot_length=0U;
    return true;
}

static bool clear_game_snapshot(GameId id){
    if(active_player>=SAVE_PLAYER_COUNT||!Save_clear_game_snapshot(active_player,id)){return false;}
    uint16_t length=1U;
    if(!Save_load_game_snapshot(active_player,id,snapshot_buffer,sizeof(snapshot_buffer),&length)||length!=0U){return false;}
    set_game_snapshot_available(id,false);pending_clear_game_id=0U;return true;
}

static void storage_error(PageId return_page){UiSound_stop();error_return=return_page;set_page(PageId::StorageError);UiSound_play(UiSoundId::Error);}

static bool save_all_active(void){
    if(!restore_game_menu_state()){return false;}
    if(!flash_ready){return !game_active;}
    if(active_player>=SAVE_PLAYER_COUNT){return !game_active;}
    if(pending_clear_game_id!=0U&&!clear_game_snapshot((GameId)pending_clear_game_id)){return false;}
    if(game_active&&!save_game_snapshot()){return false;}
    return save_game_data_if_needed(false)&&save_game_catalog_if_needed(false)&&save_profile_if_needed(false);
}

static void request_shutdown(void){
    if(shutdown_failed){return;}
    PageId return_page=page==PageId::GameMenu?PageId::Playing:page;
    if(!save_all_active()){shutdown_failed=true;storage_error(return_page);return;}
    UiSound_play_shutdown();
    Lcd_sleep();
    Power_shutdown();
}

static uint8_t days_in_month(uint16_t year,uint8_t month){
    static const uint8_t days[12]={31U,28U,31U,30U,31U,30U,31U,31U,30U,31U,30U,31U};
    if(month==2U&&((year%4U==0U&&year%100U!=0U)||year%400U==0U)){return 29U;}
    return days[month-1U];
}

static void adjust_datetime(int8_t delta){
    int16_t value;
    switch(datetime_field){
    case 0U:value=(int16_t)edit_time.year+delta;if(value<2000){value=2099;}if(value>2099){value=2000;}edit_time.year=(uint16_t)value;break;
    case 1U:value=(int16_t)edit_time.month+delta;if(value<1){value=12;}if(value>12){value=1;}edit_time.month=(uint8_t)value;break;
    case 2U:value=(int16_t)edit_time.day+delta;if(value<1){value=days_in_month(edit_time.year,edit_time.month);}if(value>days_in_month(edit_time.year,edit_time.month)){value=1;}edit_time.day=(uint8_t)value;break;
    case 3U:value=(int16_t)edit_time.hour+delta;if(value<0){value=23;}if(value>23){value=0;}edit_time.hour=(uint8_t)value;break;
    case 4U:value=(int16_t)edit_time.minute+delta;if(value<0){value=59;}if(value>59){value=0;}edit_time.minute=(uint8_t)value;break;
    default:value=(int16_t)edit_time.weekday+delta;if(value<1){value=7;}if(value>7){value=1;}edit_time.weekday=(uint8_t)value;break;
    }
    uint8_t maximum=days_in_month(edit_time.year,edit_time.month);
    if(edit_time.day>maximum){edit_time.day=maximum;}
}

static void draw_startup(void){
    ui.centered_ascii(55U,product_name,UI_COLOR_PRIMARY,32U);
    ui.centered_ascii(110U,product_version,UI_COLOR_MUTED,24U);
    ui.centered_ascii(150U,"By ChatGPT&Inseven",UI_COLOR_MUTED,16U);
    ui.centered_ascii(190U,font_ready?"READY":(flash_ready?"FONT ERROR":"FLASH ERROR"),font_ready?UI_COLOR_TEXT:UI_COLOR_DANGER,16U);
}

static GameId latest_snapshot_game(void){
    GameId selected=GameId::Snake;bool found=false;uint32_t selected_date=0U,selected_time=0U;
    for(uint8_t i=0U;i<game_count();i++){
        GameId id=games[i]->id();if(!game_snapshot_available(id)){continue;}
        const GameCatalogEntry& entry=catalog_entry_const(id);uint32_t date=(uint32_t)entry.year*10000UL+(uint32_t)entry.month*100UL+entry.day;
        uint32_t time=(uint32_t)entry.hour*3600UL+(uint32_t)entry.minute*60UL+entry.second;
        bool tie=date==selected_date&&time==selected_time;
        if(!found||date>selected_date||(date==selected_date&&time>selected_time)||(tie&&(uint8_t)id==game_catalog.last_game_id)){selected=id;selected_date=date;selected_time=time;found=true;}
    }
    if(found){return selected;}
    return game_id_registered(game_catalog.last_game_id)?(GameId)game_catalog.last_game_id:GameId::Snake;
}

static void format_home_activity(const GameActivity& activity,char* value){
    if(activity.valid==0U||activity.year==0U){value[0]='-';value[1]='-';value[2]='\0';return;}
    two_digits(activity.month,&value[0]);value[2]='-';two_digits(activity.day,&value[3]);value[5]=' ';
    two_digits(activity.hour,&value[6]);value[8]=':';two_digits(activity.minute,&value[9]);value[11]='\0';
}

static void draw_home_identity(void){
    char logo_name[sizeof(product_name)];
    for(uint8_t i=0U;i<sizeof(product_name);i++){
        char character=product_name[i];logo_name[i]=character>='a'&&character<='z'?(char)(character-'a'+'A'):character;
    }
    ui.fill(49U,83U,14U,14U,UI_COLOR_PRIMARY);
    ui.fill(53U,87U,6U,6U,UI_COLOR_GOLD);
    ui.fill(35U,68U,32U,3U,UI_COLOR_FOCUS);
    ui.fill(29U,71U,6U,6U,UI_COLOR_FOCUS);
    ui.fill(26U,77U,3U,24U,UI_COLOR_FOCUS);
    ui.fill(29U,101U,6U,6U,UI_COLOR_FOCUS);
    ui.fill(35U,107U,32U,3U,UI_COLOR_FOCUS);
    ui.fill(67U,104U,6U,3U,UI_COLOR_FOCUS);
    ui.fill(73U,98U,3U,6U,UI_COLOR_FOCUS);
    ui.fill(76U,77U,3U,21U,UI_COLOR_FOCUS);
    ui.filled_circle(73,70,4U,UI_COLOR_GOLD);
    ui.ascii(28U,132U,logo_name,UI_COLOR_PRIMARY,UI_COLOR_BACKGROUND,16U);
}

static void draw_home_content(void){
    GameId home_id=latest_snapshot_game();Game* last_game=game_for_id((uint8_t)home_id);GameActivity activity=activity_for_game(home_id);char recent[12];
    draw_home_identity();
    ui.fill(102U,38U,202U,126U,UI_COLOR_PANEL);Lcd_draw_rect(102U,38U,202U,126U,UI_COLOR_LINE);
    last_game->draw_home_thumbnail(ui,110U,34U,82U,102U);
    if(snapshot_available_mask==0U){ui.fill(108U,138U,56U,20U,UI_COLOR_PANEL);ui.text(112U,140U,UiTextId::NoSave,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);}
    else{ui.fill(108U,138U,56U,20U,UI_COLOR_PANEL);ui.text(112U,140U,UiTextId::HasSave,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);}
    ui.text(204U,54U,last_game->display_name(),UI_COLOR_PRIMARY,UI_COLOR_PANEL,16U);
    ui.text(204U,88U,UiTextId::RecentPlay,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);
    format_home_activity(activity,recent);ui.ascii_fit(204U,116U,92U,recent,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.focus_frame(102U,38U,202U,126U,focus==0U);
    ui.lr_hint(4U,90U,312U,UI_COLOR_BACKGROUND);
    ui.large_button(16U,180U,90U,UiTextId::About,focus==1U);
    ui.large_button(115U,180U,90U,UiTextId::Records,focus==2U);
    ui.large_button(214U,180U,90U,UiTextId::Settings,focus==3U);
}

static void draw_home(void){ui.clear();draw_header();draw_home_content();}

static void draw_about(void){
    ui.clear();draw_header();
    ui.fill(34U,40U,252U,168U,UI_COLOR_PANEL);
    Lcd_draw_rect(34U,40U,252U,168U,UI_COLOR_LINE);
    ui.ascii(124U,52U,product_name,UI_COLOR_PRIMARY,UI_COLOR_PANEL,24U);
    ui.text(64U,90U,UiTextId::Version,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    ui.ascii(208U,90U,product_version,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    ui.text(64U,126U,UiTextId::ProductionDate,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    ui.ascii(208U,126U,product_revision_date,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    ui.text(64U,166U,UiTextId::Creators,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    ui.centered(217U,UiTextId::PromptBack,UI_COLOR_MUTED,16U);
}

static void draw_game_entry_content(void){
    Game* game=entry_game();
    GameOverview overview=game_overview(game->id());
    GameActivity activity=activity_for_game(game->id());
    game->draw_entry_page(ui,activity,overview);
    bool available=game_snapshot_available(game->id());
    game->draw_entry_control(ui,0U,focus==0U,available);
    game->draw_entry_control(ui,1U,focus==1U,available);
    ui.lr_hint(4U,90U,312U,UI_COLOR_BACKGROUND);
}

static void draw_game_entry(void){ui.clear();draw_header();draw_game_entry_content();}

static void draw_game_ring_transition(void){
    ui.fill(16U,36U,288U,138U,UI_COLOR_BACKGROUND);
    ui.fill(16U,178U,288,48U,UI_COLOR_BACKGROUND);
    ui.fill(0U,0U,110U,27U,UI_COLOR_PANEL);
    ui.ascii(6U,6U,page_title(page),UI_COLOR_PRIMARY,UI_COLOR_PANEL,16U);
    if(page==PageId::Home){draw_home_content();}else{draw_game_entry_content();}
    game_ring_dirty=false;
}

static void draw_low_battery_warning(void){
    if(!low_battery_warning){return;}
    if(page==PageId::Playing){
        ui.fill(242U,70U,76U,72U,UI_COLOR_PANEL);ui.text(248U,74U,UiTextId::LowBattery,UI_COLOR_DANGER,UI_COLOR_PANEL,16U);
        ui.text(248U,96U,UiTextId::BatteryPlease,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
        ui.text(248U,118U,UiTextId::BatteryRefill,UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    }
    else{
        ui.fill(0U,0U,278U,27U,UI_COLOR_PANEL);
        ui.text(72U,6U,UiTextId::LowBatteryDetail,UI_COLOR_DANGER,UI_COLOR_PANEL,16U);
    }
    low_battery_warning_dirty=false;
}

static void update_low_battery_warning(uint32_t now){
    if(low_battery_warning&&(int32_t)(now-low_battery_until_ms)>=0){low_battery_warning=false;low_battery_restore_pending=true;}
    if(low_battery_restore_pending){
        low_battery_restore_pending=false;low_battery_warning_dirty=false;
        if(page==PageId::Playing){active_game()->draw(ui,true);}else if(page_has_header(page)){draw_header();}else{first_page_draw=true;}
    }
    if(low_battery_warning&&low_battery_warning_dirty){draw_low_battery_warning();}
}

static void draw_game_select(void){
    Game* game=entry_game();
    ui.clear();draw_header();
    game->draw_variant_select(ui,selected_variant,selected_variant,progress_for_game(game->id()),true);
    select_page_dirty=false;
}

static void draw_pause(void){
    ui.clear();draw_header();
    ui.menu_item(39U,UiTextId::ContinueGame,focus==0U,true);
    ui.menu_item(79U,UiTextId::Restart,focus==1U,true);
    ui.menu_item(119U,UiTextId::EndGame,focus==2U,true);
    ui.menu_item(159U,UiTextId::ReturnHome,focus==3U,true);
    ui.centered(208U,UiTextId::ProgressSaved,UI_COLOR_MUTED,16U);
}

static void draw_result(void){
    char value[16];GameResult result=active_game()->result();
    ui.clear();draw_header();
    UiTextId title=UiTextId::GameOver;uint16_t title_color=UI_COLOR_DANGER;
    if(result.outcome==GameOutcome::LevelComplete){title=UiTextId::LevelComplete;title_color=UI_COLOR_FOCUS;}
    else if(result.outcome==GameOutcome::PlayerVictory){title=UiTextId::PlayerVictory;title_color=UI_COLOR_FOCUS;}
    else if(result.outcome==GameOutcome::AiVictory){title=UiTextId::AiVictory;}
    else if(result.outcome==GameOutcome::Draw){title=UiTextId::DrawGame;title_color=UI_COLOR_GOLD;}
    ui.centered(34U,title,title_color,16U);
    ui.text(60U,58U,result.primary_label,UI_COLOR_MUTED,UI_COLOR_BACKGROUND,16U);u32_text(result.score,value);ui.ascii(150U,58U,value,UI_COLOR_TEXT,UI_COLOR_BACKGROUND,16U);
    if(result.secondary_label!=result.primary_label){ui.text(60U,82U,result.secondary_label,UI_COLOR_MUTED,UI_COLOR_BACKGROUND,16U);u32_text(result.length,value);ui.ascii(150U,82U,value,UI_COLOR_TEXT,UI_COLOR_BACKGROUND,16U);}
    if(new_record){ui.text(210U,58U,UiTextId::NewRecord,UI_COLOR_FOCUS,UI_COLOR_BACKGROUND,16U);}
    if(new_unlock){ui.text(198U,82U,UiTextId::NewMapUnlocked,UI_COLOR_FOCUS,UI_COLOR_BACKGROUND,16U);}
    if(result.outcome==GameOutcome::LevelComplete){
        ui.menu_item(108U,result.variant+1U<active_game()->variant_count()?UiTextId::NextLevel:UiTextId::RetryRound,focus==0U,true);
        ui.menu_item(140U,UiTextId::RetryRound,focus==1U,true);
        ui.menu_item(172U,active_game()->variant_action_text(),focus==2U,true);
        ui.menu_item(204U,UiTextId::ReturnGameEntry,focus==3U,true);
    }
    else{
        ui.menu_item(138U,UiTextId::RetryRound,focus==0U,true);
        ui.menu_item(172U,active_game()->variant_action_text(),focus==1U,true);
        ui.menu_item(206U,UiTextId::ReturnGameEntry,focus==2U,true);
    }
}

static uint8_t settings_page_for(uint8_t row){return row<6U?0U:(row<12U?1U:2U);}

static uint16_t setting_y(uint8_t row){
    uint8_t page_index=settings_page_for(row),local=(uint8_t)(row-settings_page_starts[page_index]);
    return (uint16_t)(34U+local*32U);
}

static void draw_setting_row(uint8_t row){
    char value[12];
    static const UiTextId labels[13]={UiTextId::Sound,UiTextId::SoundDensity,UiTextId::Brightness,UiTextId::Theme,UiTextId::AutoDim,UiTextId::AutoOff,UiTextId::DateTime,UiTextId::Clock,UiTextId::ProfileManagement,UiTextId::SystemStatus,UiTextId::ResetSettings,UiTextId::FactoryReset,UiTextId::Shutdown};
    static const UiTextId volume_labels[5]={UiTextId::VolumeMute,UiTextId::VolumeLow,UiTextId::VolumeMedium,UiTextId::VolumeHigh,UiTextId::VolumeMax};
    static const UiTextId density_labels[3]={UiTextId::VolumeLow,UiTextId::VolumeMedium,UiTextId::VolumeHigh};
    if(row>=settings_count||settings_page_for(row)!=settings_page_for(settings_row)){return;}
    uint16_t y=setting_y(row),background=row==settings_row?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL;
    ui.fill(18U,y,286U,32U,background);
    ui.text(28U,(uint16_t)(y+8U),labels[row],row==settings_row?UI_COLOR_FOCUS:UI_COLOR_TEXT,background,16U);
    if(row<=5U&&row==settings_row){ui.lr_hint(214U,(uint16_t)(y+8U),80U,background);}
    if(row==0U){ui.text(238U,(uint16_t)(y+8U),volume_labels[profile.volume_index],UI_COLOR_MUTED,background,16U);}
    else if(row==1U){ui.text(238U,(uint16_t)(y+8U),density_labels[profile.sound_density],UI_COLOR_MUTED,background,16U);}
    else if(row==2U){u32_text(brightness_levels[profile.brightness_index],value);uint8_t end=0U;while(value[end]!='\0'){end++;}value[end++]='%';value[end]='\0';ui.ascii(238U,(uint16_t)(y+8U),value,UI_COLOR_MUTED,background,16U);}
    else if(row==3U){ui.text(238U,(uint16_t)(y+8U),profile.theme_mode==0U?UiTextId::Dark:UiTextId::Light,UI_COLOR_MUTED,background,16U);}
    else if(row==4U){
        if(profile.auto_dim_index==3U){ui.text(238U,(uint16_t)(y+8U),UiTextId::Never,UI_COLOR_MUTED,background,16U);}
        else{u32_text(profile.auto_dim_index==0U?30U:(uint32_t)profile.auto_dim_index,value);uint8_t end=0U;while(value[end]!='\0'){end++;}value[end++]=profile.auto_dim_index==0U?'s':'m';value[end]='\0';ui.ascii(238U,(uint16_t)(y+8U),value,UI_COLOR_MUTED,background,16U);}
    }
    else if(row==5U){u32_text(auto_off_ms[profile.auto_off_index]/60000UL,value);uint8_t end=0U;while(value[end]!='\0'){end++;}value[end++]='m';value[end]='\0';ui.ascii(238U,(uint16_t)(y+8U),value,UI_COLOR_MUTED,background,16U);}
}

static void draw_settings(void){
    ui.clear();draw_header();
    uint8_t page_index=settings_page_for(settings_row),start=settings_page_starts[page_index],count=settings_page_counts[page_index];
    for(uint8_t i=0U;i<count;i++){draw_setting_row((uint8_t)(start+i));}
    ui.fill(312U,38U,2U,188U,UI_COLOR_LINE);
    ui.fill(309U,(uint16_t)(38U+page_index*70U),8U,48U,UI_COLOR_PRIMARY);
}

static void draw_datetime_cell(uint8_t field){
    char value[6];
    static const UiTextId labels[6]={UiTextId::Year,UiTextId::Month,UiTextId::Day,UiTextId::Hour,UiTextId::Minute,UiTextId::Weekday};
    const uint32_t values[6]={edit_time.year,edit_time.month,edit_time.day,edit_time.hour,edit_time.minute,edit_time.weekday};
    if(field>=6U){return;}
    uint16_t x=(uint16_t)(24U+(field%2U)*148U);uint16_t y=(uint16_t)(43U+(field/2U)*48U);uint16_t background=field==datetime_field?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL;
    ui.fill(x,y,132U,37U,background);ui.text((uint16_t)(x+7U),(uint16_t)(y+10U),labels[field],field==datetime_field?UI_COLOR_FOCUS:UI_COLOR_TEXT,background,16U);u32_text(values[field],value);
    if(field==datetime_field){ui.lr_hint((uint16_t)(x+72U),(uint16_t)(y+9U),54U,background);}
    ui.ascii((uint16_t)(x+88U),(uint16_t)(y+9U),value,UI_COLOR_MUTED,background,16U);
}

static void draw_datetime(void){
    ui.clear();draw_header();
    for(uint8_t i=0U;i<6U;i++){draw_datetime_cell(i);}
    ui.centered(207U,UiTextId::PromptSaveBack,UI_COLOR_MUTED,16U);
}

static const int8_t clock_sin[60]={0,13,26,39,52,63,75,85,94,103,110,116,121,124,126,127,126,124,121,116,110,103,94,85,75,63,52,39,26,13,0,-13,-26,-39,-52,-63,-75,-85,-94,-103,-110,-116,-121,-124,-126,-127,-126,-124,-121,-116,-110,-103,-94,-85,-75,-63,-52,-39,-26,-13};

static int16_t clock_x(uint8_t index,uint8_t length){return (int16_t)(160+(int16_t)clock_sin[index%60U]*length/127);}
static int16_t clock_y(uint8_t index,uint8_t length){return (int16_t)(108-(int16_t)clock_sin[(index+15U)%60U]*length/127);}

static void draw_clock_hand(uint8_t index,uint8_t length,uint16_t color){Lcd_draw_line(160,108,clock_x(index,length),clock_y(index,length),color);}

static void draw_clock_face(void){
    Lcd_draw_circle(160,108,72U,UI_COLOR_LINE);
    for(uint8_t tick=0U;tick<60U;tick+=5U){Lcd_draw_line(clock_x(tick,64U),clock_y(tick,64U),clock_x(tick,69U),clock_y(tick,69U),UI_COLOR_MUTED);}
}

static void draw_clock_hands(const Rtc_datetime& current,uint16_t hour_color,uint16_t minute_color,uint16_t second_color){
    uint8_t hour_index=(uint8_t)(((current.hour%12U)*5U+current.minute/12U)%60U);
    draw_clock_hand(hour_index,38U,hour_color);draw_clock_hand(current.minute,54U,minute_color);draw_clock_hand(current.second,62U,second_color);
}

static void draw_clock_content(bool full){
    Rtc_datetime current={2026U,1U,1U,4U,12U,0U,0U};bool valid=rtc_ready&&Rtc_get_datetime(&current)==0U;
    if(!valid){current={2026U,1U,1U,4U,12U,0U,0U};}
    char date[11],time[9];
    bool date_changed=full||valid!=clock_drawn_rtc_valid||current.year!=clock_drawn_time.year||current.month!=clock_drawn_time.month||current.day!=clock_drawn_time.day;
    if(!full&&!valid&&!clock_drawn_rtc_valid){return;}
    if(full){ui.fill(84U,32U,152U,152U,UI_COLOR_BACKGROUND);ui.fill(64U,184U,192U,50U,UI_COLOR_BACKGROUND);}
    else{draw_clock_hands(clock_drawn_time,UI_COLOR_BACKGROUND,UI_COLOR_BACKGROUND,UI_COLOR_BACKGROUND);}
    draw_clock_face();draw_clock_hands(current,UI_COLOR_TEXT,UI_COLOR_PRIMARY,UI_COLOR_FOCUS);ui.filled_circle(160,108,3U,UI_COLOR_GOLD);
    date[0]=(char)('0'+current.year/1000U%10U);date[1]=(char)('0'+current.year/100U%10U);date[2]=(char)('0'+current.year/10U%10U);date[3]=(char)('0'+current.year%10U);date[4]='-';two_digits(current.month,&date[5]);date[7]='-';two_digits(current.day,&date[8]);date[10]='\0';
    two_digits(current.hour,&time[0]);time[2]=':';two_digits(current.minute,&time[3]);time[5]=':';two_digits(current.second,&time[6]);time[8]='\0';
    if(date_changed){ui.fill(64U,184U,192U,20U,UI_COLOR_BACKGROUND);if(valid){ui.centered_ascii(188U,date,UI_COLOR_MUTED,16U);}else{ui.centered(188U,UiTextId::ClockUnset,UI_COLOR_DANGER,16U);}}
    ui.fill(64U,208U,192U,24U,UI_COLOR_BACKGROUND);ui.centered_ascii(212U,valid?time:"12:00:00",UI_COLOR_TEXT,16U);
    clock_drawn_time=current;clock_drawn_rtc_valid=valid;
}

static void draw_clock(void){ui.clear();draw_header();draw_clock_content(true);clock_last_tick=HAL_GetTick()/1000UL;}

static uint8_t snapshot_count(void){uint8_t count=0U;for(uint8_t bit=0U;bit<GAME_CAPACITY;bit++){if((snapshot_available_mask&(uint16_t)(1U<<bit))!=0U){count++;}}return count;}

static void append_ascii(char* target,uint8_t capacity,const char* suffix){uint8_t end=0U;while(end<capacity&&target[end]!='\0'){end++;}uint8_t source=0U;while((uint8_t)(end+1U)<capacity&&suffix[source]!='\0'){target[end++]=suffix[source++];}if(end<capacity){target[end]='\0';}}

static void draw_status_pair(uint16_t y,UiTextId label,const char* value,uint16_t color=UI_COLOR_TEXT){
    ui.text(26U,y,label,UI_COLOR_MUTED,UI_COLOR_BACKGROUND,16U);ui.ascii_fit(156U,y,148U,value,color,UI_COLOR_BACKGROUND);
}

static void draw_system_battery_value(void){char value[24];if(status_battery_mv==0U){value[0]='-';value[1]='-';value[2]='\0';}else{u32_text(status_battery_mv,value);append_ascii(value,sizeof(value),"mV");}ui.fill(156U,162U,148U,18U,UI_COLOR_BACKGROUND);ui.ascii_fit(156U,162U,148U,value,status_battery_mv==0U?UI_COLOR_DANGER:UI_COLOR_TEXT,UI_COLOR_BACKGROUND);}
static void draw_system_uptime_value(void){char value[24];u32_text(HAL_GetTick()/1000UL,value);append_ascii(value,sizeof(value),"s");ui.fill(156U,192U,148U,18U,UI_COLOR_BACKGROUND);ui.ascii_fit(156U,192U,148U,value,UI_COLOR_TEXT,UI_COLOR_BACKGROUND);}

static void draw_system_status_content(void){
    char value[24];ui.fill(0U,28U,LCD_WIDTH,212U,UI_COLOR_BACKGROUND);
    if(system_status_page==0U){
        draw_status_pair(42U,UiTextId::Version,product_version);
        draw_status_pair(72U,UiTextId::CurrentPlayer,active_player<SAVE_PLAYER_COUNT?player_directory.players[active_player].name:"--");
        u32_text(game_count(),value);append_ascii(value,sizeof(value),"/10");draw_status_pair(102U,UiTextId::RegisteredGames,value);
        u32_text(snapshot_count(),value);draw_status_pair(132U,UiTextId::SaveCount,value);
        draw_status_pair(162U,UiTextId::BatteryVoltage,"");draw_system_battery_value();
        draw_status_pair(192U,UiTextId::Uptime,"");draw_system_uptime_value();
    }
    else if(system_status_page==1U){
        draw_status_pair(42U,UiTextId::Mcu,"STM32F411RE");
        draw_status_pair(72U,UiTextId::Display,"320x240 ILI9341");
        draw_status_pair(102U,UiTextId::ExternalFlash,flash_ready?"W25Q64 8MB OK":"W25Q64 ERROR",flash_ready?UI_COLOR_TEXT:UI_COLOR_DANGER);
        draw_status_pair(132U,UiTextId::RtcStatus,rtc_ready?"OK":"ERROR",rtc_ready?UI_COLOR_TEXT:UI_COLOR_DANGER);
        draw_status_pair(162U,UiTextId::FontStatus,font_ready?"GBK16/24 OK":"GBK ERROR",font_ready?UI_COLOR_TEXT:UI_COLOR_DANGER);
        draw_status_pair(192U,UiTextId::DataFormat,"P5/C2");
    }
    else{
        SaveHealthStats health=Save_health_stats();const char* overall=health.invalid_required_pairs!=0U?"ERROR":(health.degraded_pairs!=0U?"DEGRADED":"OK");uint16_t color=health.invalid_required_pairs!=0U?UI_COLOR_DANGER:(health.degraded_pairs!=0U?UI_COLOR_GOLD:UI_COLOR_TEXT);
        draw_status_pair(42U,UiTextId::SlotOverall,overall,color);u32_text(health.checked_pairs,value);draw_status_pair(72U,UiTextId::SlotChecked,value);
        u32_text(health.healthy_pairs,value);draw_status_pair(102U,UiTextId::SlotHealthy,value);u32_text(health.repaired_pairs,value);draw_status_pair(132U,UiTextId::SlotRepaired,value);
        u32_text(health.degraded_pairs,value);draw_status_pair(162U,UiTextId::SlotDegraded,value,health.degraded_pairs==0U?UI_COLOR_TEXT:UI_COLOR_GOLD);u32_text(health.invalid_required_pairs,value);draw_status_pair(192U,UiTextId::SlotInvalid,value,health.invalid_required_pairs==0U?UI_COLOR_TEXT:UI_COLOR_DANGER);
    }
    static const char* pages[3]={"<  1 / 3  >","<  2 / 3  >","<  3 / 3  >"};ui.centered_ascii(220U,pages[system_status_page<3U?system_status_page:0U],UI_COLOR_PRIMARY,16U);
}

static void draw_system_status(void){ui.clear();draw_header();draw_system_status_content();system_last_tick=HAL_GetTick()/1000UL;}

static bool load_record_cache(void){
    if(!save_game_data_if_needed(true)){return false;}
    for(uint8_t game=0U;game<game_count();game++){for(uint8_t player=0U;player<SAVE_PLAYER_COUNT;player++){record_cache[game][player]={0U,0U,0U,0U,0U};}}
    for(uint8_t player=0U;player<SAVE_PLAYER_COUNT;player++){
        if(player_directory.players[player].occupied==0U){continue;}
        if(!Save_load_game_data(player,&game_data_candidate)){return false;}
        for(uint8_t game=0U;game<game_count();game++){
            const GameDataEntry& entry=game_data_candidate.games[game_index(games[game]->id())];
            record_cache[game][player]=games[game]->record_summary(entry.payload,entry.payload_length,entry.data_version);
        }
    }
    return true;
}

static bool record_is_leader(uint8_t player){
    const GameRecordSummary& candidate=record_cache[records_page][player];if(candidate.valid==0U){return false;}
    for(uint8_t other=0U;other<SAVE_PLAYER_COUNT;other++){
        const GameRecordSummary& value=record_cache[records_page][other];if(value.valid==0U){continue;}
        if(candidate.lower_is_better!=0U){if(value.value<candidate.value){return false;}}
        else if(value.value>candidate.value){return false;}
    }
    return true;
}

static void draw_records(void){
    ui.clear();draw_header();char value[16],mode[16];Game* game=games[records_page<game_count()?records_page:0U];
    ui.centered(34U,game->display_name(),UI_COLOR_PRIMARY,16U);
    ui.text(10U,57U,UiTextId::PlayerName,UI_COLOR_MUTED,UI_COLOR_BACKGROUND,16U);ui.text(116U,57U,UiTextId::Record,UI_COLOR_MUTED,UI_COLOR_BACKGROUND,16U);ui.text(205U,57U,UiTextId::Mode,UI_COLOR_MUTED,UI_COLOR_BACKGROUND,16U);
    for(uint8_t player=0U;player<SAVE_PLAYER_COUNT;player++){
        uint16_t y=(uint16_t)(78U+player*22U);uint16_t background=(player&1U)!=0U?UI_COLOR_PANEL:UI_COLOR_BACKGROUND;ui.fill(6U,y,308U,20U,background);
        bool occupied=player_directory.players[player].occupied!=0U;ui.ascii_fit(10U,(uint16_t)(y+2U),92U,occupied?player_directory.players[player].name:"-",UI_COLOR_TEXT,background);
        const GameRecordSummary& record=record_cache[records_page][player];
        if(!occupied||record.valid==0U){value[0]='-';value[1]='\0';mode[0]='-';mode[1]='\0';}
        else{u32_text(record.value,value);game->format_record_variant(record.variant,mode,sizeof(mode));}
        uint16_t color=record_is_leader(player)?UI_COLOR_GOLD:UI_COLOR_TEXT;ui.ascii_fit(116U,(uint16_t)(y+2U),70U,value,color,background);ui.ascii_fit(205U,(uint16_t)(y+2U),105U,mode,color,background);
    }
    char page_value[16]={'<',' ','0',' ','/',' ','0',' ','>','\0'};page_value[2]=(char)('1'+records_page);page_value[6]=(char)('0'+game_count());ui.centered_ascii(220U,page_value,UI_COLOR_PRIMARY,16U);
}

static void draw_game_menu(void){
    char mode[20];Game* game=active_game();game->format_menu_mode(mode,sizeof(mode));
    ui.clear();draw_header();ui.fill(18U,36U,284U,164U,UI_COLOR_PANEL);Lcd_draw_rect(18U,36U,284U,164U,UI_COLOR_LINE);
    ui.text(30U,48U,UiTextId::Goal,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);ui.text(82U,48U,game->menu_goal_text(),UI_COLOR_TEXT,UI_COLOR_PANEL,16U);
    ui.text(30U,74U,UiTextId::Mode,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);ui.ascii_fit(82U,74U,200U,mode,UI_COLOR_PRIMARY,UI_COLOR_PANEL);
    uint8_t count=game->menu_control_count();if(count>5U){count=5U;}
    for(uint8_t i=0U;i<count;i++){ui.text(38U,(uint16_t)(102U+i*19U),game->menu_control_text(i),UI_COLOR_TEXT,UI_COLOR_PANEL,16U);}
    ui.centered_ascii(218U,"MENU / X BACK",UI_COLOR_MUTED,16U);
}

static void format_datetime(uint16_t year,uint8_t month,uint8_t day,uint8_t hour,uint8_t minute,char* value){
    if(year==0U){value[0]='-';value[1]='-';value[2]='-';value[3]='-';value[4]='-';value[5]='-';value[6]='-';value[7]='-';value[8]=' ';value[9]='-';value[10]='-';value[11]=':';value[12]='-';value[13]='-';value[14]='\0';return;}
    value[0]=(char)('0'+year/1000U%10U);value[1]=(char)('0'+year/100U%10U);value[2]=(char)('0'+year/10U%10U);value[3]=(char)('0'+year%10U);value[4]='-';
    two_digits(month,&value[5]);value[7]='-';two_digits(day,&value[8]);value[10]=' ';two_digits(hour,&value[11]);value[13]=':';two_digits(minute,&value[14]);value[16]='\0';
}

static const GameCatalogEntry* latest_activity(const GameCatalog& value){
    const GameCatalogEntry* latest=NULL;uint32_t latest_date=0U;uint32_t latest_time=0U;
    for(uint8_t i=0U;i<GAME_CAPACITY;i++){
        const GameCatalogEntry& activity=value.games[i];
        if(activity.activity_valid==0U){continue;}
        uint32_t date=(uint32_t)activity.year*10000UL+(uint32_t)activity.month*100UL+activity.day;
        uint32_t time=(uint32_t)activity.hour*3600UL+(uint32_t)activity.minute*60UL+activity.second;
        if(latest==NULL||date>latest_date||(date==latest_date&&time>latest_time)){latest=&activity;latest_date=date;latest_time=time;}
    }
    return latest;
}

static void draw_profile_row(uint8_t slot){
    if(slot>=SAVE_PLAYER_COUNT){return;}
    char date[18];uint16_t y=(uint16_t)(35U+slot*32U);bool selected=slot==selected_profile;uint16_t background=selected?UI_COLOR_FOCUS_BG:UI_COLOR_PANEL;
    ui.fill(20U,y,280U,28U,background);
    const PlayerMeta& meta=player_directory.players[slot];
    if(meta.occupied!=0U){
        ui.ascii_fit(30U,(uint16_t)(y+6U),112U,meta.name,selected?UI_COLOR_FOCUS:UI_COLOR_TEXT,background);
        format_datetime(meta.year,meta.month,meta.day,meta.hour,meta.minute,date);
        ui.ascii_fit(150U,(uint16_t)(y+8U),140U,date,UI_COLOR_MUTED,background);
        if(slot==active_player){ui.ascii(282U,(uint16_t)(y+6U),"*",UI_COLOR_GOLD,background,16U);}
    }
    else{ui.text(30U,(uint16_t)(y+6U),UiTextId::NewProfile,selected?UI_COLOR_FOCUS:UI_COLOR_TEXT,background,16U);}
}

static void draw_profile_list(void){
    ui.clear();draw_header();
    for(uint8_t i=0U;i<SAVE_PLAYER_COUNT;i++){draw_profile_row(i);}
    ui.centered(224U,UiTextId::PromptBack,UI_COLOR_MUTED,16U);
}

static void draw_profile_detail(void){
    char date[18];const PlayerMeta& meta=player_directory.players[selected_profile];const GameCatalogEntry* recent=latest_activity(game_catalog_candidate);
    ui.clear();draw_header();ui.fill(18U,34U,284U,112U,UI_COLOR_PANEL);Lcd_draw_rect(18U,34U,284U,112U,UI_COLOR_LINE);
    ui.text(30U,48U,UiTextId::PlayerName,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);ui.ascii_fit(150U,48U,142U,meta.name,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(30U,80U,UiTextId::CreatedTime,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);format_datetime(meta.year,meta.month,meta.day,meta.hour,meta.minute,date);ui.ascii_fit(150U,82U,142U,date,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.text(30U,112U,UiTextId::RecentPlay,UI_COLOR_MUTED,UI_COLOR_PANEL,16U);format_datetime(recent==NULL?0U:recent->year,recent==NULL?0U:recent->month,recent==NULL?0U:recent->day,recent==NULL?0U:recent->hour,recent==NULL?0U:recent->minute,date);ui.ascii_fit(150U,114U,142U,date,UI_COLOR_TEXT,UI_COLOR_PANEL);
    ui.menu_item(162U,UiTextId::EnterProfile,focus==0U,true);
    ui.menu_item(200U,UiTextId::DeleteProfile,focus==1U,player_directory.player_count>1U);
}

static void draw_name_input(void){
    ui.clear();draw_header();ui.keyboard_frame(name_buffer);ui.keyboard_key(keyboard_focus,true);
    ui.centered_ascii(230U,"Y INPUT  X DELETE  A OK",UI_COLOR_MUTED,12U);
}

static void draw_password_input(void){
    ui.clear();draw_header();
    ui.centered(48U,password_purpose==PasswordPurpose::CreateConfirm?UiTextId::PasswordAgain:UiTextId::Password,UI_COLOR_TEXT,16U);
    ui.password_line(82U,password_count);
    ui.centered_ascii(126U,"UP=1 RIGHT=2 DOWN=3 LEFT=4",UI_COLOR_MUTED,16U);
    ui.centered_ascii(166U,"8 KEYS AUTO SUBMIT",UI_COLOR_MUTED,16U);
    ui.centered_ascii(200U,"X DELETE / CANCEL",UI_COLOR_MUTED,16U);
}

static void draw_storage_error(void){
    ui.clear();
    ui.centered_ascii(65U,font_ready?"STORAGE ERROR":"RESOURCE ERROR",UI_COLOR_DANGER,24U);
    ui.centered_ascii(112U,flash_ready?"GBK FONT / SAVE ERROR":"FLASH INIT FAILED",UI_COLOR_TEXT,16U);
    ui.centered_ascii(158U,"Y RETRY   X BACK",UI_COLOR_MUTED,16U);
}

static void draw_page(void){
    switch(page){
    case PageId::Startup:draw_startup();break;
    case PageId::Home:draw_home();break;
    case PageId::About:draw_about();break;
    case PageId::GameInfo:draw_game_entry();break;
    case PageId::GameSelect:draw_game_select();break;
    case PageId::Playing:active_game()->draw(ui,true);break;
    case PageId::GameMenu:draw_game_menu();break;
    case PageId::Pause:draw_pause();break;
    case PageId::Result:draw_result();break;
    case PageId::Settings:draw_settings();break;
    case PageId::DateTime:draw_datetime();break;
    case PageId::Clock:draw_clock();break;
    case PageId::SystemStatus:draw_system_status();break;
    case PageId::Records:draw_records();break;
    case PageId::ConfirmNew:ui.clear();ui.dialog(UiTextId::DeleteSnapshotPrompt,focus);break;
    case PageId::ConfirmRestart:ui.clear();ui.dialog(UiTextId::Restart,focus);break;
    case PageId::ConfirmEndGame:ui.clear();ui.dialog(UiTextId::EndGamePrompt,focus);break;
    case PageId::ConfirmResetSettings:ui.clear();ui.dialog(UiTextId::ResetSettingsPrompt,focus);break;
    case PageId::ConfirmFactoryReset:ui.clear();ui.dialog(UiTextId::FactoryResetPrompt,focus);break;
    case PageId::ConfirmShutdown:ui.clear();ui.dialog(UiTextId::ShutdownPrompt,focus);break;
    case PageId::ProfileList:draw_profile_list();break;
    case PageId::ProfileDetail:draw_profile_detail();break;
    case PageId::NameInput:draw_name_input();break;
    case PageId::PasswordInput:draw_password_input();break;
    case PageId::ConfirmDelete:ui.clear();ui.dialog(UiTextId::DeleteProfilePrompt,focus);break;
    default:draw_storage_error();break;
    }
    first_page_draw=false;
    focus_dirty=false;
    value_dirty=false;
    extra_value_dirty=false;
    variant_dirty=false;
}

static void draw_focus_control(PageId current,uint8_t control){
    if(current==PageId::Home){
        if(control==0U){ui.focus_frame(102U,38U,202U,126U,focus==0U);}
        else if(control==1U){ui.large_button(16U,180U,90U,UiTextId::About,focus==1U);}
        else if(control==2U){ui.large_button(115U,180U,90U,UiTextId::Records,focus==2U);}
        else if(control==3U){ui.large_button(214U,180U,90U,UiTextId::Settings,focus==3U);}
    }
    else if(current==PageId::GameInfo){entry_game()->draw_entry_control(ui,control,focus==control,game_snapshot_available(entry_game()->id()));}
    else if(current==PageId::Pause){
        static const UiTextId labels[4]={UiTextId::ContinueGame,UiTextId::Restart,UiTextId::EndGame,UiTextId::ReturnHome};
        static const uint16_t positions[4]={39U,79U,119U,159U};
        if(control<4U){ui.menu_item(positions[control],labels[control],focus==control,true);}
    }
    else if(current==PageId::Result){
        GameResult result=active_game()->result();
        if(result.outcome==GameOutcome::LevelComplete){
            static const uint16_t positions[4]={108U,140U,172U,204U};
            UiTextId label=control==0U?(result.variant+1U<active_game()->variant_count()?UiTextId::NextLevel:UiTextId::RetryRound):(control==1U?UiTextId::RetryRound:(control==2U?active_game()->variant_action_text():UiTextId::ReturnGameEntry));
            if(control<4U){ui.menu_item(positions[control],label,focus==control,true);}
        }
        else{
            static const uint16_t positions[3]={138U,172U,206U};UiTextId label=control==0U?UiTextId::RetryRound:(control==1U?active_game()->variant_action_text():UiTextId::ReturnGameEntry);
            if(control<3U){ui.menu_item(positions[control],label,focus==control,true);}
        }
    }
    else if(current==PageId::Settings){draw_setting_row(control);}
    else if(current==PageId::DateTime){draw_datetime_cell(control);}
    else if(current==PageId::ProfileList){draw_profile_row(control);}
    else if(current==PageId::ProfileDetail){
        if(control==0U){ui.menu_item(162U,UiTextId::EnterProfile,focus==0U,true);}
        else if(control==1U){ui.menu_item(200U,UiTextId::DeleteProfile,focus==1U,player_directory.player_count>1U);}
    }
    else if(current==PageId::NameInput){ui.keyboard_key(control,keyboard_focus==control);}
    else if(current==PageId::ConfirmNew||current==PageId::ConfirmRestart||current==PageId::ConfirmEndGame||current==PageId::ConfirmDelete||current==PageId::ConfirmResetSettings||current==PageId::ConfirmFactoryReset||current==PageId::ConfirmShutdown){
        if(control==0U){ui.menu_item(104U,UiTextId::Cancel,focus==0U,true);}
        else if(control==1U){ui.menu_item(146U,UiTextId::Confirm,focus==1U,true);}
    }
}

static void draw_partial(void){
    if(game_ring_dirty){draw_game_ring_transition();focus_dirty=false;return;}
    if(focus_dirty){
        draw_focus_control(page,previous_focus);
        draw_focus_control(page,focus);
        focus_dirty=false;
    }
    if(value_dirty){
        if(page==PageId::Settings){draw_setting_row(dirty_field);}
        else if(page==PageId::DateTime){draw_datetime_cell(dirty_field);}
        else if(page==PageId::NameInput){ui.keyboard_frame(name_buffer);ui.keyboard_key(keyboard_focus,true);}
        else if(page==PageId::PasswordInput){ui.password_line(82U,password_count);}
        value_dirty=false;
    }
    if(extra_value_dirty){
        if(page==PageId::DateTime){draw_datetime_cell(extra_dirty_field);}
        extra_value_dirty=false;
    }
    if(variant_dirty){
        Game* game=entry_game();
        game->draw_variant_select(ui,selected_variant,previous_variant,progress_for_game(game->id()),select_page_dirty);
        select_page_dirty=false;
        variant_dirty=false;
    }
}

static void begin_new_game(void){
    GameId id=entry_game()->id();
    if(game_snapshot_available(id)){
        pending_clear_game_id=(uint8_t)id;
        if(!clear_game_snapshot(id)){storage_error(PageId::GameSelect);return;}
    }
    active_game_index=entry_game_index;
    active_game()->start_new(selected_variant);
    game_active=true;
    set_game_snapshot_available(id,false);set_saved_variant(id,selected_variant);game_catalog.last_game_id=(uint8_t)id;game_catalog_dirty=true;
    set_page(PageId::Playing);
    UiSound_play(UiSoundId::Start);
}

static void finish_game(void){
    GameResult result=active_game()->result();
    GameId id=active_game()->id();
    new_record=false;new_unlock=false;
    update_game_activity();
    active_game()->apply_result(result,&new_record,&new_unlock);game_data_dirty=true;
    game_active=false;set_game_snapshot_available(id,false);pending_clear_game_id=(uint8_t)id;
    if(!clear_game_snapshot(id)||!save_game_data_if_needed(true)||!save_game_catalog_if_needed(true)||!save_profile_if_needed(false)){storage_error(PageId::Result);return;}
    set_page(PageId::Result);
    if(new_unlock){UiSound_play(UiSoundId::Unlock);}
    else if(result.outcome==GameOutcome::LevelComplete){UiSound_play(UiSoundId::LevelComplete);}
    else if(new_record){UiSound_play(UiSoundId::Record);}
    else if(result.outcome==GameOutcome::PlayerVictory){UiSound_play(UiSoundId::LevelComplete);}
    else{UiSound_play(UiSoundId::GameOver);}
}

static void enter_pause(void){
    if(!save_game_snapshot()){storage_error(PageId::Playing);return;}
    set_page(PageId::Pause);
    UiSound_play(UiSoundId::PauseSaved);
}

static void return_home_from_game(void);

static void enter_game_menu(void){
    game_menu_snapshot_length=active_game()->make_snapshot(snapshot_buffer,sizeof(snapshot_buffer));
    if(game_menu_snapshot_length==0U){UiSound_play(UiSoundId::Error);return;}
    set_page(PageId::GameMenu);UiSound_play(UiSoundId::Confirm);
}

static void handle_game_menu(const InputEvent& input){
    if(Input_is_pressed(input,InputAction::Home)){
        if(!restore_game_menu_state()){storage_error(PageId::Playing);return;}
        set_page(PageId::Playing);return_home_from_game();return;
    }
    if(Input_is_pressed(input,InputAction::Pause)){
        if(!restore_game_menu_state()){storage_error(PageId::Playing);return;}
        set_page(PageId::Playing);enter_pause();return;
    }
    if(Input_is_pressed(input,InputAction::Menu)||Input_is_pressed(input,InputAction::Cancel)){
        if(!restore_game_menu_state()){storage_error(PageId::Playing);return;}
        set_page(PageId::Playing);UiSound_play(UiSoundId::Resume);
    }
}

static void return_home_from_game(void){
    if(!save_game_snapshot()){storage_error(PageId::Playing);return;}
    game_active=false;
    set_page(PageId::Home);
    UiSound_play(UiSoundId::Back);
}

static void handle_home(const InputEvent& input,uint32_t now_ms){
    if(Input_is_pressed(input,InputAction::Menu)){settings_return=PageId::Home;set_page(PageId::Settings);UiSound_play(UiSoundId::Confirm);return;}
    uint16_t ring_key=Input_repeat_update(&game_ring_repeat,input.state,input.pressed,(uint16_t)(KEY_L|KEY_R),now_ms,300U,180U);
    if(ring_key==KEY_L){entry_game_index=(uint8_t)(game_count()-1U);set_page(PageId::GameInfo);UiSound_play(UiSoundId::PageLeft);return;}
    if(ring_key==KEY_R){entry_game_index=0U;set_page(PageId::GameInfo);UiSound_play(UiSoundId::PageRight);return;}
    if(Input_is_pressed(input,InputAction::Down)&&focus==0U){set_focus(1U);UiSound_play(UiSoundId::Move);}
    else if(Input_is_pressed(input,InputAction::Up)&&focus>=1U&&focus<=3U){set_focus(0U);UiSound_play(UiSoundId::Move);}
    else if(Input_is_pressed(input,InputAction::Left)&&focus>=1U&&focus<=3U){set_focus(focus==1U?3U:(uint8_t)(focus-1U));UiSound_play(UiSoundId::Move);}
    else if(Input_is_pressed(input,InputAction::Right)&&focus>=1U&&focus<=3U){set_focus(focus==3U?1U:(uint8_t)(focus+1U));UiSound_play(UiSoundId::Move);}
    if(!Input_is_pressed(input,InputAction::Confirm)){return;}
    if(focus==0U){
        GameId id=latest_snapshot_game();entry_game_index=game_list_index(id);
        if(game_snapshot_available(id)){if(restore_game_snapshot(id)){set_page(PageId::Playing);UiSound_play(UiSoundId::Resume);}else{storage_error(PageId::Home);}}
        else{set_page(PageId::GameInfo);UiSound_play(UiSoundId::Confirm);}
    }
    else if(focus==1U){set_page(PageId::About);UiSound_play(UiSoundId::Confirm);}
    else if(focus==2U){if(!load_record_cache()){storage_error(PageId::Home);return;}records_page=0U;set_page(PageId::Records);UiSound_play(UiSoundId::Confirm);}
    else{settings_return=PageId::Home;set_page(PageId::Settings);UiSound_play(UiSoundId::Confirm);}
}

static void handle_about(const InputEvent& input){if(Input_is_pressed(input,InputAction::Cancel)){set_page(PageId::Home);UiSound_play(UiSoundId::Back);}}

static void handle_records(const InputEvent& input){
    if(Input_is_pressed(input,InputAction::Cancel)){set_page(PageId::Home);UiSound_play(UiSoundId::Back);return;}
    if(Input_is_pressed(input,InputAction::Action1)){records_page=records_page==0U?(uint8_t)(game_count()-1U):(uint8_t)(records_page-1U);first_page_draw=true;UiSound_play(UiSoundId::PageLeft);}
    else if(Input_is_pressed(input,InputAction::Action2)){records_page=(uint8_t)((records_page+1U)%game_count());first_page_draw=true;UiSound_play(UiSoundId::PageRight);}
}

static void handle_game_info(const InputEvent& input,uint32_t now_ms){
    if(Input_is_pressed(input,InputAction::Cancel)){set_page(PageId::Home);UiSound_play(UiSoundId::Back);return;}
    if(Input_is_pressed(input,InputAction::Menu)){settings_return=PageId::GameInfo;set_page(PageId::Settings);UiSound_play(UiSoundId::Confirm);return;}
    uint16_t ring_key=Input_repeat_update(&game_ring_repeat,input.state,input.pressed,(uint16_t)(KEY_L|KEY_R),now_ms,300U,180U);
    if(ring_key==KEY_L){
        if(entry_game_index==0U){set_page(PageId::Home);}else{entry_game_index--;set_page(PageId::GameInfo);}
        UiSound_play(UiSoundId::PageLeft);return;
    }
    if(ring_key==KEY_R){
        if((uint8_t)(entry_game_index+1U)>=game_count()){set_page(PageId::Home);}else{entry_game_index++;set_page(PageId::GameInfo);}
        UiSound_play(UiSoundId::PageRight);return;
    }
    if(Input_is_pressed(input,InputAction::Left)&&focus==1U){set_focus(0U);UiSound_play(UiSoundId::Move);}
    else if(Input_is_pressed(input,InputAction::Right)&&focus==0U){set_focus(1U);UiSound_play(UiSoundId::Move);}
    if(!Input_is_pressed(input,InputAction::Confirm)){return;}
    GameId id=entry_game()->id();
    if(focus==0U){
        if(game_snapshot_available(id)){if(restore_game_snapshot(id)){set_page(PageId::Playing);UiSound_play(UiSoundId::Resume);}else{storage_error(PageId::GameInfo);}}
        else{selected_variant=saved_variant(id);if(!entry_game()->variant_unlocked(progress_for_game(id),selected_variant)){selected_variant=0U;}begin_new_game();}
    }
    else{
        selected_variant=saved_variant(id);if(!entry_game()->variant_unlocked(progress_for_game(id),selected_variant)){selected_variant=0U;}
        if(game_snapshot_available(id)){set_page(PageId::ConfirmNew);}else{set_page(PageId::GameSelect);}UiSound_play(UiSoundId::Confirm);
    }
}

static void handle_game_select(const InputEvent& input){
    if(Input_is_pressed(input,InputAction::Cancel)){set_page(PageId::GameInfo);UiSound_play(UiSoundId::Back);return;}
    Game* game=entry_game();
    previous_variant=selected_variant;
    GameSelectSignal signal=game->handle_variant_select(input,&selected_variant);
    if(signal==GameSelectSignal::SelectionChanged){variant_dirty=true;UiSound_play(UiSoundId::Move);return;}
    if(signal==GameSelectSignal::PageLeft||signal==GameSelectSignal::PageRight){select_page_dirty=true;variant_dirty=true;UiSound_play(signal==GameSelectSignal::PageLeft?UiSoundId::PageLeft:UiSoundId::PageRight);return;}
    if(signal==GameSelectSignal::Confirmed){
        set_saved_variant(game->id(),selected_variant);
        if(!save_game_catalog_if_needed(true)||!save_profile_if_needed(false)){storage_error(PageId::GameSelect);return;}
        set_page(PageId::GameInfo);UiSound_play(UiSoundId::Saved);return;
    }
    if(Input_is_pressed(input,InputAction::Confirm)){
        if(!game->variant_unlocked(progress_for_game(game->id()),selected_variant)){UiSound_play(UiSoundId::Locked);}
        else{set_saved_variant(game->id(),selected_variant);begin_new_game();}
    }
}

static void handle_pause(const InputEvent& input){
    if(Input_is_pressed(input,InputAction::Pause)||Input_is_pressed(input,InputAction::Cancel)){if(restore_game_snapshot(active_game()->id())){set_page(PageId::Playing);UiSound_play(UiSoundId::Resume);}else{storage_error(PageId::Pause);}return;}
    if(Input_is_pressed(input,InputAction::Up)){set_focus(focus==0U?3U:(uint8_t)(focus-1U));UiSound_play(UiSoundId::Move);}
    else if(Input_is_pressed(input,InputAction::Down)){set_focus((uint8_t)((focus+1U)%4U));UiSound_play(UiSoundId::Move);}
    if(Input_is_pressed(input,InputAction::Confirm)){
        if(focus==0U){if(restore_game_snapshot(active_game()->id())){set_page(PageId::Playing);UiSound_play(UiSoundId::Resume);}else{storage_error(PageId::Pause);}}
        else if(focus==1U){set_page(PageId::ConfirmRestart);UiSound_play(UiSoundId::Confirm);}
        else if(focus==2U){set_page(PageId::ConfirmEndGame);UiSound_play(UiSoundId::Confirm);}
        else{game_active=false;set_page(PageId::Home);UiSound_play(UiSoundId::Back);}
    }
}

static void handle_result(const InputEvent& input){
    GameResult result=active_game()->result();uint8_t count=result.outcome==GameOutcome::LevelComplete?4U:3U;
    if(Input_is_pressed(input,InputAction::Up)){set_focus(focus==0U?(uint8_t)(count-1U):(uint8_t)(focus-1U));UiSound_play(UiSoundId::Move);}
    else if(Input_is_pressed(input,InputAction::Down)){set_focus((uint8_t)((focus+1U)%count));UiSound_play(UiSoundId::Move);}
    if(Input_is_pressed(input,InputAction::Confirm)){
        if(result.outcome==GameOutcome::LevelComplete){
            if(focus==0U){if(selected_variant+1U<active_game()->variant_count()){selected_variant++;}begin_new_game();}
            else if(focus==1U){begin_new_game();}
            else if(focus==2U){set_page(PageId::GameSelect);UiSound_play(UiSoundId::Confirm);}
            else{entry_game_index=active_game_index;set_page(PageId::GameInfo);UiSound_play(UiSoundId::Back);}
        }
        else if(focus==0U){begin_new_game();}
        else if(focus==1U){set_page(PageId::GameSelect);UiSound_play(UiSoundId::Confirm);}
        else{entry_game_index=active_game_index;set_page(PageId::GameInfo);UiSound_play(UiSoundId::Back);}
    }
    else if(Input_is_pressed(input,InputAction::Cancel)||Input_is_pressed(input,InputAction::Home)){set_page(PageId::Home);UiSound_play(UiSoundId::Back);}
}

static void apply_theme(void){ui.set_theme(profile.theme_mode==0U?UiThemeMode::Dark:UiThemeMode::Light);profile_dirty=true;focus_dirty=false;value_dirty=false;extra_value_dirty=false;variant_dirty=false;first_page_draw=true;}

static void adjust_setting(int8_t delta){
    if(settings_row==0U){
        int8_t next=(int8_t)profile.volume_index+delta;if(next<0){next=4;}if(next>4){next=0;}
        if(next==0){UiSound_play(delta<0?UiSoundId::Decrease:UiSoundId::Increase);profile.volume_index=0U;UiSound_set_volume(0U);}
        else{profile.volume_index=(uint8_t)next;UiSound_set_volume(profile.volume_index);UiSound_play(delta<0?UiSoundId::Decrease:UiSoundId::Increase);}
    }
    else if(settings_row==1U){int8_t next=(int8_t)profile.sound_density+delta;if(next<0){next=2;}if(next>2){next=0;}profile.sound_density=(uint8_t)next;UiSound_set_density((UiSoundDensity)profile.sound_density);UiSound_play(delta<0?UiSoundId::Decrease:UiSoundId::Increase);}
    else if(settings_row==2U){int8_t next=(int8_t)profile.brightness_index+delta;if(next<0){next=4;}if(next>4){next=0;}profile.brightness_index=(uint8_t)next;ui.set_brightness(brightness_levels[profile.brightness_index]);UiSound_play(delta<0?UiSoundId::Decrease:UiSoundId::Increase);}
    else if(settings_row==3U){profile.theme_mode=profile.theme_mode==0U?1U:0U;UiSound_play(delta<0?UiSoundId::Decrease:UiSoundId::Increase);apply_theme();return;}
    else if(settings_row==4U){int8_t next=(int8_t)profile.auto_dim_index+delta;if(next<0){next=3;}if(next>3){next=0;}profile.auto_dim_index=(uint8_t)next;UiSound_play(delta<0?UiSoundId::Decrease:UiSoundId::Increase);}
    else if(settings_row==5U){int8_t next=(int8_t)profile.auto_off_index+delta;if(next<0){next=2;}if(next>2){next=0;}profile.auto_off_index=(uint8_t)next;UiSound_play(delta<0?UiSoundId::Decrease:UiSoundId::Increase);}
    else{return;}
    profile_dirty=true;set_value_dirty(settings_row);
}

static bool same_name(const char* left,const char* right){
    uint8_t index=0U;
    while(index<=SAVE_PLAYER_NAME_LENGTH){if(left[index]!=right[index]){return false;}if(left[index]=='\0'){return true;}index++;}
    return true;
}

static void clear_password(void){for(uint8_t i=0U;i<8U;i++){password_buffer[i]=0U;}password_count=0U;}

static void start_password(PasswordPurpose purpose){
    password_purpose=purpose;clear_password();set_page(PageId::PasswordInput);
}

static void start_name_input(uint8_t slot){
    selected_profile=slot;name_length=0U;name_buffer[0]='\0';keyboard_focus=0U;previous_key=0U;set_page(PageId::NameInput);
}

static void apply_loaded_profile(void){
    UiSound_set_volume(profile.volume_index);
    UiSound_set_density((UiSoundDensity)profile.sound_density);
    ui.set_theme(profile.theme_mode==0U?UiThemeMode::Dark:UiThemeMode::Light);
    ui.set_brightness(brightness_levels[profile.brightness_index]);
    profile_dirty=false;
}

static bool store_profile_checked(uint8_t player,const AppProfile& value){
    return Save_store_profile(player,value)&&Save_load_profile(player,&profile_verify)&&profile_equal(value,profile_verify);
}

static bool store_catalog_checked(uint8_t player,const GameCatalog& value){
    return Save_store_game_catalog(player,value)&&Save_load_game_catalog(player,&game_catalog_verify)&&game_catalog_equal(value,game_catalog_verify);
}

static bool store_game_data_checked(uint8_t player,const GameDataStore& value){
    return Save_store_game_data(player,value)&&Save_load_game_data(player,&game_data_verify)&&game_data_equal(value,game_data_verify);
}

static bool load_profile_catalog_view(uint8_t player,AppProfile* target_profile,GameCatalog* target_catalog){
    if(!Save_load_profile(player,target_profile)||!Save_load_game_catalog(player,target_catalog)){return false;}
    normalize_game_catalog(target_catalog);return true;
}

static bool load_player(uint8_t player,AppProfile* target_profile,GameCatalog* target_catalog,GameDataStore* target_data){
    if(!Save_load_profile(player,target_profile)||!Save_load_game_catalog(player,target_catalog)||!Save_load_game_data(player,target_data)){return false;}
    bool catalog_changed=normalize_game_catalog(target_catalog);
    bool entries_changed=false;if(!apply_game_data(target_data,&entries_changed)){return false;}
    if(entries_changed&&!store_game_data_checked(player,*target_data)){return false;}
    if(catalog_changed&&!store_catalog_checked(player,*target_catalog)){return false;}
    return true;
}

static bool create_selected_profile(void){
    if(selected_profile>=SAVE_PLAYER_COUNT||player_directory.players[selected_profile].occupied!=0U){return false;}
    if(active_player<SAVE_PLAYER_COUNT&&!save_all_active()){return false;}
    bool restore_current=active_player<SAVE_PLAYER_COUNT;
    Save_profile_defaults(&profile_candidate);catalog_defaults_for_registered(&game_catalog_candidate);
    for(uint8_t i=0U;i<game_count();i++){games[i]->persistent_defaults();}
    if(!capture_game_data(&game_data_candidate)||!Save_prepare_player(selected_profile)||!store_game_data_checked(selected_profile,game_data_candidate)||
       !store_catalog_checked(selected_profile,game_catalog_candidate)||!store_profile_checked(selected_profile,profile_candidate)){
        if(restore_current){apply_game_data(&game_data,NULL);}return false;
    }
    directory_candidate=player_directory;PlayerMeta& meta=directory_candidate.players[selected_profile];
    uint8_t* bytes=(uint8_t*)&meta;for(uint8_t i=0U;i<sizeof(PlayerMeta);i++){bytes[i]=0U;}
    for(uint8_t i=0U;i<=name_length;i++){meta.name[i]=name_buffer[i];}
    meta.password_salt=Rng_get_int()^HAL_GetTick()^((uint32_t)selected_profile<<24U);
    meta.password_hash=Save_password_hash(password_first,8U,meta.password_salt);
    Rtc_datetime current;
    if(rtc_ready&&Rtc_get_datetime(&current)==0U){meta.year=current.year;meta.month=current.month;meta.day=current.day;meta.hour=current.hour;meta.minute=current.minute;}
    meta.occupied=1U;directory_candidate.player_count=(uint8_t)(directory_candidate.player_count+1U);directory_candidate.active_player=selected_profile;
    if(!save_directory_checked(directory_candidate)){if(restore_current){apply_game_data(&game_data,NULL);}return false;}
    player_directory=directory_candidate;active_player=selected_profile;profile=profile_candidate;game_catalog=game_catalog_candidate;game_data=game_data_candidate;snapshot_available_mask=0U;game_active=false;pending_clear_game_id=0U;game_catalog_dirty=false;game_data_dirty=false;
    apply_loaded_profile();return true;
}

static bool switch_to_selected_profile(void){
    if(selected_profile>=SAVE_PLAYER_COUNT||player_directory.players[selected_profile].occupied==0U){return false;}
    if(selected_profile==active_player){return true;}
    if(active_player<SAVE_PLAYER_COUNT&&!save_all_active()){return false;}
    if(!load_player(selected_profile,&profile_candidate,&game_catalog_candidate,&game_data_candidate)){apply_game_data(&game_data,NULL);return false;}
    directory_candidate=player_directory;directory_candidate.active_player=selected_profile;
    if(!save_directory_checked(directory_candidate)){apply_game_data(&game_data,NULL);return false;}
    player_directory=directory_candidate;active_player=selected_profile;profile=profile_candidate;game_catalog=game_catalog_candidate;game_data=game_data_candidate;active_game_index=game_list_index((GameId)game_catalog.last_game_id);selected_variant=saved_variant(games[active_game_index]->id());game_active=false;pending_clear_game_id=0U;
    apply_loaded_profile();game_catalog_dirty=false;game_data_dirty=false;scan_saved_snapshots();return true;
}

static bool delete_selected_profile(void){
    if(selected_profile>=SAVE_PLAYER_COUNT||player_directory.players[selected_profile].occupied==0U||player_directory.player_count<=1U){return false;}
    if(active_player<SAVE_PLAYER_COUNT&&!save_all_active()){return false;}
    directory_candidate=player_directory;PlayerMeta& meta=directory_candidate.players[selected_profile];uint8_t* bytes=(uint8_t*)&meta;
    for(uint8_t i=0U;i<sizeof(PlayerMeta);i++){bytes[i]=0U;}
    directory_candidate.player_count=(uint8_t)(directory_candidate.player_count-1U);
    bool deleted_active=selected_profile==active_player;if(deleted_active){directory_candidate.active_player=0xFFU;}
    if(!save_directory_checked(directory_candidate)){return false;}
    player_directory=directory_candidate;
    if(deleted_active){active_player=0xFFU;snapshot_available_mask=0U;game_active=false;Save_profile_defaults(&profile);catalog_defaults_for_registered(&game_catalog);Save_game_data_defaults(&game_data);for(uint8_t i=0U;i<game_count();i++){games[i]->persistent_defaults();}game_catalog_dirty=false;game_data_dirty=false;apply_loaded_profile();}
    return true;
}

static bool password_matches_selected(void){
    if(selected_profile>=SAVE_PLAYER_COUNT){return false;}
    const PlayerMeta& meta=player_directory.players[selected_profile];
    return meta.occupied!=0U&&Save_password_hash(password_buffer,8U,meta.password_salt)==meta.password_hash;
}

static void complete_password(void){
    if(password_purpose==PasswordPurpose::CreateFirst){
        for(uint8_t i=0U;i<8U;i++){password_first[i]=password_buffer[i];}
        start_password(PasswordPurpose::CreateConfirm);UiSound_play(UiSoundId::Confirm);return;
    }
    if(password_purpose==PasswordPurpose::CreateConfirm){
        bool equal=true;for(uint8_t i=0U;i<8U;i++){if(password_first[i]!=password_buffer[i]){equal=false;}}
        if(!equal){start_password(PasswordPurpose::CreateFirst);UiSound_play(UiSoundId::Error);return;}
        if(create_selected_profile()){set_page(PageId::Home);UiSound_play(UiSoundId::Saved);}
        else{storage_error(PageId::ProfileList);}
        return;
    }
    if(password_purpose==PasswordPurpose::FactoryReset){
        if(Save_password_hash(password_buffer,8U,system_password_salt)!=system_password_hash){clear_password();set_value_dirty(0U);UiSound_play(UiSoundId::Error);return;}
        clear_password();set_page(PageId::ConfirmFactoryReset);UiSound_play(UiSoundId::Confirm);return;
    }
    if(!password_matches_selected()){clear_password();set_value_dirty(0U);UiSound_play(UiSoundId::Error);return;}
    if(password_purpose==PasswordPurpose::SwitchPlayer){
        if(switch_to_selected_profile()){set_page(PageId::Home);UiSound_play(UiSoundId::Saved);}
        else{storage_error(PageId::ProfileDetail);}
    }
    else{set_page(PageId::ConfirmDelete);UiSound_play(UiSoundId::Confirm);}
}

static void handle_profile_list(const InputEvent& input){
    if(Input_is_pressed(input,InputAction::Cancel)){if(active_player<SAVE_PLAYER_COUNT){set_page(PageId::Settings);UiSound_play(UiSoundId::Back);}return;}
    if(Input_is_pressed(input,InputAction::Up)){uint8_t next=selected_profile==0U?(SAVE_PLAYER_COUNT-1U):(uint8_t)(selected_profile-1U);set_focus(next);selected_profile=focus;UiSound_play(UiSoundId::Move);}
    else if(Input_is_pressed(input,InputAction::Down)){set_focus((uint8_t)((selected_profile+1U)%SAVE_PLAYER_COUNT));selected_profile=focus;UiSound_play(UiSoundId::Move);}
    else if(Input_is_pressed(input,InputAction::Confirm)){
        if(player_directory.players[selected_profile].occupied==0U){start_name_input(selected_profile);}
        else if(load_profile_catalog_view(selected_profile,&profile_candidate,&game_catalog_candidate)){set_page(PageId::ProfileDetail);UiSound_play(UiSoundId::Confirm);}
        else{storage_error(PageId::ProfileList);}
    }
}

static void handle_profile_detail(const InputEvent& input){
    if(Input_is_pressed(input,InputAction::Cancel)){set_page(PageId::ProfileList);UiSound_play(UiSoundId::Back);return;}
    if(Input_is_pressed(input,InputAction::Up)||Input_is_pressed(input,InputAction::Down)){set_focus(focus==0U?1U:0U);UiSound_play(UiSoundId::Move);}
    if(!Input_is_pressed(input,InputAction::Confirm)){return;}
    if(focus==0U){
        start_password(PasswordPurpose::SwitchPlayer);UiSound_play(UiSoundId::Confirm);
    }
    else if(player_directory.player_count<=1U){UiSound_play(UiSoundId::Error);}
    else{start_password(PasswordPurpose::DeletePlayer);UiSound_play(UiSoundId::Confirm);}
}

static void handle_name_input(const InputEvent& input){
    uint8_t row=(uint8_t)(keyboard_focus/10U),column=(uint8_t)(keyboard_focus%10U),next=keyboard_focus;
    if(Input_is_pressed(input,InputAction::Left)&&column>0U){next=(uint8_t)(keyboard_focus-1U);}
    else if(Input_is_pressed(input,InputAction::Right)&&((row<3U&&column<9U)||(row==3U&&column<6U))){next=(uint8_t)(keyboard_focus+1U);}
    else if(Input_is_pressed(input,InputAction::Up)){uint8_t next_row=row==0U?3U:(uint8_t)(row-1U);uint8_t next_column=next_row==3U&&column>6U?6U:column;next=(uint8_t)(next_row*10U+next_column);}
    else if(Input_is_pressed(input,InputAction::Down)){uint8_t next_row=row==3U?0U:(uint8_t)(row+1U);uint8_t next_column=next_row==3U&&column>6U?6U:column;next=(uint8_t)(next_row*10U+next_column);}
    if(next!=keyboard_focus){previous_key=keyboard_focus;keyboard_focus=next;ui.keyboard_key(previous_key,false);ui.keyboard_key(keyboard_focus,true);UiSound_play(UiSoundId::Move);return;}
    if(Input_is_pressed(input,InputAction::Confirm)&&name_length<SAVE_PLAYER_NAME_LENGTH){
        char character=keyboard_focus<26U?(char)('A'+keyboard_focus):(keyboard_focus<36U?(char)('0'+keyboard_focus-26U):' ');
        name_buffer[name_length++]=character;name_buffer[name_length]='\0';set_value_dirty(0U);UiSound_play(UiSoundId::Confirm);return;
    }
    if(Input_is_pressed(input,InputAction::Cancel)){
        if(name_length==0U){set_page(PageId::ProfileList);UiSound_play(UiSoundId::Back);}
        else{name_buffer[--name_length]='\0';set_value_dirty(0U);UiSound_play(UiSoundId::Back);}return;
    }
    if(Input_is_pressed(input,InputAction::Action3)){
        uint8_t first=0U;while(first<name_length&&name_buffer[first]==' '){first++;}
        uint8_t last=name_length;while(last>first&&name_buffer[last-1U]==' '){last--;}
        if(first==last){UiSound_play(UiSoundId::Error);return;}
        name_length=(uint8_t)(last-first);for(uint8_t i=0U;i<name_length;i++){name_buffer[i]=name_buffer[first+i];}name_buffer[name_length]='\0';
        for(uint8_t i=0U;i<SAVE_PLAYER_COUNT;i++){if(player_directory.players[i].occupied!=0U&&same_name(player_directory.players[i].name,name_buffer)){UiSound_play(UiSoundId::Error);return;}}
        start_password(PasswordPurpose::CreateFirst);UiSound_play(UiSoundId::Confirm);
    }
}

static void handle_password_input(const InputEvent& input){
    uint8_t digit=0U;
    if(Input_is_pressed(input,InputAction::Up)){digit=1U;}
    else if(Input_is_pressed(input,InputAction::Right)){digit=2U;}
    else if(Input_is_pressed(input,InputAction::Down)){digit=3U;}
    else if(Input_is_pressed(input,InputAction::Left)){digit=4U;}
    if(digit!=0U&&password_count<8U){password_buffer[password_count++]=digit;set_value_dirty(0U);if(password_count==8U){complete_password();}else{UiSound_play(UiSoundId::Confirm);}return;}
    if(Input_is_pressed(input,InputAction::Cancel)){
        if(password_count!=0U){password_buffer[--password_count]=0U;set_value_dirty(0U);UiSound_play(UiSoundId::Back);return;}
        if(password_purpose==PasswordPurpose::CreateConfirm){start_password(PasswordPurpose::CreateFirst);}
        else if(password_purpose==PasswordPurpose::CreateFirst){set_page(PageId::NameInput);}
        else if(password_purpose==PasswordPurpose::FactoryReset){set_page(PageId::Settings);}
        else{set_page(PageId::ProfileDetail);}
        UiSound_play(UiSoundId::Back);
    }
}

static void handle_settings(const InputEvent& input,uint32_t now_ms){
    if(Input_is_pressed(input,InputAction::Cancel)||Input_is_pressed(input,InputAction::Home)){
        set_page(Input_is_pressed(input,InputAction::Home)?PageId::Home:settings_return);UiSound_play(UiSoundId::Back);
        return;
    }
    uint16_t repeat_key=Input_repeat_update(&settings_repeat,input.state,input.pressed,(uint16_t)(KEY_UP|KEY_DOWN),now_ms,300U,180U);
    if(repeat_key!=0U){
        uint8_t previous_page=settings_page_for(settings_row),next;
        if(repeat_key==KEY_UP){next=settings_row==0U?(uint8_t)(settings_count-1U):(uint8_t)(settings_row-1U);}
        else{next=(uint8_t)((settings_row+1U)%settings_count);}
        set_focus(next);settings_row=focus;
        if(settings_page_for(settings_row)!=previous_page){first_page_draw=true;focus_dirty=false;}
        UiSound_play(UiSoundId::Move);
    }
    if(Input_is_pressed(input,InputAction::Action1)){adjust_setting(-1);}
    else if(Input_is_pressed(input,InputAction::Action2)){adjust_setting(1);}
    else if(Input_is_pressed(input,InputAction::Confirm)){
        if(settings_row==6U){if(rtc_ready&&Rtc_get_datetime(&edit_time)!=0U){edit_time={2026U,1U,1U,4U,12U,0U,0U};}datetime_field=0U;datetime_return=PageId::Settings;set_page(PageId::DateTime);UiSound_play(UiSoundId::Confirm);}
        else if(settings_row==7U){clock_last_tick=UINT32_MAX;set_page(PageId::Clock);UiSound_play(UiSoundId::Confirm);}
        else if(settings_row==8U){selected_profile=active_player<SAVE_PLAYER_COUNT?active_player:0U;set_page(PageId::ProfileList);UiSound_play(UiSoundId::Confirm);}
        else if(settings_row==9U){Save_audit_health(player_directory);system_status_page=0U;system_last_tick=UINT32_MAX;set_page(PageId::SystemStatus);UiSound_play(UiSoundId::Confirm);}
        else if(settings_row==10U){set_page(PageId::ConfirmResetSettings);UiSound_play(UiSoundId::Confirm);}
        else if(settings_row==11U){start_password(PasswordPurpose::FactoryReset);UiSound_play(UiSoundId::Confirm);}
        else if(settings_row==12U){set_page(PageId::ConfirmShutdown);UiSound_play(UiSoundId::Confirm);}
    }
}

static void handle_datetime(const InputEvent& input){
    if(Input_is_pressed(input,InputAction::Cancel)){set_page(datetime_return);UiSound_play(UiSoundId::Back);return;}
    if(Input_is_pressed(input,InputAction::Left)||Input_is_pressed(input,InputAction::Right)){set_focus((uint8_t)(datetime_field^1U));datetime_field=focus;UiSound_play(UiSoundId::Move);}
    else if(Input_is_pressed(input,InputAction::Up)){uint8_t column=(uint8_t)(datetime_field%2U);uint8_t row=(uint8_t)(datetime_field/2U);set_focus((uint8_t)(((row+2U)%3U)*2U+column));datetime_field=focus;UiSound_play(UiSoundId::Move);}
    else if(Input_is_pressed(input,InputAction::Down)){uint8_t column=(uint8_t)(datetime_field%2U);uint8_t row=(uint8_t)(datetime_field/2U);set_focus((uint8_t)(((row+1U)%3U)*2U+column));datetime_field=focus;UiSound_play(UiSoundId::Move);}
    else if(Input_is_pressed(input,InputAction::Action1)){uint8_t old_day=edit_time.day;adjust_datetime(-1);set_value_dirty(datetime_field);if(edit_time.day!=old_day&&datetime_field!=2U){set_extra_value_dirty(2U);}UiSound_play(UiSoundId::Decrease);}
    else if(Input_is_pressed(input,InputAction::Action2)){uint8_t old_day=edit_time.day;adjust_datetime(1);set_value_dirty(datetime_field);if(edit_time.day!=old_day&&datetime_field!=2U){set_extra_value_dirty(2U);}UiSound_play(UiSoundId::Increase);}
    else if(Input_is_pressed(input,InputAction::Confirm)){
        edit_time.second=0U;
        if(Rtc_set_datetime(&edit_time)==0U){rtc_ready=true;refresh_system_status(HAL_GetTick(),true);set_page(datetime_return);UiSound_play(UiSoundId::Saved);}
        else{UiSound_play(UiSoundId::Error);}
    }
}

static void handle_clock(const InputEvent& input){
    if(Input_is_pressed(input,InputAction::Cancel)){set_page(PageId::Settings);UiSound_play(UiSoundId::Back);}
}

static void handle_system_status(const InputEvent& input){
    if(Input_is_pressed(input,InputAction::Cancel)){set_page(PageId::Settings);UiSound_play(UiSoundId::Back);return;}
    if(Input_is_pressed(input,InputAction::Action1)){
        system_status_page=system_status_page==0U?2U:(uint8_t)(system_status_page-1U);first_page_draw=true;UiSound_play(UiSoundId::PageLeft);
    }
    else if(Input_is_pressed(input,InputAction::Action2)){
        system_status_page=(uint8_t)((system_status_page+1U)%3U);first_page_draw=true;UiSound_play(UiSoundId::PageRight);
    }
}

static PageId confirm_return_page(PageId value){
    if(value==PageId::ConfirmRestart||value==PageId::ConfirmEndGame){return PageId::Pause;}
    if(value==PageId::ConfirmResetSettings||value==PageId::ConfirmFactoryReset||value==PageId::ConfirmShutdown){return PageId::Settings;}
    return PageId::GameInfo;
}

static void handle_confirm(const InputEvent& input){
    if(Input_is_pressed(input,InputAction::Up)||Input_is_pressed(input,InputAction::Down)||Input_is_pressed(input,InputAction::Left)||Input_is_pressed(input,InputAction::Right)){set_focus(focus==0U?1U:0U);UiSound_play(UiSoundId::Move);}
    if(Input_is_pressed(input,InputAction::Cancel)){set_page(confirm_return_page(page));UiSound_play(UiSoundId::Back);return;}
    if(!Input_is_pressed(input,InputAction::Confirm)){return;}
    if(focus==0U){set_page(confirm_return_page(page));UiSound_play(UiSoundId::Back);return;}
    if(page==PageId::ConfirmResetSettings){
        Save_profile_defaults(&profile);apply_loaded_profile();profile_dirty=true;dimmed=false;last_activity_ms=HAL_GetTick();set_page(PageId::Settings);UiSound_play(UiSoundId::Saved);return;
    }
    if(page==PageId::ConfirmFactoryReset){
        UiSound_stop();ui.clear();ui.centered_ascii(86U,"FACTORY RESET",UI_COLOR_DANGER,24U);ui.centered_ascii(132U,"ERASING PLAYER DATA...",UI_COLOR_TEXT,16U);ui.centered_ascii(172U,"DO NOT POWER OFF",UI_COLOR_MUTED,16U);
        factory_reset_pending=true;
        if(!Save_factory_reset()){storage_error(PageId::Settings);return;}
        Save_directory_defaults(&player_directory);Save_profile_defaults(&profile);catalog_defaults_for_registered(&game_catalog);Save_game_data_defaults(&game_data);
        for(uint8_t i=0U;i<game_count();i++){games[i]->persistent_defaults();}
        active_player=0xFFU;selected_profile=0U;snapshot_available_mask=0U;game_active=false;pending_clear_game_id=0U;profile_dirty=false;game_catalog_dirty=false;game_data_dirty=false;factory_reset_pending=false;apply_loaded_profile();settings_return=PageId::Home;
        set_page(PageId::ProfileList);UiSound_play(UiSoundId::Saved);return;
    }
    if(page==PageId::ConfirmShutdown){request_shutdown();return;}
    if(page==PageId::ConfirmNew){
        GameId id=entry_game()->id();pending_clear_game_id=(uint8_t)id;
        if(!clear_game_snapshot(id)){storage_error(PageId::GameInfo);return;}
        set_page(PageId::GameSelect);UiSound_play(UiSoundId::Saved);return;
    }
    if(page==PageId::ConfirmEndGame){
        GameId id=active_game()->id();entry_game_index=active_game_index;selected_variant=active_game()->result().variant;game_menu_snapshot_length=0U;game_active=false;pending_clear_game_id=(uint8_t)id;
        if(!clear_game_snapshot(id)){storage_error(PageId::GameInfo);return;}
        set_page(PageId::GameInfo);UiSound_play(UiSoundId::Back);return;
    }
    entry_game_index=active_game_index;selected_variant=active_game()->result().variant;begin_new_game();
}

static void handle_confirm_delete(const InputEvent& input){
    if(Input_is_pressed(input,InputAction::Up)||Input_is_pressed(input,InputAction::Down)||Input_is_pressed(input,InputAction::Left)||Input_is_pressed(input,InputAction::Right)){set_focus(focus==0U?1U:0U);UiSound_play(UiSoundId::Move);}
    if(Input_is_pressed(input,InputAction::Cancel)){set_page(PageId::ProfileDetail);UiSound_play(UiSoundId::Back);return;}
    if(!Input_is_pressed(input,InputAction::Confirm)){return;}
    if(focus==0U){set_page(PageId::ProfileDetail);UiSound_play(UiSoundId::Back);return;}
    bool deleted_active=selected_profile==active_player;
    if(!delete_selected_profile()){storage_error(PageId::ProfileDetail);return;}
    selected_profile=0U;while(selected_profile<SAVE_PLAYER_COUNT&&player_directory.players[selected_profile].occupied==0U){selected_profile++;}
    if(selected_profile>=SAVE_PLAYER_COUNT){selected_profile=0U;}
    set_page(PageId::ProfileList);UiSound_play(UiSoundId::Saved);
    if(deleted_active){settings_return=PageId::Home;}
}

static void handle_error(const InputEvent& input){
    if(Input_is_pressed(input,InputAction::Confirm)){
        shutdown_failed=false;
        if(!flash_ready||!font_ready||startup_storage_error){
            if(!flash_ready){flash_ready=Flash_init()==0U;}
            font_ready=flash_ready&&font_resources_valid();
            if(font_ready){
                if(active_player<SAVE_PLAYER_COUNT){
                    Save_profile_defaults(&profile);
                    if(!load_player(active_player,&profile,&game_catalog,&game_data)){shutdown_failed=true;startup_storage_error=true;UiSound_play(UiSoundId::Error);return;}
                    apply_loaded_profile();game_catalog_dirty=false;game_data_dirty=false;scan_saved_snapshots();
                }
                startup_storage_error=false;
                open_start_page();
                UiSound_play(UiSoundId::Confirm);
            }
            else{shutdown_failed=true;UiSound_play(UiSoundId::Error);}
        }
        else if(factory_reset_pending){
            UiSound_stop();ui.clear();ui.centered_ascii(86U,"FACTORY RESET",UI_COLOR_DANGER,24U);ui.centered_ascii(132U,"RETRYING ERASE...",UI_COLOR_TEXT,16U);ui.centered_ascii(172U,"DO NOT POWER OFF",UI_COLOR_MUTED,16U);
            if(Save_factory_reset()){
                Save_directory_defaults(&player_directory);Save_profile_defaults(&profile);catalog_defaults_for_registered(&game_catalog);Save_game_data_defaults(&game_data);for(uint8_t i=0U;i<game_count();i++){games[i]->persistent_defaults();}
                active_player=0xFFU;selected_profile=0U;snapshot_available_mask=0U;game_active=false;pending_clear_game_id=0U;profile_dirty=false;game_catalog_dirty=false;game_data_dirty=false;factory_reset_pending=false;apply_loaded_profile();settings_return=PageId::Home;set_page(PageId::ProfileList);UiSound_play(UiSoundId::Saved);
            }
            else{shutdown_failed=true;UiSound_play(UiSoundId::Error);}return;
        }
        else if(Power_shutdown_requested()!=0U){request_shutdown();}
        else if(pending_clear_game_id!=0U){if(clear_game_snapshot((GameId)pending_clear_game_id)&&save_game_data_if_needed(false)&&save_game_catalog_if_needed(false)&&save_profile_if_needed(false)){set_page(error_return);UiSound_play(UiSoundId::Saved);}else{shutdown_failed=true;UiSound_play(UiSoundId::Error);}}
        else if(save_all_active()){set_page(error_return);UiSound_play(UiSoundId::Saved);}
        else{shutdown_failed=true;UiSound_play(UiSoundId::Error);}
    }
    else if(Input_is_pressed(input,InputAction::Cancel)&&Power_shutdown_requested()==0U&&!factory_reset_pending){set_page(error_return);UiSound_play(UiSoundId::Back);}
}

static void dispatch_input(const InputEvent& input,uint32_t now_ms){
    if(Input_is_pressed(input,InputAction::Home)){
        Input_repeat_reset(&game_ring_repeat);
        if(page==PageId::Home){return;}
        if(page==PageId::Pause||page==PageId::ConfirmRestart||page==PageId::ConfirmEndGame){game_active=false;set_page(PageId::Home);UiSound_play(UiSoundId::Back);return;}
        if(page==PageId::DateTime){set_page(active_player<SAVE_PLAYER_COUNT?PageId::Home:PageId::ProfileList);UiSound_play(UiSoundId::Back);return;}
        if(page==PageId::Settings||page==PageId::Clock||page==PageId::SystemStatus||page==PageId::ConfirmResetSettings||page==PageId::ConfirmFactoryReset||page==PageId::ConfirmShutdown||(page==PageId::PasswordInput&&password_purpose==PasswordPurpose::FactoryReset)){set_page(PageId::Home);UiSound_play(UiSoundId::Back);return;}
        if(page==PageId::About||page==PageId::Records||page==PageId::GameInfo||page==PageId::GameSelect||page==PageId::ConfirmNew){set_page(PageId::Home);UiSound_play(UiSoundId::Back);return;}
    }
    switch(page){
    case PageId::Startup:if(Input_is_pressed(input,InputAction::Confirm)){open_start_page();UiSound_play(font_ready?UiSoundId::Confirm:UiSoundId::Error);}break;
    case PageId::Home:handle_home(input,now_ms);break;
    case PageId::About:handle_about(input);break;
    case PageId::GameInfo:handle_game_info(input,now_ms);break;
    case PageId::GameSelect:handle_game_select(input);break;
    case PageId::Playing:
        if(Input_is_pressed(input,InputAction::Home)){return_home_from_game();}
        else if(Input_is_pressed(input,InputAction::Pause)){enter_pause();}
        else if(Input_is_pressed(input,InputAction::Menu)){enter_game_menu();}
        else{active_game()->handle_input(input);}
        break;
    case PageId::GameMenu:handle_game_menu(input);break;
    case PageId::Pause:handle_pause(input);break;
    case PageId::Result:handle_result(input);break;
    case PageId::Settings:handle_settings(input,now_ms);break;
    case PageId::DateTime:handle_datetime(input);break;
    case PageId::Clock:handle_clock(input);break;
    case PageId::SystemStatus:handle_system_status(input);break;
    case PageId::Records:handle_records(input);break;
    case PageId::ConfirmNew:case PageId::ConfirmRestart:case PageId::ConfirmEndGame:case PageId::ConfirmResetSettings:case PageId::ConfirmFactoryReset:case PageId::ConfirmShutdown:handle_confirm(input);break;
    case PageId::ProfileList:handle_profile_list(input);break;
    case PageId::ProfileDetail:handle_profile_detail(input);break;
    case PageId::NameInput:handle_name_input(input);break;
    case PageId::PasswordInput:handle_password_input(input);break;
    case PageId::ConfirmDelete:handle_confirm_delete(input);break;
    case PageId::StorageError:handle_error(input);break;
    default:break;
    }
}

static bool handle_idle(const InputEvent& input,uint32_t now){
    if(input.pressed!=0U){
        last_activity_ms=now;
        if(dimmed){Input_repeat_reset(&game_ring_repeat);Input_repeat_reset(&settings_repeat);ui.set_brightness(brightness_levels[profile.brightness_index]);dimmed=false;refresh_system_status(now,true);UiSound_play(UiSoundId::Wake);return true;}
    }
    if(page==PageId::Playing){last_activity_ms=now;return false;}
    uint32_t limit=auto_off_ms[profile.auto_off_index];uint32_t idle=now-last_activity_ms;
    if(idle>=limit){request_shutdown();return false;}
    uint32_t dim_limit=profile.auto_dim_index<3U?auto_dim_ms[profile.auto_dim_index]:0U;
    if(!dimmed&&dim_limit!=0U&&idle>=dim_limit){ui.set_brightness(20U);dimmed=true;}
    return false;
}

static void initialize(void){
    Save_profile_defaults(&profile);
    catalog_defaults_for_registered(&game_catalog);
    Save_game_data_defaults(&game_data);
    for(uint8_t i=0U;i<game_count();i++){games[i]->persistent_defaults();}
    Save_directory_defaults(&player_directory);
    if(Power_init()!=0U){while(1){}}
    Key_init();Audio_init();
    rtc_ready=Rtc_init()==0U&&Rtc_is_valid()!=0U;
    flash_ready=Flash_init()==0U;
    if(flash_ready){
        font_ready=font_resources_valid();
        if(Save_load_directory(&player_directory)&&player_directory.player_count!=0U){
            active_player=player_directory.active_player;
            if(active_player<SAVE_PLAYER_COUNT){
                if(!load_player(active_player,&profile,&game_catalog,&game_data)){startup_storage_error=true;}
                else{game_catalog_dirty=false;game_data_dirty=false;scan_saved_snapshots();}
            }
            Save_audit_health(player_directory);
        }
    }
    selected_profile=active_player<SAVE_PLAYER_COUNT?active_player:0U;
    UiSound_set_volume(profile.volume_index);
    UiSound_set_density((UiSoundDensity)profile.sound_density);
    ui.set_theme(profile.theme_mode==0U?UiThemeMode::Dark:UiThemeMode::Light);
    if(ui.init(brightness_levels[profile.brightness_index])!=0U){while(1){Key_scan();if(Power_shutdown_requested()!=0U){UiSound_stop();Power_shutdown();}}}
    ui_ready=true;
    Rng_init();
    startup_ms=HAL_GetTick();last_activity_ms=startup_ms;last_status_ms=startup_ms-30000UL;
    refresh_system_status(startup_ms,true);
    first_page_draw=true;
}

}

void App_run(void){
    initialize();
    for(;;){
        Key_scan();
        InputEvent input=Input_capture();
        uint32_t now=HAL_GetTick();
        if(Power_shutdown_requested()!=0U&&!shutdown_failed&&!factory_reset_pending){request_shutdown();}
        if(handle_idle(input,now)){continue;}
        dispatch_input(input,now);
        if(page==PageId::Startup&&(uint32_t)(now-startup_ms)>=800U){open_start_page();}
        if(page==PageId::Playing){
            GameResult before=active_game()->result();
            GameSignal signal=active_game()->update(now);
            if(signal==GameSignal::GameOver||signal==GameSignal::LevelComplete){finish_game();}
            else if(signal==GameSignal::LifeLost){active_game()->draw(ui,false);if(low_battery_warning){low_battery_warning_dirty=true;}UiSound_play(UiSoundId::LifeLost);}
            else if(signal==GameSignal::NeedsDraw){
                GameResult after=active_game()->result();
                if(after.score_change_sound!=0U&&after.score>before.score){
                    uint32_t delta=after.score-before.score;
                    UiSound_play(delta>=50U?UiSoundId::GoldenFruit:(delta>=20U?UiSoundId::ClockFruit:UiSoundId::Food));
                }
                active_game()->draw(ui,false);if(low_battery_warning){low_battery_warning_dirty=true;}
            }
        }
        refresh_system_status(now,false);
        if(!first_page_draw&&page==PageId::Clock&&now/1000UL!=clock_last_tick){clock_last_tick=now/1000UL;draw_clock_content(false);}
        if(!first_page_draw&&page==PageId::SystemStatus&&system_status_page==0U){
            if(now/1000UL!=system_last_tick){system_last_tick=now/1000UL;draw_system_uptime_value();}
            if(status_dirty){draw_system_battery_value();}
        }
        if(first_page_draw){draw_page();if(low_battery_warning){low_battery_warning_dirty=true;}}
        else{
            bool ring_was_dirty=game_ring_dirty;draw_partial();if(ring_was_dirty&&low_battery_warning){low_battery_warning_dirty=true;}
            if(status_dirty&&page_has_header(page)){draw_header();if(low_battery_warning){low_battery_warning_dirty=true;}}
        }
        update_low_battery_warning(now);
    }
}

void App_handle_unexpected_return(void){
    UiSound_stop();
    if(!ui_ready){Power_shutdown();}

    ui.clear();
    ui.centered_ascii(58U,"APP STOPPED",UI_COLOR_DANGER,24U);
    ui.centered_ascii(112U,"SAVING DATA...",UI_COLOR_TEXT,16U);

    bool saved=save_all_active();
    if(!saved){
        ui.clear();
        ui.centered_ascii(48U,"APP STOPPED",UI_COLOR_DANGER,24U);
        ui.centered_ascii(100U,"SAVE FAILED",UI_COLOR_DANGER,16U);
        ui.centered_ascii(132U,"Y RETRY",UI_COLOR_TEXT,16U);
        ui.centered_ascii(164U,"POWER KEPT ON",UI_COLOR_MUTED,16U);
        (void)Input_capture();
        while(!saved){
            Key_scan();
            InputEvent input=Input_capture();
            if(Input_is_pressed(input,InputAction::Confirm)){
                ui.fill(64U,92U,192U,88U,UI_COLOR_BACKGROUND);
                ui.centered_ascii(112U,"SAVING DATA...",UI_COLOR_TEXT,16U);
                saved=save_all_active();
                if(!saved){
                    ui.fill(64U,92U,192U,88U,UI_COLOR_BACKGROUND);
                    ui.centered_ascii(100U,"SAVE FAILED",UI_COLOR_DANGER,16U);
                    ui.centered_ascii(132U,"Y RETRY",UI_COLOR_TEXT,16U);
                    ui.centered_ascii(164U,"POWER KEPT ON",UI_COLOR_MUTED,16U);
                }
            }
        }
    }

    ui.clear();
    ui.centered_ascii(72U,"DATA SAVED",UI_COLOR_TEXT,16U);
    ui.centered_ascii(116U,"POWERING OFF",UI_COLOR_PRIMARY,24U);
    HAL_Delay(800U);
    UiSound_stop();
    Lcd_sleep();
    Power_shutdown();
}
