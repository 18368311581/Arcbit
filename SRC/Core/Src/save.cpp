#include "save.hpp"
#include <stddef.h>

#define SAVE_MAGIC              0x47505332UL
#define SAVE_FORMAT_VERSION     2U
#define SAVE_COMMITTED          0x00U
#define SAVE_UNCOMMITTED        0xFFU
#define SAVE_RECORD_DIRECTORY   1U
#define SAVE_RECORD_PROFILE     2U
#define SAVE_RECORD_SNAPSHOT    3U
#define SAVE_RECORD_GAME_CATALOG 4U
#define SAVE_RECORD_GAME_DATA   5U
#define SAVE_GAME_NONE          0U

#pragma pack(push,1)
struct SaveRecordHeader {
    uint32_t magic;
    uint16_t format_version;
    uint8_t record_type;
    uint8_t player_id;
    uint8_t game_id;
    uint8_t reserved0;
    uint32_t sequence;
    uint16_t payload_length;
    uint16_t header_length;
    uint32_t payload_crc;
    uint32_t header_crc;
    uint8_t committed;
    uint8_t reserved[5];
};

#pragma pack(pop)

static_assert(sizeof(SaveRecordHeader)==32U,"Save header format changed");
static_assert(sizeof(GameActivity)==16U,"Game activity format changed");
static_assert(sizeof(GameCatalogEntry)==24U,"Game catalog entry v2 format changed");
static_assert(sizeof(GameCatalog)==256U,"Game catalog v2 format changed");
static_assert(sizeof(AppProfile)==32U,"App profile v5 format changed");
static_assert(sizeof(GameDataEntry)==512U,"Game data entry format changed");
static_assert(sizeof(GameCatalog)+sizeof(SaveRecordHeader)<=FLASH_SECTOR_SIZE,"Game catalog must fit one sector");
static_assert(sizeof(GameDataStore)+sizeof(SaveRecordHeader)<=SAVE_GAME_DATA_SLOT_SIZE,"Game data must fit one slot");
static_assert(sizeof(PlayerDirectory)<SAVE_SLOT_CAPACITY-sizeof(SaveRecordHeader),"Directory exceeds slot");
static_assert(sizeof(AppProfile)<SAVE_SLOT_CAPACITY-sizeof(SaveRecordHeader),"Profile exceeds slot");
static_assert(sizeof(PlayerDirectory)+sizeof(SaveRecordHeader)<=FLASH_SECTOR_SIZE,"Directory payload must fit one sector");
static_assert(sizeof(AppProfile)+sizeof(SaveRecordHeader)<=FLASH_SECTOR_SIZE,"Profile payload must fit one sector");
static_assert(SAVE_SNAPSHOT_CAPACITY+sizeof(SaveRecordHeader)<=FLASH_SECTOR_SIZE,"Snapshot payload must fit one sector");
static_assert(SAVE_PLAYER_BASE+SAVE_PLAYER_COUNT*SAVE_PLAYER_REGION_SIZE==SAVE_GLOBAL_BACKUP_BASE,"Player regions must end at backup area");
static_assert(SAVE_GLOBAL_BACKUP_BASE<SAVE_SELF_TEST_BASE,"Backup area overlaps self-test sector");
static_assert(SAVE_GAME_CATALOG_OFFSET_B+FLASH_SECTOR_SIZE<=SAVE_GAME_DATA_OFFSET_A,"Game catalog overlaps game data");
static_assert(SAVE_GAME_DATA_OFFSET_B+SAVE_GAME_DATA_SLOT_SIZE<=SAVE_PLAYER_REGION_SIZE,"Game data exceeds player region");
static_assert(SAVE_EXTRA_GAME_BASE+(GAME_CAPACITY-2U)*SAVE_EXTRA_GAME_STRIDE+2U*SAVE_GAME_SLOT_SIZE<=SAVE_GAME_CATALOG_OFFSET_A,"Game snapshot slots overlap catalog");

struct SaveSlotState {
    bool valid;
    SaveRecordHeader header;
};

static uint8_t save_verify_buffer[64];
static PlayerDirectory factory_directory_work;
static SaveHealthStats save_health={0U,0U,0U,0U,0U};
static uint16_t save_auto_repaired=0U;

