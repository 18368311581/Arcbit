#ifndef __SAVE_HPP__
#define __SAVE_HPP__

#include "flash.h"
#include "game.hpp"

#define SAVE_PLAYER_COUNT          6U
#define SAVE_LEVEL_COUNT           72U
#define SAVE_DIRECTORY_SLOT_A      0x00700000UL
#define SAVE_DIRECTORY_SLOT_B      0x00702000UL
#define SAVE_SLOT_CAPACITY         0x00002000UL
#define SAVE_PLAYER_BASE           0x00710000UL
#define SAVE_PLAYER_REGION_SIZE    0x00020000UL
#define SAVE_PROFILE_OFFSET_A      0x00000000UL
#define SAVE_PROFILE_OFFSET_B      0x00002000UL
#define SAVE_SNAKE_OFFSET_A        0x00004000UL
#define SAVE_SNAKE_OFFSET_B        0x00006000UL
#define SAVE_EXTRA_GAME_BASE       0x00008000UL
#define SAVE_EXTRA_GAME_STRIDE     0x00002000UL
#define SAVE_GAME_SLOT_SIZE        0x00001000UL
#define SAVE_GAME_CATALOG_OFFSET_A 0x0001A000UL
#define SAVE_GAME_CATALOG_OFFSET_B 0x0001B000UL
#define SAVE_GAME_DATA_OFFSET_A    0x0001C000UL
#define SAVE_GAME_DATA_OFFSET_B    0x0001E000UL
#define SAVE_GAME_DATA_SLOT_SIZE   0x00002000UL
#define SAVE_GLOBAL_BACKUP_BASE    0x007D0000UL
#define SAVE_SELF_TEST_BASE        0x007FF000UL
#define SAVE_SNAPSHOT_CAPACITY     1536U
#define SAVE_PLAYER_NAME_LENGTH    12U
#define SAVE_PROFILE_VERSION       5U
#define SAVE_GAME_CATALOG_VERSION  2U
#define SAVE_GAME_DATA_VERSION     1U
#define SAVE_GAME_VALUE_COUNT      10U
#define SAVE_GAME_DATA_CAPACITY    508U

struct PlayerMeta {
    char name[SAVE_PLAYER_NAME_LENGTH+1U];
    uint32_t password_salt;
    uint32_t password_hash;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t occupied;
    uint8_t reserved[3];
};

struct PlayerDirectory {
    uint8_t active_player;
    uint8_t player_count;
    uint8_t reserved[14];
    PlayerMeta players[SAVE_PLAYER_COUNT];
};

struct AppProfile {
    uint16_t profile_version;
    uint8_t volume_index;
    uint8_t sound_density;
    uint8_t brightness_index;
    uint8_t auto_dim_index;
    uint8_t auto_off_index;
    uint8_t theme_mode;
    uint8_t reserved[24];
};

struct GameCatalogEntry {
    uint32_t activity_score;
    uint16_t activity_progress;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t activity_valid;
    uint8_t game_id;
    uint8_t selected_variant;
    uint8_t reserved[8];
};

struct GameCatalog {
    uint16_t version;
    uint8_t last_game_id;
    uint8_t reserved0;
    GameCatalogEntry games[GAME_CAPACITY];
    uint8_t reserved[12];
};

struct GameDataEntry {
    uint16_t data_version;
    uint16_t payload_length;
    uint8_t payload[SAVE_GAME_DATA_CAPACITY];
};

struct GameDataStore {
    uint16_t version;
    uint8_t reserved[14];
    GameDataEntry games[GAME_CAPACITY];
};

struct SaveHealthStats {
    uint16_t checked_pairs;
    uint16_t healthy_pairs;
    uint16_t repaired_pairs;
    uint16_t degraded_pairs;
    uint16_t invalid_required_pairs;
};

void Save_directory_defaults(PlayerDirectory* directory);
bool Save_load_directory(PlayerDirectory* directory);
bool Save_store_directory(const PlayerDirectory& directory);
void Save_profile_defaults(AppProfile* profile);
bool Save_load_profile(uint8_t player,AppProfile* profile);
bool Save_store_profile(uint8_t player,const AppProfile& profile);
void Save_game_catalog_defaults(GameCatalog* catalog);
bool Save_load_game_catalog(uint8_t player,GameCatalog* catalog);
bool Save_store_game_catalog(uint8_t player,const GameCatalog& catalog);
void Save_game_data_defaults(GameDataStore* data);
bool Save_load_game_data(uint8_t player,GameDataStore* data);
bool Save_store_game_data(uint8_t player,const GameDataStore& data);
bool Save_load_game_snapshot(uint8_t player,GameId game,uint8_t* data,uint16_t capacity,uint16_t* length);
bool Save_store_game_snapshot(uint8_t player,GameId game,const uint8_t* data,uint16_t length);
bool Save_clear_game_snapshot(uint8_t player,GameId game);
bool Save_prepare_player(uint8_t player);
bool Save_factory_reset(void);
uint32_t Save_password_hash(const uint8_t* password,uint8_t length,uint32_t salt);
void Save_audit_health(const PlayerDirectory& directory);
SaveHealthStats Save_health_stats(void);

#endif