static uint32_t save_crc_update(uint32_t crc,const uint8_t* data,uint32_t length){
    for(uint32_t i=0U;i<length;i++){
        crc^=data[i];
        for(uint8_t bit=0U;bit<8U;bit++){
            crc=(crc&1U)!=0U?(crc>>1U)^0xEDB88320UL:crc>>1U;
        }
    }
    return crc;
}

static uint32_t save_crc(const uint8_t* data,uint32_t length){
    return save_crc_update(0xFFFFFFFFUL,data,length)^0xFFFFFFFFUL;
}

uint32_t Save_password_hash(const uint8_t* password,uint8_t length,uint32_t salt){
    uint32_t crc=save_crc_update(0xFFFFFFFFUL,(const uint8_t*)&salt,sizeof(salt));
    if(password!=NULL&&length!=0U){crc=save_crc_update(crc,password,length);}
    return crc^0xFFFFFFFFUL;
}

static uint32_t save_payload_crc(uint32_t address,uint16_t length,bool* ok){
    uint32_t crc=0xFFFFFFFFUL;
    uint16_t offset=0U;
    *ok=false;
    while(offset<length){
        uint16_t chunk=(uint16_t)(length-offset);
        if(chunk>sizeof(save_verify_buffer)){chunk=sizeof(save_verify_buffer);}
        if(Flash_read(address+offset,save_verify_buffer,chunk)!=0U){return 0U;}
        crc=save_crc_update(crc,save_verify_buffer,chunk);
        offset=(uint16_t)(offset+chunk);
    }
    *ok=true;
    return crc^0xFFFFFFFFUL;
}

static bool save_header_matches(const SaveRecordHeader& header,uint8_t type,uint8_t player,uint8_t game,uint16_t maximum_payload){
    if(header.magic!=SAVE_MAGIC||header.format_version!=SAVE_FORMAT_VERSION){return false;}
    if(header.record_type!=type||header.player_id!=player||header.game_id!=game){return false;}
    if(header.header_length!=sizeof(SaveRecordHeader)||header.payload_length>maximum_payload){return false;}
    if(header.committed!=SAVE_COMMITTED){return false;}
    return save_crc((const uint8_t*)&header,offsetof(SaveRecordHeader,header_crc))==header.header_crc;
}

static SaveSlotState save_read_slot(uint32_t address,uint8_t type,uint8_t player,uint8_t game,uint16_t maximum_payload){
    SaveSlotState state={false,{0}};
    bool read_ok=false;
    if(Flash_read(address,(uint8_t*)&state.header,sizeof(state.header))!=0U){return state;}
    if(!save_header_matches(state.header,type,player,game,maximum_payload)){return state;}
    uint32_t payload_crc=save_payload_crc(address+sizeof(SaveRecordHeader),state.header.payload_length,&read_ok);
    if(!read_ok||payload_crc!=state.header.payload_crc){return state;}
    state.valid=true;
    return state;
}

static bool save_sequence_newer(uint32_t left,uint32_t right){return (int32_t)(left-right)>0;}

static bool save_repair_slot(uint32_t source,uint32_t target,const SaveSlotState& selected,uint8_t type,uint8_t player,uint8_t game,uint16_t maximum_payload);

static bool save_load_latest(uint32_t slot_a,uint32_t slot_b,uint8_t type,uint8_t player,uint8_t game,uint16_t maximum_payload,uint8_t* data,uint16_t capacity,uint16_t* length){
    SaveSlotState a=save_read_slot(slot_a,type,player,game,maximum_payload);
    SaveSlotState b=save_read_slot(slot_b,type,player,game,maximum_payload);
    const SaveSlotState* selected=NULL;
    uint32_t address=0U;
    if(a.valid&&(!b.valid||save_sequence_newer(a.header.sequence,b.header.sequence))){selected=&a;address=slot_a;}
    else if(b.valid){selected=&b;address=slot_b;}
    if(selected==NULL||selected->header.payload_length>capacity){return false;}
    if(a.valid!=b.valid){
        uint32_t target=a.valid?slot_b:slot_a;
        if(save_repair_slot(address,target,*selected,type,player,game,maximum_payload)&&save_auto_repaired!=0xFFFFU){save_auto_repaired++;}
    }
    if(selected->header.payload_length!=0U&&(data==NULL||Flash_read(address+sizeof(SaveRecordHeader),data,selected->header.payload_length)!=0U)){return false;}
    if(length!=NULL){*length=selected->header.payload_length;}
    return true;
}

static bool save_verify_uncommitted(uint32_t address,const SaveRecordHeader& expected){
    SaveRecordHeader actual={0};
    bool read_ok=false;
    if(Flash_read(address,(uint8_t*)&actual,sizeof(actual))!=0U){return false;}
    if(actual.magic!=expected.magic||actual.format_version!=expected.format_version||actual.record_type!=expected.record_type||
       actual.player_id!=expected.player_id||actual.game_id!=expected.game_id||actual.sequence!=expected.sequence||
       actual.payload_length!=expected.payload_length||actual.header_length!=expected.header_length||
       actual.payload_crc!=expected.payload_crc||actual.header_crc!=expected.header_crc||actual.committed!=SAVE_UNCOMMITTED){return false;}
    uint32_t payload_crc=save_payload_crc(address+sizeof(SaveRecordHeader),actual.payload_length,&read_ok);
    return read_ok&&payload_crc==actual.payload_crc;
}

static bool save_erase_used(uint32_t address,uint16_t payload_length){
    uint32_t used=sizeof(SaveRecordHeader)+(uint32_t)payload_length;
    for(uint32_t offset=0U;offset<used;offset+=FLASH_SECTOR_SIZE){
        if(Flash_erase_sector(address+offset)!=0U){return false;}
    }
    return true;
}

static bool save_repair_slot(uint32_t source,uint32_t target,const SaveSlotState& selected,uint8_t type,uint8_t player,uint8_t game,uint16_t maximum_payload){
    SaveRecordHeader header=selected.header;
    header.record_type=type;header.player_id=player;header.game_id=game;header.sequence=selected.header.sequence+1U;
    header.header_crc=save_crc((const uint8_t*)&header,offsetof(SaveRecordHeader,header_crc));header.committed=SAVE_UNCOMMITTED;
    for(uint8_t i=0U;i<sizeof(header.reserved);i++){header.reserved[i]=0xFFU;}
    if(header.payload_length>maximum_payload||!save_erase_used(target,header.payload_length)){return false;}
    if(Flash_write(target,(const uint8_t*)&header,sizeof(header))!=0U){return false;}
    uint16_t offset=0U;
    while(offset<header.payload_length){
        uint16_t chunk=(uint16_t)(header.payload_length-offset);if(chunk>sizeof(save_verify_buffer)){chunk=sizeof(save_verify_buffer);}
        if(Flash_read(source+sizeof(header)+offset,save_verify_buffer,chunk)!=0U||Flash_write(target+sizeof(header)+offset,save_verify_buffer,chunk)!=0U){return false;}
        offset=(uint16_t)(offset+chunk);
    }
    if(!save_verify_uncommitted(target,header)){return false;}
    uint8_t commit=SAVE_COMMITTED;if(Flash_write(target+offsetof(SaveRecordHeader,committed),&commit,1U)!=0U){return false;}
    SaveSlotState repaired=save_read_slot(target,type,player,game,maximum_payload);
    return repaired.valid&&repaired.header.sequence==header.sequence;
}

static bool save_write_latest(uint32_t slot_a,uint32_t slot_b,uint8_t type,uint8_t player,uint8_t game,uint16_t maximum_payload,const uint8_t* data,uint16_t length){
    if(length>maximum_payload||(length!=0U&&data==NULL)){return false;}
    SaveSlotState a=save_read_slot(slot_a,type,player,game,maximum_payload);
    SaveSlotState b=save_read_slot(slot_b,type,player,game,maximum_payload);
    uint32_t target=slot_a;
    uint32_t latest_sequence=0U;
    if(a.valid&&b.valid){
        if(save_sequence_newer(a.header.sequence,b.header.sequence)){latest_sequence=a.header.sequence;target=slot_b;}
        else{latest_sequence=b.header.sequence;target=slot_a;}
    }
    else if(a.valid){latest_sequence=a.header.sequence;target=slot_b;}
    else if(b.valid){latest_sequence=b.header.sequence;target=slot_a;}
    SaveRecordHeader header={0};
    header.magic=SAVE_MAGIC;
    header.format_version=SAVE_FORMAT_VERSION;
    header.record_type=type;
    header.player_id=player;
    header.game_id=game;
    header.sequence=latest_sequence+1U;
    header.payload_length=length;
    header.header_length=sizeof(SaveRecordHeader);
    header.payload_crc=length==0U?save_crc(NULL,0U):save_crc(data,length);
    header.header_crc=save_crc((const uint8_t*)&header,offsetof(SaveRecordHeader,header_crc));
    header.committed=SAVE_UNCOMMITTED;
    for(uint8_t i=0U;i<sizeof(header.reserved);i++){header.reserved[i]=0xFFU;}
    if(!save_erase_used(target,length)){return false;}
    if(Flash_write(target,(const uint8_t*)&header,sizeof(header))!=0U){return false;}
    if(length!=0U&&Flash_write(target+sizeof(header),data,length)!=0U){return false;}
    if(!save_verify_uncommitted(target,header)){return false;}
    uint8_t commit=SAVE_COMMITTED;
    if(Flash_write(target+offsetof(SaveRecordHeader,committed),&commit,1U)!=0U){return false;}
    SaveSlotState final_state=save_read_slot(target,type,player,game,maximum_payload);
    return final_state.valid&&final_state.header.sequence==header.sequence;
}

static uint32_t save_player_base(uint8_t player){
    return SAVE_PLAYER_BASE+(uint32_t)player*SAVE_PLAYER_REGION_SIZE;
}

void Save_directory_defaults(PlayerDirectory* directory){
    if(directory==NULL){return;}
    uint8_t* bytes=(uint8_t*)directory;
    for(uint16_t i=0U;i<sizeof(PlayerDirectory);i++){bytes[i]=0U;}
    directory->active_player=0xFFU;
}

bool Save_load_directory(PlayerDirectory* directory){
    uint16_t length=0U;
    if(directory==NULL||!save_load_latest(SAVE_DIRECTORY_SLOT_A,SAVE_DIRECTORY_SLOT_B,SAVE_RECORD_DIRECTORY,0xFFU,SAVE_GAME_NONE,sizeof(*directory),(uint8_t*)directory,sizeof(*directory),&length)||length!=sizeof(*directory)){return false;}
    uint8_t count=0U;
    for(uint8_t i=0U;i<SAVE_PLAYER_COUNT;i++){
        PlayerMeta& meta=directory->players[i];
        meta.name[SAVE_PLAYER_NAME_LENGTH]='\0';
        if(meta.occupied!=0U){meta.occupied=1U;count++;}
    }
    directory->player_count=count;
    if(count==0U){directory->active_player=0xFFU;}
    else if(directory->active_player!=0xFFU&&(directory->active_player>=SAVE_PLAYER_COUNT||directory->players[directory->active_player].occupied==0U)){return false;}
    return true;
}

bool Save_store_directory(const PlayerDirectory& directory){
    return save_write_latest(SAVE_DIRECTORY_SLOT_A,SAVE_DIRECTORY_SLOT_B,SAVE_RECORD_DIRECTORY,0xFFU,SAVE_GAME_NONE,sizeof(directory),(const uint8_t*)&directory,sizeof(directory));
}

void Save_profile_defaults(AppProfile* profile){
    if(profile==NULL){return;}
    uint8_t* bytes=(uint8_t*)profile;
    for(uint16_t i=0U;i<sizeof(AppProfile);i++){bytes[i]=0U;}
    profile->profile_version=SAVE_PROFILE_VERSION;
    profile->volume_index=1U;
    profile->sound_density=2U;
    profile->brightness_index=2U;
    profile->auto_dim_index=1U;
    profile->auto_off_index=1U;
    profile->theme_mode=0U;
}

bool Save_load_profile(uint8_t player,AppProfile* profile){
    if(player>=SAVE_PLAYER_COUNT||profile==NULL){return false;}
    uint32_t base=save_player_base(player);
    uint16_t length=0U;
    if(!save_load_latest(base+SAVE_PROFILE_OFFSET_A,base+SAVE_PROFILE_OFFSET_B,SAVE_RECORD_PROFILE,player,SAVE_GAME_NONE,sizeof(AppProfile),(uint8_t*)profile,sizeof(AppProfile),&length)||length!=sizeof(AppProfile)||profile->profile_version!=SAVE_PROFILE_VERSION){return false;}
    return profile->volume_index<=4U&&profile->sound_density<=2U&&profile->brightness_index<=4U&&profile->auto_dim_index<=3U&&profile->auto_off_index<=2U&&profile->theme_mode<=1U;
}

bool Save_store_profile(uint8_t player,const AppProfile& profile){
    if(player>=SAVE_PLAYER_COUNT||profile.profile_version!=SAVE_PROFILE_VERSION){return false;}
    uint32_t base=save_player_base(player);
    return save_write_latest(base+SAVE_PROFILE_OFFSET_A,base+SAVE_PROFILE_OFFSET_B,SAVE_RECORD_PROFILE,player,SAVE_GAME_NONE,sizeof(AppProfile),(const uint8_t*)&profile,sizeof(profile));
}

void Save_game_catalog_defaults(GameCatalog* catalog){
    if(catalog==NULL){return;}
    uint8_t* bytes=(uint8_t*)catalog;
    for(uint16_t i=0U;i<sizeof(GameCatalog);i++){bytes[i]=0U;}
    catalog->version=SAVE_GAME_CATALOG_VERSION;
    catalog->last_game_id=(uint8_t)GameId::Snake;
    for(uint8_t i=0U;i<GAME_CAPACITY;i++){catalog->games[i].game_id=(uint8_t)(i+1U);}
}

bool Save_load_game_catalog(uint8_t player,GameCatalog* catalog){
    if(player>=SAVE_PLAYER_COUNT||catalog==NULL){return false;}
    uint32_t base=save_player_base(player);
    uint16_t length=0U;
    if(!save_load_latest(base+SAVE_GAME_CATALOG_OFFSET_A,base+SAVE_GAME_CATALOG_OFFSET_B,SAVE_RECORD_GAME_CATALOG,player,SAVE_GAME_NONE,sizeof(*catalog),(uint8_t*)catalog,sizeof(*catalog),&length)||length!=sizeof(*catalog)||catalog->version!=SAVE_GAME_CATALOG_VERSION){return false;}
    if(catalog->last_game_id==0U||catalog->last_game_id>GAME_CAPACITY){catalog->last_game_id=(uint8_t)GameId::Snake;}
    for(uint8_t i=0U;i<GAME_CAPACITY;i++){
        GameCatalogEntry& entry=catalog->games[i];
        if(entry.game_id!=(uint8_t)(i+1U)||entry.month>12U||entry.day>31U||entry.hour>23U||entry.minute>59U||entry.second>59U){return false;}
    }
    return true;
}

bool Save_store_game_catalog(uint8_t player,const GameCatalog& catalog){
    if(player>=SAVE_PLAYER_COUNT||catalog.version!=SAVE_GAME_CATALOG_VERSION){return false;}
    uint32_t base=save_player_base(player);
    return save_write_latest(base+SAVE_GAME_CATALOG_OFFSET_A,base+SAVE_GAME_CATALOG_OFFSET_B,SAVE_RECORD_GAME_CATALOG,player,SAVE_GAME_NONE,sizeof(catalog),(const uint8_t*)&catalog,sizeof(catalog));
}

void Save_game_data_defaults(GameDataStore* data){
    if(data==NULL){return;}
    uint8_t* bytes=(uint8_t*)data;
    for(uint16_t i=0U;i<sizeof(GameDataStore);i++){bytes[i]=0U;}
    data->version=SAVE_GAME_DATA_VERSION;
}

bool Save_load_game_data(uint8_t player,GameDataStore* data){
    if(player>=SAVE_PLAYER_COUNT||data==NULL){return false;}
    uint32_t base=save_player_base(player);uint16_t length=0U;
    if(!save_load_latest(base+SAVE_GAME_DATA_OFFSET_A,base+SAVE_GAME_DATA_OFFSET_B,SAVE_RECORD_GAME_DATA,player,SAVE_GAME_NONE,sizeof(*data),(uint8_t*)data,sizeof(*data),&length)||length!=sizeof(*data)||data->version!=SAVE_GAME_DATA_VERSION){return false;}
    for(uint8_t i=0U;i<GAME_CAPACITY;i++){if(data->games[i].payload_length>SAVE_GAME_DATA_CAPACITY){return false;}}
    return true;
}

bool Save_store_game_data(uint8_t player,const GameDataStore& data){
    if(player>=SAVE_PLAYER_COUNT||data.version!=SAVE_GAME_DATA_VERSION){return false;}
    for(uint8_t i=0U;i<GAME_CAPACITY;i++){if(data.games[i].payload_length>SAVE_GAME_DATA_CAPACITY){return false;}}
    uint32_t base=save_player_base(player);
    return save_write_latest(base+SAVE_GAME_DATA_OFFSET_A,base+SAVE_GAME_DATA_OFFSET_B,SAVE_RECORD_GAME_DATA,player,SAVE_GAME_NONE,sizeof(data),(const uint8_t*)&data,sizeof(data));
}

static bool save_game_slots(uint8_t player,GameId game,uint32_t* slot_a,uint32_t* slot_b){
    uint8_t id=(uint8_t)game;
    if(player>=SAVE_PLAYER_COUNT||id==0U||id>GAME_CAPACITY||slot_a==NULL||slot_b==NULL){return false;}
    uint32_t base=save_player_base(player);
    if(id==(uint8_t)GameId::Snake){*slot_a=base+SAVE_SNAKE_OFFSET_A;*slot_b=base+SAVE_SNAKE_OFFSET_B;}
    else{
        uint32_t offset=SAVE_EXTRA_GAME_BASE+(uint32_t)(id-2U)*SAVE_EXTRA_GAME_STRIDE;
        *slot_a=base+offset;*slot_b=*slot_a+SAVE_GAME_SLOT_SIZE;
    }
    return true;
}

bool Save_load_game_snapshot(uint8_t player,GameId game,uint8_t* data,uint16_t capacity,uint16_t* length){
    uint32_t slot_a,slot_b;
    if(!save_game_slots(player,game,&slot_a,&slot_b)){return false;}
    return save_load_latest(slot_a,slot_b,SAVE_RECORD_SNAPSHOT,player,(uint8_t)game,SAVE_SNAPSHOT_CAPACITY,data,capacity,length);
}

bool Save_store_game_snapshot(uint8_t player,GameId game,const uint8_t* data,uint16_t length){
    uint32_t slot_a,slot_b;
    if(length==0U||length>SAVE_SNAPSHOT_CAPACITY||!save_game_slots(player,game,&slot_a,&slot_b)){return false;}
    return save_write_latest(slot_a,slot_b,SAVE_RECORD_SNAPSHOT,player,(uint8_t)game,SAVE_SNAPSHOT_CAPACITY,data,length);
}

bool Save_clear_game_snapshot(uint8_t player,GameId game){
    uint32_t slot_a,slot_b;
    if(!save_game_slots(player,game,&slot_a,&slot_b)){return false;}
    return save_write_latest(slot_a,slot_b,SAVE_RECORD_SNAPSHOT,player,(uint8_t)game,SAVE_SNAPSHOT_CAPACITY,NULL,0U);
}

bool Save_prepare_player(uint8_t player){
    if(player>=SAVE_PLAYER_COUNT){return false;}
    uint32_t base=save_player_base(player);
    if(Flash_erase_sector(base+SAVE_PROFILE_OFFSET_A)!=0U||Flash_erase_sector(base+SAVE_PROFILE_OFFSET_B)!=0U||
       Flash_erase_sector(base+SAVE_SNAKE_OFFSET_A)!=0U||Flash_erase_sector(base+SAVE_SNAKE_OFFSET_B)!=0U){return false;}
    for(uint8_t id=2U;id<=GAME_CAPACITY;id++){
        uint32_t offset=SAVE_EXTRA_GAME_BASE+(uint32_t)(id-2U)*SAVE_EXTRA_GAME_STRIDE;
        if(Flash_erase_sector(base+offset)!=0U||Flash_erase_sector(base+offset+SAVE_GAME_SLOT_SIZE)!=0U){return false;}
    }
    if(Flash_erase_sector(base+SAVE_GAME_CATALOG_OFFSET_A)!=0U||Flash_erase_sector(base+SAVE_GAME_CATALOG_OFFSET_B)!=0U){return false;}
    if(Flash_erase_sector(base+SAVE_GAME_DATA_OFFSET_A)!=0U||Flash_erase_sector(base+SAVE_GAME_DATA_OFFSET_A+FLASH_SECTOR_SIZE)!=0U||
       Flash_erase_sector(base+SAVE_GAME_DATA_OFFSET_B)!=0U||Flash_erase_sector(base+SAVE_GAME_DATA_OFFSET_B+FLASH_SECTOR_SIZE)!=0U){return false;}
    return true;
}

bool Save_factory_reset(void){
    for(uint8_t player=0U;player<SAVE_PLAYER_COUNT;player++){
        uint32_t base=save_player_base(player);
        for(uint32_t offset=0U;offset<SAVE_PLAYER_REGION_SIZE;offset+=FLASH_SECTOR_SIZE){if(Flash_erase_sector(base+offset)!=0U){return false;}}
    }
    if(Flash_erase_sector(SAVE_DIRECTORY_SLOT_A)!=0U||Flash_erase_sector(SAVE_DIRECTORY_SLOT_B)!=0U){return false;}
    Save_directory_defaults(&factory_directory_work);
    if(!Save_store_directory(factory_directory_work)){return false;}
    if(!Save_load_directory(&factory_directory_work)){return false;}
    return factory_directory_work.active_player==0xFFU&&factory_directory_work.player_count==0U;
}

static bool save_slot_programmed(uint32_t address){
    uint32_t marker=0xFFFFFFFFUL;
    return Flash_read(address,(uint8_t*)&marker,sizeof(marker))==0U&&marker!=0xFFFFFFFFUL;
}

static void save_audit_pair(uint32_t slot_a,uint32_t slot_b,uint8_t type,uint8_t player,uint8_t game,uint16_t maximum_payload,bool required){
    SaveSlotState a=save_read_slot(slot_a,type,player,game,maximum_payload);
    SaveSlotState b=save_read_slot(slot_b,type,player,game,maximum_payload);
    if(!a.valid&&!b.valid&&!required&&!save_slot_programmed(slot_a)&&!save_slot_programmed(slot_b)){return;}
    save_health.checked_pairs++;
    if(a.valid&&b.valid){save_health.healthy_pairs++;return;}
    if(a.valid||b.valid){
        const SaveSlotState& selected=a.valid?a:b;uint32_t source=a.valid?slot_a:slot_b;uint32_t target=a.valid?slot_b:slot_a;
        if(save_repair_slot(source,target,selected,type,player,game,maximum_payload)){save_health.healthy_pairs++;if(save_auto_repaired!=0xFFFFU){save_auto_repaired++;save_health.repaired_pairs=save_auto_repaired;}}
        else{save_health.degraded_pairs++;}
        return;
    }
    if(required){save_health.invalid_required_pairs++;}else{save_health.degraded_pairs++;}
}

void Save_audit_health(const PlayerDirectory& directory){
    save_health={0U,0U,save_auto_repaired,0U,0U};
    save_audit_pair(SAVE_DIRECTORY_SLOT_A,SAVE_DIRECTORY_SLOT_B,SAVE_RECORD_DIRECTORY,0xFFU,SAVE_GAME_NONE,sizeof(PlayerDirectory),true);
    for(uint8_t player=0U;player<SAVE_PLAYER_COUNT;player++){
        if(directory.players[player].occupied==0U){continue;}
        uint32_t base=save_player_base(player);
        save_audit_pair(base+SAVE_PROFILE_OFFSET_A,base+SAVE_PROFILE_OFFSET_B,SAVE_RECORD_PROFILE,player,SAVE_GAME_NONE,sizeof(AppProfile),true);
        save_audit_pair(base+SAVE_GAME_CATALOG_OFFSET_A,base+SAVE_GAME_CATALOG_OFFSET_B,SAVE_RECORD_GAME_CATALOG,player,SAVE_GAME_NONE,sizeof(GameCatalog),true);
        save_audit_pair(base+SAVE_GAME_DATA_OFFSET_A,base+SAVE_GAME_DATA_OFFSET_B,SAVE_RECORD_GAME_DATA,player,SAVE_GAME_NONE,sizeof(GameDataStore),true);
        for(uint8_t id=1U;id<=GAME_CAPACITY;id++){
            uint32_t slot_a,slot_b;if(!save_game_slots(player,(GameId)id,&slot_a,&slot_b)){continue;}
            save_audit_pair(slot_a,slot_b,SAVE_RECORD_SNAPSHOT,player,id,SAVE_SNAPSHOT_CAPACITY,false);
        }
    }
}

SaveHealthStats Save_health_stats(void){return save_health;}
